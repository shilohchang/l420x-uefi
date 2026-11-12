/** KirinApPrep - clean the EL2 state of every secondary CPU before an OS boots.

  Finding (KirinApRaw): a secondary CPU switched on with PSCI CPU_ON arrives in
  EL2 with HCR_EL2 = 0x30080080007, i.e. with stage-2 translation (VM) and SMC
  trapping (TSC) already enabled and VBAR_EL2 pointing into the vendor's own
  EL2 component. The boot CPU has 0x88000002. An OS start-up path that does not
  rewrite HCR_EL2 completely then drops to EL1 on that CPU with stage-2
  translation on and no tables: the CPU is "online" but can do nothing.
  That matches Windows hanging as soon as a second CPU is enabled.

  What this program does, for each of the 11 secondary CPUs in turn:
    pass 1: CPU_ON -> stub records HCR_EL2, writes a clean value, CPU_OFF
    pass 2: CPU_ON again -> stub records what HCR_EL2 is now, CPU_OFF
  Pass 2 answers the key question: does the clean value SURVIVE a power cycle
  of the CPU? If yes, running this program before booting is the whole fix.

  Output: screen + \EFI\BOOT\logs\APPREP.TXT. Leaves every secondary OFF.
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
    if (!EFI_ERROR (Dir->Open (Dir, &mLog, L"APPREP.TXT", Rw | EFI_FILE_MODE_CREATE, 0))) {
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




extern VOID KirinApPrepEntry (VOID);
extern VOID KirinApPrepEnd (VOID);

STATIC CONST UINT64  mAp[11] = {
  0x100, 0x200, 0x300, 0x400, 0x401, 0x500, 0x501, 0x600, 0x601, 0x700, 0x701
};

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
  UINT64                Ret, State, V;
  UINTN                 Pass, I, K, Wait;
  UINTN                 Clean2 = 0, Answered2 = 0;

  OpenLog (ImageHandle);
  Log (L"===== KIRIN AP PREP =====");
  __asm__ __volatile__ ("mrs %0, hcr_el2" : "=r" (V));
  Log (L"boot CPU HCR_EL2 = %lx", V);

  S = gBS->AllocatePages (AllocateAnyPages, EfiBootServicesData, 1, &Page);
  if (EFI_ERROR (S)) {
    Log (L"AllocatePages: %r", S);
    goto Done;
  }
  R = (volatile UINT64 *)(UINTN)Page;
  CacheOp ((VOID *)(UINTN)KirinApPrepEntry, (UINTN)KirinApPrepEnd - (UINTN)KirinApPrepEntry, FALSE);

  for (Pass = 1; Pass <= 2; Pass++) {
    Log (L"--- pass %u (%s) ---", (UINT32)Pass,
         Pass == 1 ? L"record + clean" : L"did the clean value survive a power cycle?");
    for (I = 0; I < 11; I++) {
      for (K = 0; K < 8; K++) {
        R[K] = 0;
      }
      CacheOp ((VOID *)(UINTN)Page, 64, TRUE);     // zeros in RAM, nothing cached here

      Ret = Smc (PSCI_CPU_ON, mAp[I], (UINT64)(UINTN)KirinApPrepEntry, (UINT64)Page);
      // Wait until the CPU has switched itself off again (at most 2 s).
      State = 0;
      for (Wait = 0; Wait < 200; Wait++) {
        gBS->Stall (10 * 1000);
        State = Smc (PSCI_AFFINITY_INFO, mAp[I], 0, 0);
        if (State == 1 && Wait >= 2) {
          break;
        }
      }
      CacheOp ((VOID *)(UINTN)Page, 64, TRUE);

      if (Ret != 0 || R[0] != 0x100) {
        Log (L"CPU %03lx: CPU_ON=%lx marker=%lx state=%s  <== did not complete",
             mAp[I], Ret, R[0], State == 1 ? L"OFF" : State == 0 ? L"ON" : L"other");
        continue;
      }
      Log (L"CPU %03lx: EL%u HCR_EL2 arrived=%lx now=%lx VBAR_EL2=%lx state=%s",
           mAp[I], (UINT32)((R[1] >> 2) & 3), R[3], R[4], R[5],
           State == 1 ? L"OFF" : State == 0 ? L"ON" : L"other");
      if (Pass == 2) {
        Answered2++;
        if (R[3] == 0x80000000ULL) {
          Clean2++;
        }
      }
    }
  }

  Log (L"RESULT: %u of 11 secondaries answered in pass 2; %u arrived already clean.",
       (UINT32)Answered2, (UINT32)Clean2);
  if (Clean2 == 11) {
    Log (L"The clean state SURVIVES. Booting the OS in this same session should now see sane secondaries.");
  } else if (Clean2 == 0) {
    Log (L"The clean state does NOT survive: the secure firmware reloads HCR_EL2 at every CPU_ON.");
  } else {
    Log (L"Mixed result - send this log to Claude.");
  }

Done:
  Log (L"===== END =====");
  if (mLog != NULL) {
    mLog->Close (mLog);
  }
  return EFI_SUCCESS;
}
