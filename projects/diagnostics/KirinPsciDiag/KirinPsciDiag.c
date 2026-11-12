/** KirinPsciDiag - are the secondary CPUs already running inside the firmware?

  Read-only. Asks the secure firmware (PSCI, via SMC) for the power state of
  each of the 12 logical CPUs, and asks the UEFI firmware (MP Services) how
  many processors it is managing.

  Windows starts secondary CPUs with PSCI CPU_ON. If a CPU is already ON
  (parked by the UEFI firmware), CPU_ON is refused and Windows waits forever.

  Output: screen + \EFI\BOOT\logs\PSCIDIAG.TXT on the volume it was started from.
**/
#include <PiDxe.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/MpService.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PrintLib.h>

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
    if (!EFI_ERROR (Dir->Open (Dir, &mLog, L"PSCIDIAG.TXT", Rw | EFI_FILE_MODE_CREATE, 0))) {
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

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                 S;
  EFI_MP_SERVICES_PROTOCOL   *Mp = NULL;
  EFI_PROCESSOR_INFORMATION  Info;
  UINTN                      Total = 0, Enabled = 0, I;
  UINT64                     R;
  UINTN                      On = 0, Off = 0;

  OpenLog (ImageHandle);
  Log (L"===== KIRIN PSCI / MP DIAG =====");

  R = Smc (PSCI_VERSION, 0, 0);
  Log (L"PSCI_VERSION        : 0x%lx  (major %u, minor %u)", R, (UINT32)((R >> 16) & 0x7FFF), (UINT32)(R & 0xFFFF));
  R = Smc (PSCI_FEATURES, PSCI_CPU_ON, 0);
  Log (L"PSCI_FEATURES CPU_ON: 0x%lx  (0 = supported, negative = not supported)", R);

  for (I = 0; I < 12; I++) {
    R = Smc (PSCI_AFFINITY_INFO, mMpidr[I], 0);
    Log (L"CPU %2u MPIDR 0x%03lx : %s (0x%lx)", (UINT32)I, mMpidr[I],
         R == 0 ? L"ON" : R == 1 ? L"OFF" : R == 2 ? L"ON_PENDING" : L"error/unknown", R);
    if (R == 0) {
      On++;
    } else if (R == 1) {
      Off++;
    }
  }
  Log (L"PSCI summary: %u ON, %u OFF  (expected for an OS hand-off: 1 ON, 11 OFF)", (UINT32)On, (UINT32)Off);

  S = gBS->LocateProtocol (&gEfiMpServiceProtocolGuid, NULL, (VOID **)&Mp);
  Log (L"UEFI MP Services protocol: %r", S);
  if (!EFI_ERROR (S) && Mp != NULL) {
    S = Mp->GetNumberOfProcessors (Mp, &Total, &Enabled);
    Log (L"  processors known to firmware: %u, enabled: %u (%r)", (UINT32)Total, (UINT32)Enabled, S);
    for (I = 0; !EFI_ERROR (S) && I < Total && I < 16; I++) {
      if (!EFI_ERROR (Mp->GetProcessorInfo (Mp, I, &Info))) {
        Log (L"  #%u id=0x%lx flags=0x%x%s", (UINT32)I, Info.ProcessorId, Info.StatusFlag,
             (Info.StatusFlag & PROCESSOR_AS_BSP_BIT) != 0 ? L" (boot CPU)" : L"");
      }
    }
  }

  Log (L"===== END =====");
  if (mLog != NULL) {
    mLog->Close (mLog);
  }
  return EFI_SUCCESS;
}
