/** KirinApDiag - what state is each secondary CPU in when it is switched on?

  Uses the firmware's own MP Services to run a tiny function on every
  secondary CPU, one at a time, that only READS system registers:
    CurrentEL, MPIDR, MIDR, CNTFRQ_EL0 (timer frequency), counter value,
    and - when the CPU is in EL2 - CNTHCTL_EL2 / HCR_EL2.

  Why: with more than one CPU Windows hangs (bugcheck 0x1D5, ACPI, 360 s).
  PSCI itself is fine (KirinPsciDiag: 1 ON / 11 OFF, CPU_ON supported), so the
  secondaries start but do not work. The classic cause is a per-CPU register
  that the secure firmware programmes only on the boot CPU - above all
  CNTFRQ_EL0. The factory device tree carries an explicit timer
  clock-frequency override, which is what Linux needs in exactly that case.

  Nothing is written anywhere except the log file
  \EFI\BOOT\logs\APDIAG.TXT on the start volume.
**/
#include <PiDxe.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/MpService.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseMemoryLib.h>

#define PSCI_VERSION        0x84000000ULL
#define PSCI_AFFINITY_INFO  0xC4000004ULL
#define PSCI_FEATURES       0x8400000AULL
#define PSCI_CPU_ON         0xC4000003ULL

STATIC CONST UINT64  mMpidr[12] = {
  0x000, 0x100, 0x200, 0x300, 0x400, 0x401, 0x500, 0x501, 0x600, 0x601, 0x700, 0x701
};

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
    if (!EFI_ERROR (Dir->Open (Dir, &mLog, L"APDIAG.TXT", Rw | EFI_FILE_MODE_CREATE, 0))) {
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
  IN UINT64  Arg2
  )
{
  register UINT64  X0 __asm__ ("x0") = Function;
  register UINT64  X1 __asm__ ("x1") = Arg1;
  register UINT64  X2 __asm__ ("x2") = Arg2;
  register UINT64  X3 __asm__ ("x3") = 0;

  __asm__ __volatile__ (
    "smc #0"
    : "+r" (X0), "+r" (X1), "+r" (X2), "+r" (X3)
    :
    : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory"
    );
  return X0;
}


typedef struct {
  volatile UINT64  Done;
  UINT64           CurrentEl, Mpidr, Midr, Cntfrq, Count0, Count1, Cnthctl, Hcr, Pfr0;
} AP_REPORT;

STATIC AP_REPORT  mReport;

#define READ_SYSREG(Name, Var)  __asm__ __volatile__ ("mrs %0, " Name : "=r" (Var))

// Runs on the secondary CPU. Reads only; no library calls.
STATIC
VOID
EFIAPI
ApProbe (
  IN OUT VOID  *Context
  )
{
  AP_REPORT  *R = (AP_REPORT *)Context;
  UINT64     V;
  UINTN      I;

  READ_SYSREG ("CurrentEL", V);        R->CurrentEl = (V >> 2) & 3;
  READ_SYSREG ("mpidr_el1", V);        R->Mpidr     = V;
  READ_SYSREG ("midr_el1", V);         R->Midr      = V;
  READ_SYSREG ("cntfrq_el0", V);       R->Cntfrq    = V;
  READ_SYSREG ("id_aa64pfr0_el1", V);  R->Pfr0      = V;
  READ_SYSREG ("cntvct_el0", V);       R->Count0    = V;
  for (I = 0; I < 2000000; I++) {
    __asm__ __volatile__ ("nop");
  }
  READ_SYSREG ("cntvct_el0", V);       R->Count1    = V;
  if (R->CurrentEl == 2) {
    READ_SYSREG ("cnthctl_el2", V);    R->Cnthctl   = V;
    READ_SYSREG ("hcr_el2", V);        R->Hcr       = V;
  }
  R->Done = 1;
}

STATIC
VOID
LogReport (
  IN CONST CHAR16  *Who
  )
{
  Log (L"%s EL%u MPIDR=%lx MIDR=%lx", Who, (UINT32)mReport.CurrentEl, mReport.Mpidr, mReport.Midr);
  Log (L"     CNTFRQ_EL0=%lu Hz%s  counter advanced by %lu",
       mReport.Cntfrq, mReport.Cntfrq == 0 ? L"  <== ZERO" : L"", mReport.Count1 - mReport.Count0);
  Log (L"     PFR0=%lx CNTHCTL_EL2=%lx HCR_EL2=%lx", mReport.Pfr0, mReport.Cnthctl, mReport.Hcr);
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                S;
  EFI_MP_SERVICES_PROTOCOL  *Mp = NULL;
  UINTN                     Total = 0, Enabled = 0, I, Me = 0, On = 0;
  CHAR16                    Who[24];
  UINT64                    R;

  OpenLog (ImageHandle);
  Log (L"===== KIRIN AP DIAG =====");

  ZeroMem (&mReport, sizeof (mReport));
  ApProbe (&mReport);
  LogReport (L"BOOT CPU:");

  S = gBS->LocateProtocol (&gEfiMpServiceProtocolGuid, NULL, (VOID **)&Mp);
  if (EFI_ERROR (S) || Mp == NULL) {
    Log (L"MP Services not available: %r", S);
    goto Done;
  }
  Mp->GetNumberOfProcessors (Mp, &Total, &Enabled);
  Mp->WhoAmI (Mp, &Me);
  Log (L"MP Services: %u processors, %u enabled, this is #%u", (UINT32)Total, (UINT32)Enabled, (UINT32)Me);

  for (I = 0; I < Total && I < 16; I++) {
    if (I == Me) {
      continue;
    }
    ZeroMem (&mReport, sizeof (mReport));
    // Blocking call with a 2 second limit, so a CPU that never answers cannot hang the test.
    S = Mp->StartupThisAP (Mp, ApProbe, I, NULL, 2000000, &mReport, NULL);
    UnicodeSPrint (Who, sizeof (Who), L"CPU #%u:", (UINT32)I);
    if (EFI_ERROR (S) || mReport.Done == 0) {
      Log (L"%s did not answer (%r)", Who, S);
    } else {
      LogReport (Who);
    }
  }

  // Did the firmware leave the secondaries switched on afterwards?
  for (I = 0; I < 12; I++) {
    R = Smc (PSCI_AFFINITY_INFO, mMpidr[I], 0);
    if (R == 0) {
      On++;
    }
  }
  Log (L"After the test: %u CPU(s) ON according to PSCI (1 = firmware switched them off again)", (UINT32)On);

Done:
  Log (L"===== END =====");
  if (mLog != NULL) {
    mLog->Close (mLog);
  }
  return EFI_SUCCESS;
}
