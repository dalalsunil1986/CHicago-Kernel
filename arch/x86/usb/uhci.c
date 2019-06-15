// File author is Ítalo Lima Marconato Matias
//
// Created on June 15 of 2019, at 10:11 BRT
// Last edited on June 15 of 2019, at 17:55 BRT

#include <chicago/arch/pci.h>
#include <chicago/arch/port.h>
#include <chicago/arch/usb.h>

#include <chicago/alloc.h>
#include <chicago/debug.h>
#include <chicago/mm.h>
#include <chicago/string.h>
#include <chicago/timer.h>

static UInt32 UHCIReadRegister(PUHCIDevice dev, UInt8 reg) {
	if (reg == UHCI_SOFMOD) {																											// Read a single byte?
		return PortInByte(dev->base + reg);																								// Yes
	} else if (reg == UHCI_FRBASEADD) {																									// Read four bytes?
		return PortInLong(dev->base + reg) & ~0xFFF;																					// Yes, let's also strip the first bits
	}
	
	return PortInWord(dev->base + reg);																									// Nope, read two bytes
}

static Void UHCIWriteRegister(PUHCIDevice dev, UInt8 reg, UInt32 val) {
	if (reg == UHCI_SOFMOD) {																											// Write a single byte?
		PortOutByte(dev->base + reg, (UInt8)val);																						// Yes
	} else if (reg == UHCI_FRBASEADD) {																									// Write four bytes?
		PortOutLong(dev->base + reg, MmGetPhys(val & ~0xFFF));																			// Yes, let's get the physical address and also strip the first bits from it
	} else {
		PortOutWord(dev->base + reg, (UInt16)val);																						// Nope, write two bytes
	}
}

static Void UHCISetPortScBit(PUHCIDevice dev, UInt8 port, UInt32 bit) {
	UInt32 val = UHCIReadRegister(dev, UHCI_PORTSC + (port * 2));																		// Read the current value from PORTSC<port>
	
	val |= bit;																															// Set the bit
	
	UHCIWriteRegister(dev, UHCI_PORTSC + (port * 2), val);																				// And write back
}

static Void UHCIClearPortScBit(PUHCIDevice dev, UInt8 port, UInt32 bit) {
	UInt32 val = UHCIReadRegister(dev, UHCI_PORTSC + (port * 2));																		// Read the current value from PORTSC<port>
	
	val &= ~bit;																														// Clear the bit
	
	UHCIWriteRegister(dev, UHCI_PORTSC + (port * 2), val);																				// And write back
}

static PUHCIQueueHead UHCIAllocQueueHead(PUHCIDevice dev, UInt32 qhlp, UInt32 qelp) {
	ListForeach(dev->qheads, i) {																										// First, let's see if we can find any free one
		PUHCIQueueEntry entry = (PUHCIQueueEntry)i->data;
		
		if (entry->fcount > 0) {																										// Any free one here?
			for (UInt32 j = 0; j < 8; j++) {																							// Yes, let's search for it!
				if (!entry->heads[j].used) {																							// Found?
					entry->heads[j].qhlp = qhlp;																						// Yes, set everything and return
					entry->heads[j].qelp = qelp;
					entry->heads[j].used = True;
					
					return &entry->heads[j];
				}
			}
		}
	}
	
	PUHCIQueueEntry entry = (PUHCIQueueEntry)MemAAllocate(sizeof(UHCIQueueEntry), 16);													// Not found, let's alloc a new one!
	
	if (entry == Null) {
		return Null;																													// Failed :(
	} else if (!ListAdd(dev->qheads, entry)) {																							// Add it to our queue head list
		MemAFree((UIntPtr)entry);																										// Failed :(
		return Null;
	}
	
	StrSetMemory(entry, 0, sizeof(UHCIQueueEntry));																						// Clear everything
	
	entry->fcount = 7;																													// Set how many free entries we have (7, as we are going to use 1)
	
	entry->heads[0].qhlp = qhlp;																										// Set all the fields from the qh that we need and return
	entry->heads[0].qelp = qelp;
	entry->heads[0].used = True;
	
	return &entry->heads[0];
}

static Boolean UHCIDetect(PUHCIDevice dev) {
	UHCIWriteRegister(dev, UHCI_USBCMD, 0x04);																							// Set the GRESET bit in the USBCMD register
	TimerSleep(50);																														// Wait 10ms
	UHCIWriteRegister(dev, UHCI_USBCMD, 0x00);																							// Clear the GRESET bit in the USBCMD register
	
	if ((UHCIReadRegister(dev, UHCI_USBCMD) != 0x00) || (UHCIReadRegister(dev, UHCI_USBSTS) != 0x20)) {									// Check if the registers are in their default state
		return False;																													// Nope :(
	}
	
	UHCIWriteRegister(dev, UHCI_USBSTS, 0xFF);																							// Clear the write-clear out of the USBSTS
	
	if (UHCIReadRegister(dev, UHCI_SOFMOD) != 0x40) {																					// Check if the SOFMOD register is in it default state
		return False;																													// ...
	}
	
	UHCIWriteRegister(dev, UHCI_USBCMD, 0x02);																							// Let's set the HCRESET bit in the USBCMD register
	TimerSleep(50);																														// Wait some time
	
	return (UHCIReadRegister(dev, UHCI_USBCMD) & 0x02) != 0x02;																			// And check if it's unset again
}

static Boolean UHCIResetPort(PUHCIDevice dev, UInt8 port) {
	UHCISetPortScBit(dev, port, UHCI_PORTSC_PR);																						// Set the PR bit in the PORTSC<port> register
	TimerSleep(50);																														// Wait 50ms
	UHCIClearPortScBit(dev, port, UHCI_PORTSC_PR);																						// Clear the PR bit in the PORTSC<port> register
	TimerSleep(10);																														// Wait 10ms
	
	for (UInt32 i = 0; i < 16; i++) {																									// Let's wait until the port is enabled again
		UInt16 val = UHCIReadRegister(dev, UHCI_PORTSC + (port * 2));																	// Read the PORTSC<port> register
		
		if ((val & UHCI_PORTSC_CCS) != UHCI_PORTSC_CCS) {																				// Nothing attached here?
			return True;																												// Yes, so let's just return
		} else if ((val & (UHCI_PORTSC_CSC | UHCI_PORTSC_PEC)) != 0) {																	// Check if the CSC or the PEC bit is set
			UHCIClearPortScBit(dev, port, UHCI_PORTSC_CSC | UHCI_PORTSC_PEC);															// Clear if it's set
		} else if ((val & UHCI_PORTSC_PE) == UHCI_PORTSC_PE) {																			// The PE bit is set?
			return True;																												// Yes :)
		} else {
			UHCISetPortScBit(dev, port, UHCI_PORTSC_PE);																				// Just set the PE bit
		}
		
		TimerSleep(10);																													// Wait 10ms before the next try/check
	}
	
	return False;
}

static Boolean UHCICheckPort(PUHCIDevice dev, UInt8 port) {
	if ((UHCIReadRegister(dev, UHCI_PORTSC + (port * 2)) & UHCI_PORTSC_RES) != UHCI_PORTSC_RES) {										// Check if the reserved bit is zero
		return False;																													// Yes, so this is not a valid port
	}
	
	UHCIClearPortScBit(dev, port, UHCI_PORTSC_RES);																						// Try to clear the reserved bit
	
	if ((UHCIReadRegister(dev, UHCI_PORTSC + (port * 2)) & UHCI_PORTSC_RES) != UHCI_PORTSC_RES) {										// Check if the reserved bit is zero
		return False;																													// Yes, so this is not a valid port
	}
	
	UHCISetPortScBit(dev, port, UHCI_PORTSC_RES);																						// Try to set the reserved bit
	
	if ((UHCIReadRegister(dev, UHCI_PORTSC + (port * 2)) & UHCI_PORTSC_RES) != UHCI_PORTSC_RES) {										// Check if the reserved bit is zero
		return False;																													// Yes, so this is not a valid port
	}
	
	UHCISetPortScBit(dev, port, 0x0A);																									// Try to set bits 3:1 to 1
	
	return (UHCIReadRegister(dev, UHCI_PORTSC + (port * 2)) & 0x0A) != 0x0A;															// Check if it got cleared, if it got, it's a valid port
}

static Boolean UHCIInitFrameList(PUHCIDevice dev) {
	dev->frames = (PUInt32)MemAAllocate(MM_PAGE_SIZE, MM_PAGE_SIZE);																	// Alloc the 4KB-aligned 1024 entries
	
	if (dev->frames == Null) {
		return False;																													// Failed :(
	}
	
	dev->qheads = ListNew(False, False);																								// Init our queue heads list
	
	if (dev->qheads == Null) {
		MemAFree((UIntPtr)dev->frames);																									// Failed :(
		return False;
	}
	
	PUHCIQueueHead qh = UHCIAllocQueueHead(dev, 1, 1);																					// Alloc a single queue head
	
	if (qh == Null) {
		ListFree(dev->qheads);																											// Failed :(
		MemAFree((UIntPtr)dev->frames);
		return False;
	}
	
	for (UInt32 i = 0; i < 1024; i++) {																									// Now, init the frame list
		dev->frames[i] = (UInt32)qh | 0x02;
	}
	
	return True;
}

static Void UHCIHandler(PVoid priv) {
	Volatile PUHCIDevice dev = (PUHCIDevice)priv;																						// We don't have anything to do for now :/
	DbgWriteFormated("[x86] UHCIHandler()\r\n");
	(Void)dev;
}

Void UHCIInit(PPCIDevice pdev) {
	PUHCIDevice dev = (PUHCIDevice)MemAllocate(sizeof(UHCIDevice));																		// Alloc the UHCI device struct
	
	if (dev == Null) {
		return;																															// Failed
	}
	
	dev->pdev = pdev;
	
	DbgWriteFormated("[x86] Found UHCI controller at %d:%d:%d\r\n", pdev->bus, pdev->slot, pdev->func);
	
	if ((pdev->bar4 & 0x01) != 0x01) {																									// We have the IO base?
		DbgWriteFormated("      This device doesn't have the IO base\r\n");																// No, so let's just cancel and go to the next controller
		MemFree((UIntPtr)dev);
		return;
	}
	
	dev->base = pdev->bar4 & ~1;																										// Get the IO base
	
	DbgWriteFormated("      The IO base is 0x%x\r\n", dev->base);
	
	PCIEnableBusMaster(pdev);																											// Enable bus mastering
	DbgWriteFormated("      Enabled bus matering\r\n");
	
	PCIWriteDWord(pdev, 0x34, 0);																										// Write 0 to dwords 0x34 and 0x38
	PCIWriteDWord(pdev, 0x38, 0);
	DbgWriteFormated("      Wrote 0 to dwords 0x34 and 0x38 of this device's PCI configuration space\r\n");
	
	PCIWriteDWord(pdev, UHCI_LEGACY_SUPPORT, 0x8F00);																					// Disable legacy support
	DbgWriteFormated("      Disabled legacy support\r\n");
	
	if (!UHCIDetect(dev)) {																												// Let's reset it and check if we really have a working USB host controller here
		DbgWriteFormated("      Couldn't reset and check the host controller\r\n");														// Nope, we don't have :(
		MemFree((UIntPtr)dev);
		return;
	}
	
	DbgWriteFormated("      Reseted and checked the host controller\r\n");
	
	if (!UHCIInitFrameList(dev)) {																										// Init the frame list
		MemFree((UIntPtr)dev);																											// Failed
		return;
	}
	
	DbgWriteFormated("      Initialized the frame list\r\n");
	
	PCIRegisterIRQHandler(pdev, UHCIHandler, dev);																						// Register the IRQ handler
	DbgWriteFormated("      Registered the IRQ handler\r\n");
	
	UHCIWriteRegister(dev, UHCI_USBINTR, 0x0F);																							// Set all the four bits from the USBINTR register
	DbgWriteFormated("      Set all the four bits of USBINTR\r\n");
	
	UHCIWriteRegister(dev, UHCI_FRNUM, 0x00);																							// Clear the FRNUM register
	DbgWriteFormated("      Cleared the FRNUM register\r\n");
	
	UHCIWriteRegister(dev, UHCI_FRBASEADD, (UInt32)dev->frames);																		// Set the FRBASEADD to our frame list
	DbgWriteFormated("      Set the FRBASEADD register to 0x%x\r\n", (UInt32)dev->frames);
	
	UHCIWriteRegister(dev, UHCI_SOFMOD, 0x40);																							// Set the SOFMOD register to 0x40, it should already be, but let's make sure that it is
	DbgWriteFormated("      Set the SOFMOD register to 0x40\r\n");
	
	UHCIWriteRegister(dev, UHCI_USBSTS, 0xFFFF);																						// Clear the USBSTS register
	DbgWriteFormated("      Cleared the USBSTS register\r\n");
	
	UHCIWriteRegister(dev, UHCI_USBCMD, 0x41);																							// Set the RS and the CF bits in the USBCMD register, this will finally start the controller
	DbgWriteFormated("      Initialized the UHCI controller\r\n");
	
	dev->ports = 0;																														// Let's get the port count
	
	while (UHCICheckPort(dev, dev->ports)) {																							// Check if this port is valid
		dev->ports++;																													// If it is increase the amount of valid ports
	}
	
	DbgWriteFormated("      There are %d ports\r\n", dev->ports);
}
