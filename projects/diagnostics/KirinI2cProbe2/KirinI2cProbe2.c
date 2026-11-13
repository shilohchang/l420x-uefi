/** KirinI2cProbe2 - switch the keyboard's I2C controller on and read it.

  From the factory kernel (table crgctrl_gate_clks, driver hs_i2c_master):
    clock gate   clk_i2c7       : CRG 0xFFB85000, enable register 0x10, bit 31
    clock gate   clk_i2c6_acpu  : CRG 0xFFB85000, enable register 0x10, bit 30
    reset        i2c7 / i2c6    : assert 0x60, de-assert 0x64, status 0x68; bits 14 / 19
  The register layout of the controller matches mainline Linux
  drivers/i2c/busses/i2c-hisi.c (SCL counts at 0x10-0x24, FIFO_CTRL 0x28,
  SDA_HOLD 0x30, INT_MSTAT 0x44, INT_CLR 0x48, INT_MASK 0x4C, VERSION 0x58).

  Steps (each one is logged BEFORE it is done, so a crash shows where):
    1. write the clock-enable bit (write-1-to-set register)
    2. write the reset de-assert bit
    3. read the controller registers 0x00-0x5C

  Default: keyboard bus (0xFF88F000).  Argument /tp: touchpad bus (0xFF88E000).
  The CRG block itself is never read (reading it crashed Linux).

  Output: screen + \\EFI\\BOOT\\logs\\I2CPROBE2.TXT
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
    if (!EFI_ERROR (Dir->Open (Dir, &mLog, L"I2CPROBE2.TXT", Rw | EFI_FILE_MODE_CREATE, 0))) {
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

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  STATIC CONST CHAR16  *Name[] = {
    L"FRAME_CTRL", L"SLV_ADDR", L"CMD_TXDATA", L"RXDATA", L"SS_SCL_HCNT", L"SS_SCL_LCNT",
    L"FS_SCL_HCNT", L"FS_SCL_LCNT", L"HS_SCL_HCNT", L"HS_SCL_LCNT", L"FIFO_CTRL", L"FIFO_STATE",
    L"SDA_HOLD", L"0x34", L"FS_SPK_LEN", L"HS_SPK_LEN", L"0x40", L"INT_MSTAT", L"INT_CLR(wo)",
    L"INT_MASK", L"TRANS_STATE", L"TRANS_ERR", L"VERSION", L"0x5C"
  };
  EFI_LOADED_IMAGE_PROTOCOL  *Li = NULL;
  BOOLEAN                    Touchpad = FALSE;
  UINT64                     Base;
  UINT32                     ClkBit, RstBit, Off;

  if (!EFI_ERROR (gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Li)) &&
      Li != NULL && Li->LoadOptions != NULL && Li->LoadOptionsSize >= 4 * sizeof (CHAR16) &&
      ((CHAR16 *)Li->LoadOptions)[Li->LoadOptionsSize / sizeof (CHAR16) - 1] == 0) {
    Touchpad = (StrStr ((CHAR16 *)Li->LoadOptions, L"/tp") != NULL);
  }
  Base   = Touchpad ? 0xFF88E000ULL : 0xFF88F000ULL;
  ClkBit = Touchpad ? (1U << 30) : (1U << 31);
  RstBit = Touchpad ? (1U << 19) : (1U << 14);

  OpenLog (ImageHandle);
  Log (L"===== KIRIN I2C PROBE 2: %s bus @ %lx =====", Touchpad ? L"touchpad" : L"keyboard", Base);

  Log (L"step 1: clock enable  -> write %08x to %lx", ClkBit, CRG_BASE + 0x10);
  Wr (CRG_BASE + 0x10, ClkBit);
  gBS->Stall (2000);
  Log (L"step 1 done");

  Log (L"step 2: reset release -> write %08x to %lx", RstBit, CRG_BASE + 0x64);
  Wr (CRG_BASE + 0x64, RstBit);
  gBS->Stall (2000);
  Log (L"step 2 done");

  Log (L"step 3: reading controller registers");
  for (Off = 0; Off < 0x60; Off += 4) {
    if (Off == 0x0C || Off == 0x48) {       // RXDATA pops the FIFO, INT_CLR is write-only: leave alone
      Log (L"  %02x %-12s (skipped)", Off, Name[Off / 4]);
      continue;
    }
    Log (L"  %02x %-12s about to read", Off, Name[Off / 4]);
    Log (L"  %02x %-12s = %08x", Off, Name[Off / 4], Rd (Base + Off));
  }
  Log (L"===== END (all three steps completed without a crash) =====");
  if (mLog != NULL) {
    mLog->Close (mLog);
  }
  return EFI_SUCCESS;
}
