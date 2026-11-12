/** KirinApRaw - the state of a secondary CPU exactly as an OS receives it.

  KirinApDiag went through the firmware's MP Services, which initialise the
  CPU first. Windows does not: it calls PSCI CPU_ON itself and its own start-up
  code runs on whatever the secure firmware hands over. This test does the
  same for ONE secondary CPU (MPIDR 0x100): PSCI CPU_ON to a tiny stub that
  only reads registers into memory and then powers the CPU off again.

  It also reads (read-only) the interrupt-controller redistributor state of
  CPU 0 and CPU 1, in particular which software interrupts (SGIs, used for
  CPU-to-CPU signalling) are available to the normal world.

  Output: screen + \EFI\BOOT\logs\APRAW.TXT. Cold boot afterwards.
**/
#include <Uefi.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseMemoryLib.h>

#define PSCI_VERSION        0x84000000ULL
#define PSCI_AFFINITY_INFO  0xC4000004ULL
#define PSCI_FEATURES       0x8400000AULL
#define PSCI_CPU_ON         0xC4000003ULL


STATIC EFI_FILE_PROTOCOL  *mLog;

STATIC
VOID
EFIAPI
Log (
  IN CONST CHAR16  *Format,
  ...
  )
{
  STATIC CHAR16  Wide[320];
  STATIC CHAR8   Narrow[324];
  VA_LIST        Args;
  UINTN          Len, I;

  VA_START (Args, Format);
  Len = UnicodeVSPrint (Wide, sizeof (Wide), Format, Args);
  VA_END (Args);
  Print (L"%s\n", Wide);
  if (mLog != NULL) {
    for (I = 0; I < Len && I < sizeof (Narrow) - 2; I++) {
      Narrow[I] = (Wide[I] < 0x80) ? (CHAR8)Wide[I] : '?';
    }
    Narrow[I++] = '\r';
    Narrow[I++] = '\n';
    mLog->Write (mLog, &I, Narrow);
    mLog->Flush (mLog);
  }
}

STATIC
VOID
OpenLog (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_LOADED_IMAGE_PROTOCOL        *Li;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root, *Dir;
  UINT64                           Rw = EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE;

  if (EFI_ERROR (gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Li)) ||
      EFI_ERROR (gBS->HandleProtocol (Li->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs)) ||
      EFI_ERROR (Fs->OpenVolume (Fs, &Root))) {
    return;
  }
  if (!EFI_ERROR (Root->Open (Root, &Dir, L"\\EFI\\BOOT\\logs", Rw | EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY))) {
    if (!EFI_ERROR (Dir->Open (Dir, &mLog, L"APRAW.TXT", Rw | EFI_FILE_MODE_CREATE, 0))) {
      mLog->SetPosition (mLog, MAX_UINT64);
    } else {
      mLog = NULL;
    }
    Dir->Close (Dir);
  }
  Root->Close (Root);
}

// SMC Calling Convention: arguments and result in x0-x3, x4-x17 may be clobbered.
STATIC
UINT64
Smc (
  IN UINT64  Function,
  IN UINT64  Arg1,
  IN UINT64  Arg2,
  IN UINT64  Arg3
  )
{
  register UINT64  X0 __asm__ ("x0") = Function;
  register UINT64  X1 __asm__ ("x1") = Arg1;
  register UINT64  X2 __asm__ ("x2") = Arg2;
  register UINT64  X3 __asm__ ("x3") = Arg3;

  __asm__ __volatile__ (
    "smc #0"
    : "+r" (X0), "+r" (X1), "+r" (X2), "+r" (X3)
    :
    : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory"
    );
  return X0;
}



extern VOID KirinApRawEntry (VOID);
extern VOID KirinApRawEnd (VOID);

#define GICD_BASE   0xFF000000ULL
#define GICR_BASE   0xFF040000ULL
#define GICR_STRIDE 0x20000ULL

STATIC
VOID
CacheOp (
  IN VOID     *Start,
  IN UINTN    Length,
  IN BOOLEAN  Invalidate
  )
{
  UINT64  Ctr;
  UINTN   Line, Addr, End;

  __asm__ __volatile__ ("mrs %0, ctr_el0" : "=r" (Ctr));
  Line = (UINTN)4 << ((Ctr >> 16) & 0xF);
  End  = (UINTN)Start + Length;
  for (Addr = (UINTN)Start & ~(Line - 1); Addr < End; Addr += Line) {
    if (Invalidate) {
      __asm__ __volatile__ ("dc civac, %0" : : "r" (Addr));
    } else {
      __asm__ __volatile__ ("dc cvac, %0" : : "r" (Addr));
    }
  }
  __asm__ __volatile__ ("dsb sy");
}

STATIC UINT32 Mmio32 (UINT64 A) { return *(volatile UINT32 *)(UINTN)A; }
STATIC UINT64 Mmio64 (UINT64 A) { return *(volatile UINT64 *)(UINTN)A; }

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            S;
  EFI_PHYSICAL_ADDRESS  Page = 0;
  volatile UINT64       *R;
  UINT64                Ret, V;
  UINTN                 I;

  OpenLog (ImageHandle);
  Log (L"===== KIRIN AP RAW STATE =====");

  // Reference values on the boot CPU.
  __asm__ __volatile__ ("mrs %0, cntvoff_el2" : "=r" (V));  Log (L"BOOT CNTVOFF_EL2 = %lx", V);
  __asm__ __volatile__ ("mrs %0, cptr_el2" : "=r" (V));     Log (L"BOOT CPTR_EL2    = %lx", V);
  __asm__ __volatile__ ("mrs %0, sctlr_el2" : "=r" (V));    Log (L"BOOT SCTLR_EL2   = %lx", V);
  __asm__ __volatile__ ("mrs %0, mdcr_el2" : "=r" (V));     Log (L"BOOT MDCR_EL2    = %lx", V);
  __asm__ __volatile__ ("mrs %0, hstr_el2" : "=r" (V));     Log (L"BOOT HSTR_EL2    = %lx", V);

  S = gBS->AllocatePages (AllocateAnyPages, EfiBootServicesData, 1, &Page);
  if (EFI_ERROR (S)) {
    Log (L"AllocatePages: %r", S);
    goto Done;
  }
  R = (volatile UINT64 *)(UINTN)Page;
  for (I = 0; I < 512; I++) {
    R[I] = 0;
  }
  // The secondary runs with caches off: make sure RAM holds the zeros and the
  // stub code, and that this CPU has no cached copy of the report page.
  CacheOp ((VOID *)(UINTN)KirinApRawEntry, (UINTN)KirinApRawEnd - (UINTN)KirinApRawEntry, FALSE);
  CacheOp ((VOID *)(UINTN)Page, 4096, TRUE);

  Log (L"PSCI CPU_ON target=0x100 entry=%lx report=%lx", (UINT64)(UINTN)KirinApRawEntry, (UINT64)Page);
  Ret = Smc (PSCI_CPU_ON, 0x100, (UINT64)(UINTN)KirinApRawEntry, (UINT64)Page);
  Log (L"CPU_ON returned %lx (0 = success, ...FFFC = already on, ...FFFE = invalid parameters)", Ret);
  gBS->Stall (1000 * 1000);
  CacheOp ((VOID *)(UINTN)Page, 4096, TRUE);

  Log (L"progress marker  = %lx  (100 = finished, 1/2/3 = stopped at that stage, 0 = never ran)", R[0]);
  Log (L"CurrentEL        = EL%u", (UINT32)((R[1] >> 2) & 3));
  Log (L"MPIDR_EL1        = %lx", R[2]);
  Log (L"DAIF             = %lx", R[3]);
  Log (L"CNTFRQ_EL0       = %lu", R[12]);
  Log (L"SCTLR_EL2        = %lx", R[4]);
  Log (L"HCR_EL2          = %lx", R[5]);
  Log (L"CNTHCTL_EL2      = %lx", R[6]);
  Log (L"CNTVOFF_EL2      = %lx", R[7]);
  Log (L"CPTR_EL2         = %lx", R[8]);
  Log (L"MDCR_EL2         = %lx", R[9]);
  Log (L"HSTR_EL2         = %lx", R[10]);
  Log (L"VBAR_EL2         = %lx", R[11]);
  Log (L"CNTV_CTL_EL0     = %lx", R[13]);
  Log (L"CNTP_CTL_EL0     = %lx", R[14]);
  Log (L"CNTHP_CTL_EL2    = %lx", R[15]);
  Log (L"ICC_SRE_EL2      = %lx", R[16]);

  Ret = Smc (PSCI_AFFINITY_INFO, 0x100, 0, 0);
  Log (L"CPU 0x100 after the test: %s", Ret == 0 ? L"ON" : Ret == 1 ? L"OFF" : L"other");

  // Interrupt controller, read-only.
  Log (L"GICD_CTLR        = %x", Mmio32 (GICD_BASE));
  for (I = 0; I < 2; I++) {
    V = GICR_BASE + I * GICR_STRIDE;
    Log (L"GICR[%u] TYPER=%lx WAKER=%x", (UINT32)I, Mmio64 (V + 0x08), Mmio32 (V + 0x14));
    Log (L"        IGROUPR0=%x ISENABLER0=%x IGRPMODR0=%x  (low 16 bits = SGI 0..15)",
         Mmio32 (V + 0x10000 + 0x80), Mmio32 (V + 0x10000 + 0x100), Mmio32 (V + 0x10000 + 0xD00));
  }

Done:
  Log (L"===== END =====");
  if (mLog != NULL) {
    mLog->Close (mLog);
  }
  return EFI_SUCCESS;
}
