/* File author is Ítalo Lima Marconato Matias
 *
 * Created on February 06 of 2021, at 12:22 BRT
 * Last edited on April 10 of 2021, at 17:15 BRT */

#include <sys/arch.hxx>
#include <sys/mm.hxx>
#include <sys/panic.hxx>

using namespace CHicago;

extern "C" Void KernelEntry(BootInfo &Info) {
    /* Hello, World! The loader just exited the EFI environment, and gave control to the kernel! Right now the MMU
     * should be enabled, with everything we need (boot info struct, framebuffer, boot image file, etc) already mapped,
     * and we should be running on higher-half (probably). Before anything, let's call the _init function, this
     * function runs all the global constructors and things like that. */

    if (Info.Magic != BOOT_INFO_MAGIC) Arch::Halt(True);

#ifdef USE_INIT_ARRAY
    for (UIntPtr *i = &__init_array_start; i < &__init_array_end; i++) reinterpret_cast<Void(*)()>(*i)();
#else
    _init();
#endif

    /* Initialize the debug interface (change this later to also possibly not use the screen). */

    Debug = TextConsole(Info, 0, 0xFFFFFF00);
    Debug.SetForeground(0xFF00FF00);
    Debug.Write("initializing the kernel, arch = {}, version = {}\n", ARCH, VERSION);
    Debug.RestoreForeground();

    /* Initialize the arch-specific bits. */

    Arch::Initialize(Info);

    /* Initialize some important early system bits (backtrace symbol resolver, memory manager, etc). */

    StackTrace::Initialize(Info);
    PhysMem::Initialize(Info);
    VirtMem::Initialize(Info);

    /* Initialize/map all the ACPI tables that we need for now. */

    Acpi::Initialize(Info);

    /* And for now our initialization is finished. */

    Debug.SetForeground(0xFF00FF00);
    Debug.Write("initialization finished, halting the machine\n");
    Debug.RestoreForeground();

    *((volatile UIntPtr*)nullptr) = 0;
    ASSERT(False);
}
