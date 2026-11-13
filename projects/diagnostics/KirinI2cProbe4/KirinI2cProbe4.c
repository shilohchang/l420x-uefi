/** KirinI2cProbe4 - read the I2C controllers inside one uninterrupted window.

  Probe3 showed: the pages are mapped, the reset is released, but right after
  writing the clock-enable bit the status read back with bit 31 CLEAR. The
  firmware polls the built-in keyboard from a timer event and its own I2C
  driver switches the clock off again after every transfer. Between our
  "enable" and our "read" the program was logging to the USB stick, which lets
  firmware timer events run - so the clock was gone again before the read.

  This version raises the task priority to TPL_HIGH_LEVEL (no firmware events
  can run), then: reads the clock status, enables the clock, releases the
  reset, waits 200 us, reads the controller registers into a buffer, puts the
  clock back the way it was, restores the priority - and only then logs.

  Run 1: keyboard bus 0xFF88F000 (clock bit 31, reset bit 14)
  Run 2: touchpad bus 0xFF88E000 (clock bit 30, reset bit 19)
  A line is logged before each run, so a crash is attributable.

  Output: screen + \\EFI\\BOOT\\logs\\I2CPROBE4.TXT
**/
#include <Uefi.h>
#include <Protocol/LoadedImage.h>
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
    if (!EFI_ERROR (Dir->Open (Dir, &mLog, L"I2CPROBE4.TXT", Rw | EFI_FILE_MODE_CREATE, 0))) {
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

STATIC CONST CHAR16  *mName[24] = {
  L"FRAME_CTRL", L"SLV_ADDR", L"CMD_TXDATA", L"RXDATA", L"SS_SCL_HCNT", L"SS_SCL_LCNT",
  L"FS_SCL_HCNT", L"FS_SCL_LCNT", L"HS_SCL_HCNT", L"HS_SCL_LCNT", L"FIFO_CTRL", L"FIFO_STATE",
  L"SDA_HOLD", L"0x34", L"FS_SPK_LEN", L"HS_SPK_LEN", L"0x40", L"INT_MSTAT", L"INT_CLR(wo)",
  L"INT_MASK", L"TRANS_STATE", L"TRANS_ERR", L"VERSION", L"0x5C"
};

STATIC
VOID
Run (
  IN CONST CHAR16  *What,
  IN UINT64        Base,
  IN UINT32        ClkBit,
  IN UINT32        RstBit
  )
{
  STATIC UINT32  Reg[24];
  EFI_TPL        Old;
  UINT32         ClkBefore, ClkDuring, RstDuring, ClkAfter, I;

  Log (L"--- %s bus @ %lx: starting the uninterrupted window (crash after this line = this bus) ---", What, Base);

  Old = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  ClkBefore = Rd (CRG_BASE + 0x18);
  Wr (CRG_BASE + 0x10, ClkBit);                 // enable (write-1-to-set)
  Wr (CRG_BASE + 0x64, RstBit);                 // release reset
  gBS->Stall (200);
  ClkDuring = Rd (CRG_BASE + 0x18);
  RstDuring = Rd (CRG_BASE + 0x68);
  for (I = 0; I < 24; I++) {
    Reg[I] = 0xDEADDEAD;
  }
  if ((ClkDuring & ClkBit) != 0 && (RstDuring & RstBit) == 0) {
    for (I = 0; I < 24; I++) {
      if (I == 3 || I == 18) {                  // RXDATA pops the FIFO, INT_CLR is write-only
        continue;
      }
      Reg[I] = Rd (Base + I * 4);
    }
  }
  if ((ClkBefore & ClkBit) == 0) {
    Wr (CRG_BASE + 0x14, ClkBit);               // put the clock back as the firmware had it
  }
  ClkAfter = Rd (CRG_BASE + 0x18);
  gBS->RestoreTPL (Old);

  Log (L"clock status before/during/after: %08x / %08x / %08x (bit mask %08x)", ClkBefore, ClkDuring, ClkAfter, ClkBit);
  Log (L"reset status during: %08x (bit mask %08x)", RstDuring, RstBit);
  if ((ClkDuring & ClkBit) == 0 || (RstDuring & RstBit) != 0) {
    Log (L"clock did not come on or reset still asserted - controller NOT read");
    return;
  }
  for (I = 0; I < 24; I++) {
    if (I == 3 || I == 18) {
      continue;
    }
    Log (L"  %02x %-12s = %08x", I * 4, mName[I], Reg[I]);
  }
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  OpenLog (ImageHandle);
  Log (L"===== KIRIN I2C PROBE 4 =====");
  Run (L"keyboard", 0xFF88F000ULL, 1U << 31, 1U << 14);
  Run (L"touchpad", 0xFF88E000ULL, 1U << 30, 1U << 19);
  Log (L"===== END =====");
  if (mLog != NULL) {
    mLog->Close (mLog);
  }
  return EFI_SUCCESS;
}
