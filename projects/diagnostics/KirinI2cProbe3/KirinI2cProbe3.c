/** KirinI2cProbe3 - why does reading the I2C controller crash under UEFI?

  Probe2 switched the clock on and released the reset (both writes went
  through) and still crashed on the very first read of 0xFF88F000.
  Two different causes look identical from outside:
    (a) the firmware never mapped that address in its page tables
        (a plain translation fault - nothing to do with the hardware), or
    (b) the address is mapped and the hardware itself rejects the access.

  This program first walks the firmware's EL2 page tables IN SOFTWARE (pure
  memory reads) for four addresses and logs what it finds:
      0xFB232000 GPIO (known readable)   0xFFB85000 clock/reset block
      0xFF88F000 keyboard I2C            0xFF88E000 touchpad I2C
  If the keyboard I2C page is not mapped it asks the firmware to map it as
  device memory, checks again, and only then repeats the Probe2 sequence.
  Every step is logged before it is executed.

  Output: screen + \\EFI\\BOOT\\logs\\I2CPROBE3.TXT
**/
#include <Uefi.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/Cpu.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseLib.h>



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
    if (!EFI_ERROR (Dir->Open (Dir, &mLog, L"I2CPROBE3.TXT", Rw | EFI_FILE_MODE_CREATE, 0))) {
      mLog->SetPosition (mLog, MAX_UINT64);
    } else {
      mLog = NULL;
    }
    Dir->Close (Dir);
  }
  Root->Close (Root);
}




STATIC UINT32 Rd (UINT64 A) { return *(volatile UINT32 *)(UINTN)A; }
STATIC VOID   Wr (UINT64 A, UINT32 V) { *(volatile UINT32 *)(UINTN)A = V; }

#define CRG_BASE  0xFFB85000ULL
#define I2C_KBD   0xFF88F000ULL

// Software walk of the EL2 stage-1 tables (4 KB granule). Returns TRUE if mapped.
STATIC
BOOLEAN
Walk (
  IN CONST CHAR16  *What,
  IN UINT64        Va
  )
{
  UINT64  Tcr, Ttbr, *Table, Desc;
  UINTN   T0sz, Level, Shift, Index;

  __asm__ __volatile__ ("mrs %0, tcr_el2" : "=r" (Tcr));
  __asm__ __volatile__ ("mrs %0, ttbr0_el2" : "=r" (Ttbr));
  if (((Tcr >> 14) & 3) != 0) {
    Log (L"%s %lx: granule is not 4 KB (TCR_EL2=%lx) - walk skipped", What, Va, Tcr);
    return TRUE;
  }
  T0sz  = (UINTN)(Tcr & 0x3F);
  Level = (T0sz < 25) ? 0 : (T0sz < 34) ? 1 : 2;
  Table = (UINT64 *)(UINTN)(Ttbr & 0x0000FFFFFFFFF000ULL);
  for ( ; Level <= 3; Level++) {
    Shift = 12 + 9 * (3 - Level);
    Index = (UINTN)(Va >> Shift) & 0x1FF;
    Desc  = Table[Index];
    if ((Desc & 1) == 0) {
      Log (L"%s %lx: NOT MAPPED (invalid entry at level %u, descriptor %lx)", What, Va, (UINT32)Level, Desc);
      return FALSE;
    }
    if (Level == 3 || (Desc & 3) == 1) {
      Log (L"%s %lx: mapped at level %u, descriptor %lx (attr index %u, AP %u)",
           What, Va, (UINT32)Level, Desc, (UINT32)((Desc >> 2) & 7), (UINT32)((Desc >> 6) & 3));
      return TRUE;
    }
    Table = (UINT64 *)(UINTN)(Desc & 0x0000FFFFFFFFF000ULL);
  }
  return FALSE;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_CPU_ARCH_PROTOCOL  *Cpu = NULL;
  EFI_STATUS             S;
  BOOLEAN                Mapped;
  UINT64                 V;
  UINT32                 Off;

  OpenLog (ImageHandle);
  Log (L"===== KIRIN I2C PROBE 3 =====");
  __asm__ __volatile__ ("mrs %0, tcr_el2" : "=r" (V));   Log (L"TCR_EL2   = %lx", V);
  __asm__ __volatile__ ("mrs %0, ttbr0_el2" : "=r" (V)); Log (L"TTBR0_EL2 = %lx", V);

  Walk (L"GPIO     ", 0xFB232000ULL);
  Walk (L"CRG      ", CRG_BASE);
  Walk (L"I2C tpad ", 0xFF88E000ULL);
  Mapped = Walk (L"I2C kbd  ", I2C_KBD);

  if (!Mapped) {
    Log (L"keyboard I2C page is not mapped: asking the firmware to map it as device memory");
    S = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&Cpu);
    if (!EFI_ERROR (S) && Cpu != NULL) {
      S = Cpu->SetMemoryAttributes (Cpu, I2C_KBD, 0x1000, EFI_MEMORY_UC);
    }
    Log (L"SetMemoryAttributes: %r", S);
    Mapped = Walk (L"I2C kbd  ", I2C_KBD);
    if (!Mapped) {
      Log (L"still not mapped - stopping here (no hardware access attempted)");
      goto Done;
    }
  }

  Log (L"clock enable: write 80000000 to %lx", CRG_BASE + 0x10);
  Wr (CRG_BASE + 0x10, 0x80000000U);
  gBS->Stall (2000);
  Log (L"reset release: write 00004000 to %lx", CRG_BASE + 0x64);
  Wr (CRG_BASE + 0x64, 0x00004000U);
  gBS->Stall (2000);

  Log (L"about to read clock status  %lx", CRG_BASE + 0x18);
  Log (L"  PERCLKEN1 = %08x  (bit 31 = i2c7 clock)", Rd (CRG_BASE + 0x18));
  Log (L"about to read reset status  %lx", CRG_BASE + 0x68);
  Log (L"  PERRSTSTAT0 = %08x  (bit 14 = i2c7 held in reset)", Rd (CRG_BASE + 0x68));

  for (Off = 0; Off < 0x60; Off += 4) {
    if (Off == 0x0C || Off == 0x48) {
      continue;
    }
    Log (L"  i2c %02x about to read", Off);
    Log (L"  i2c %02x = %08x", Off, Rd (I2C_KBD + Off));
  }
  Log (L"all reads completed");

Done:
  Log (L"===== END =====");
  if (mLog != NULL) {
    mLog->Close (mLog);
  }
  return EFI_SUCCESS;
}
