// File author is Ítalo Lima Marconato Matias
//
// Created on July 28 of 2018, at 01:09 BRT
// Last edited on January 18 of 2020, at 19:18 BRT

#define __CHICAGO_ARCH_PROCESS__

#include <chicago/arch/gdt.h>
#include <chicago/arch/port.h>
#include <chicago/arch/process.h>
#include <chicago/arch/idt.h>

#include <chicago/alloc.h>
#include <chicago/arch.h>
#include <chicago/debug.h>
#include <chicago/mm.h>
#include <chicago/panic.h>
#include <chicago/process.h>
#include <chicago/string.h>
#include <chicago/timer.h>

Aligned(16) UInt8 PsFPUStateSave[512];
Aligned(16) UInt8 PsFPUDefaultState[512];

static Boolean PsRequeue = True;

PContext PsCreateContext(UIntPtr entry, UIntPtr userstack, Boolean user) {
	PContext ctx = (PContext)MemAllocate(sizeof(Context));															// Alloc some space for the context struct
	
	if (ctx == Null) {
		return Null;																								// Failed :(
	}
	
	PUIntPtr kstack = (PUIntPtr)(ctx->kstack + PS_STACK_SIZE - 8);													// Let's setup the context registers!
	
	*kstack-- = user ? 0x23 : 0x10;																					// Push what we need for using the IRET in the first schedule
	*kstack-- = user ? userstack : (UIntPtr)(ctx->kstack + PS_STACK_SIZE - 8);
	*kstack-- = 0x202;
	*kstack-- = user ? 0x1B : 0x08;
	*kstack-- = entry;
	*kstack-- = 0;																									// And all the other registers that we need
	*kstack-- = 0;
	*kstack-- = 0;
	*kstack-- = 0;
	*kstack-- = 0;
	*kstack-- = 0;
	*kstack-- = user ? userstack : (UIntPtr)(ctx->kstack + PS_STACK_SIZE - 8);
	*kstack-- = 0;
	*kstack-- = 0;
	*kstack-- = 0;
	*kstack-- = user ? 0x23 : 0x10;
	*kstack = user ? 0x23 : 0x10;
	
	ctx->esp = (UIntPtr)kstack;
	ctx->fs = 0;
	ctx->gs = 0;
	
	StrCopyMemory(ctx->fpu_state, PsFPUDefaultState, 512);															// Setup the default fpu state
	
	return ctx;
}

Void PsFreeContext(PContext ctx) {
	MemFree((UIntPtr)ctx);																							// Just MemFree the ctx!
}

Void PsSwitchTaskForce(PRegisters regs) {
	if (!PsTaskSwitchEnabled) {																						// We can switch?
		return;																										// Nope
	}
	
	PThread old = PsCurrentThread;																					// Save the old thread
	
	if (old != Null) {																								// Save the old thread info?
		old->cprio = old->cprio == PS_PRIORITY_VERYLOW ? old->prio : old->cprio + 1;								// Yeah, set the new priority
		old->time = (old->cprio + 1) * 5 - 1;																		// Set the quantum to the old thread
		old->ctx->esp = (UIntPtr)regs;																				// Save the old context
		Asm Volatile("fxsave (%0)" :: "r"(PsFPUStateSave));															// And the old fpu state
		StrCopyMemory(old->ctx->fpu_state, PsFPUStateSave, 512);
		
		if (PsRequeue) {																							// Add the old process to the queue again?
			PsAddToQueue(old, old->cprio);																			// Yes :)
		} else {
			PsRequeue = True;																						// No
		}
	}
	
	PsCurrentThread = PsGetNext();																					// Get the next thread
	
	GDTSetKernelStack((UInt32)(PsCurrentThread->ctx->kstack) + PS_STACK_SIZE - 8);									// Switch the kernel stack in the tss
	StrCopyMemory(PsFPUStateSave, PsCurrentThread->ctx->fpu_state, 512);											// And load the new fpu state
	Asm Volatile("fxrstor (%0)" :: "r"(PsFPUStateSave));
	
	if (((old != Null) && (PsCurrentProcess->dir != old->parent->dir)) || (old == Null)) {							// Switch the page dir
		MmSwitchDirectory(PsCurrentProcess->dir);
	}
	
	if (((old != Null) && (PsCurrentThread->ctx->fs != old->ctx->fs)) || (old == Null)) {							// Switch FS
		GDTSetFS(PsCurrentThread->ctx->fs);
	}
	
	if (((old != Null) && (PsCurrentThread->ctx->gs != old->ctx->gs)) || (old == Null)) {							// Switch GS
		GDTSetGS(PsCurrentThread->ctx->gs);
	}
	
	Asm Volatile("mov %%eax, %%esp" :: "a"(PsCurrentThread->ctx->esp));												// And let's switch!
	Asm Volatile("pop %es");
	Asm Volatile("pop %ds");
	Asm Volatile("popa");
	Asm Volatile("add $8, %esp");
	Asm Volatile("iret");
}

Void PsSwitchTaskTimer(PRegisters regs) {
	if (!PsTaskSwitchEnabled) {																						// We can switch?
		return;																										// Nope
	} else if (PsCurrentThread->time != 0) {																		// This process still have time to run?
		PsCurrentThread->time--;																					// Yes!
		return;
	}
	
	PThread old = PsCurrentThread;																					// Save the old thread
	
	old->cprio = old->cprio == PS_PRIORITY_VERYLOW ? old->prio : old->cprio + 1;									// Set the new priority (old thread)
	old->time = (old->cprio + 1) * 5 - 1;																			// Set the quantum to the old thread
	old->ctx->esp = (UIntPtr)regs;																					// Save the old context
	PsAddToQueue(old, old->cprio);																					// Add the old thread to the queue again
	
	PsCurrentThread = PsGetNext();																					// Get the next thread
	
	GDTSetKernelStack((UInt32)(PsCurrentThread->ctx->kstack) + PS_STACK_SIZE - 8);									// Switch the kernel stack in the tss
	Asm Volatile("fxsave (%0)" :: "r"(PsFPUStateSave));																// Save the old fpu state
	StrCopyMemory(old->ctx->fpu_state, PsFPUStateSave, 512);
	StrCopyMemory(PsFPUStateSave, PsCurrentThread->ctx->fpu_state, 512);											// And load the new one
	Asm Volatile("fxrstor (%0)" :: "r"(PsFPUStateSave));
	
	if (PsCurrentProcess->dir != old->parent->dir) {																// Switch the page dir
		MmSwitchDirectory(PsCurrentProcess->dir);
	}
	
	if (PsCurrentThread->ctx->fs != old->ctx->fs) {																	// Switch FS
		GDTSetFS(PsCurrentThread->ctx->fs);
	}
	
	if (PsCurrentThread->ctx->gs != old->ctx->gs) {																	// Switch GS
		GDTSetGS(PsCurrentThread->ctx->gs);
	}
	
	PortOutByte(0x20, 0x20);																						// Send EOI
	Asm Volatile("mov %%eax, %%esp" :: "a"(PsCurrentThread->ctx->esp));												// And let's switch!
	Asm Volatile("pop %es");
	Asm Volatile("pop %ds");
	Asm Volatile("popa");
	Asm Volatile("add $8, %esp");
	Asm Volatile("iret");
}

Void PsSwitchTask(PVoid priv) {
	if (priv != Null && priv != PsDontRequeue) {																	// Timer?
start:	ListForeach(&PsSleepList, i) {																				// Yes
			PThread th = (PThread)i->data;
			
			if (th->wtime == 0) {																					// Wakeup?
				PsWakeup(&PsSleepList, th);																			// Yes :)
				goto start;																							// Go back to the start!
			} else {
				th->wtime--;																						// Nope, just decrese the wtime counter
			}
		}
	}
	
	if (!PsTaskSwitchEnabled) {																						// We can switch?
		return;																										// Nope
	} else if (priv != Null && priv != PsDontRequeue) {																// Use timer?
		PsSwitchTaskTimer((PRegisters)priv);																		// Yes!
	} else {
		if (priv == PsDontRequeue) {																				// Requeue?
			PsRequeue = False;																						// Nope
		}
		
		Asm Volatile("sti; int $0x3E");																				// Let's use int 0x3E!
	}
}
