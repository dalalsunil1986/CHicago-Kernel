/* File author is Ítalo Lima Marconato Matias
 *
 * Created on February 04 of 2021, at 18:30 BRT
 * Last edited on February 06 of 2021 at 10:41 BRT */

#include <boot.hxx>

extern "C" Void KernelEntry(BootInfo *Info) {
    /* Clear the screen (to indicate that we got into the kernel) and halt. */

    for (UIntPtr i = 0; i < Info->FrameBuffer.Width * Info->FrameBuffer.Height; i++) {
        ((UInt32*)Info->FrameBuffer.Address)[i] = 0xFF00FF00;
    }

    while (True) {
        asm volatile("hlt");
    }
}
