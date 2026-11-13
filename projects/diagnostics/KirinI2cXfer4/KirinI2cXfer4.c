/** KirinI2cXfer4 - which command upsets the touchpad's report descriptor, and
    what puts it right again.

  KirinI2cXfer3 result: before the HID "reset" command the touchpad returns a
  correct 501-byte report descriptor (byte-sum 28533). After "power on + reset"
  bytes 2..29 of it are replaced by bytes of the HID descriptor (sum 27975),
  no matter how long one waits, and the interrupt line never signalled the
  reset. Windows always sends power-on and reset before reading the report
  descriptor, so it always gets the damaged one.

  After every step below the report descriptor is read again and marked
  GOOD / BAD:
    A  nothing (state as left by the firmware)
    B  power-on command only
    C  sleep command, then power-on command
    D  reset command; watch the interrupt line for 6 s
    E  read the full-size (37-byte) input report
    F  read the HID descriptor again
    G  power-on command
    H  sleep, 50 ms, power-on, 50 ms
    I  wait 3 s

  Touchpad bus only; no clock or pin register is touched.
  Output: screen + I2CXFER4.TXT
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
    if (!EFI_ERROR (Dir->Open (Dir, &mLog, L"I2CXFER4.TXT", Rw | EFI_FILE_MODE_CREATE, 0))) {
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

#define BUS           0xFF88E000ULL
#define GPIO7_INPUT   0xFB2320DCULL
#define TP_ADDR       0x2C
#define R_SLV_ADDR    0x04
#define R_CMD_TXDATA  0x08
#define R_RXDATA      0x0C
#define R_FIFO_CTRL   0x28
#define R_FIFO_STATE  0x2C
#define R_INT_MSTAT   0x44
#define R_INT_CLR     0x48
#define R_INT_MASK    0x4C
#define R_TRANS_STATE 0x50
#define R_TRANS_ERR   0x54
#define CMD_RW        0x100U
#define CMD_STOP      0x200U
#define CMD_RESTART   0x400U
#define FIFO_RX_EMPTY 0x008U
#define FIFO_TX_FULL  0x800U
#define RDESC_LEN     501U

STATIC UINT8   mBuf[600];
STATIC UINT32  mGot, mEnd, mErr, mIter;

STATIC
VOID
ClearFifos (
  VOID
  )
{
  UINT32  V = Rd (BUS + R_FIFO_CTRL);

  Wr (BUS + R_FIFO_CTRL, V | 3U);
  Wr (BUS + R_FIFO_CTRL, V & ~3U);
}

/* One transfer: WLen bytes written, optional pause (controller holds the bus
   because its command FIFO is empty and no STOP was requested), then RLen
   bytes read after a repeated start. STOP after the last byte.
   mEnd: 1 = complete, 2 = error flag, 0 = timeout. */
STATIC
VOID
Xfer (
  IN CONST UINT8  *W,
  IN UINT32       WLen,
  IN UINT32       RLen,
  IN UINT32       GapUs
  )
{
  UINT32   Total = WLen + RLen, Pushed = 0, Outstanding = 0, Cmd, M;
  BOOLEAN  GapDone = (BOOLEAN)(GapUs == 0 || WLen == 0 || RLen == 0);

  mGot = mEnd = mErr = 0;
  ClearFifos ();
  Wr (BUS + R_INT_CLR, 0x1F);
  Wr (BUS + R_INT_MASK, 0x1F);
  Wr (BUS + R_SLV_ADDR, TP_ADDR);

  for (mIter = 0; mIter < 20000; mIter++) {             // 20000 x 25 us = 0.5 s
    while (mGot < sizeof (mBuf) && (Rd (BUS + R_FIFO_STATE) & FIFO_RX_EMPTY) == 0) {
      mBuf[mGot++] = (UINT8)Rd (BUS + R_RXDATA);
      if (Outstanding != 0) {
        Outstanding--;
      }
    }
    while (Pushed < Total && (Rd (BUS + R_FIFO_STATE) & FIFO_TX_FULL) == 0) {
      if (Pushed >= WLen) {
        if (!GapDone) {
          break;                                        // write phase queued; pause handled below
        }
        if (Outstanding >= 32) {
          break;
        }
        Cmd = CMD_RW;
        if (Pushed == WLen && WLen != 0) {
          Cmd |= CMD_RESTART;
        }
        Outstanding++;
      } else {
        Cmd = W[Pushed];
      }
      if (Pushed == Total - 1) {
        Cmd |= CMD_STOP;
      }
      Wr (BUS + R_CMD_TXDATA, Cmd);
      Pushed++;
    }
    if (!GapDone && Pushed == WLen && (Rd (BUS + R_FIFO_STATE) & 0x200U) != 0) {
      gBS->Stall (1000);                                // let the last written byte leave the shifter
      gBS->Stall (GapUs);
      GapDone = TRUE;
      continue;
    }
    M = Rd (BUS + R_INT_MSTAT);
    if ((M & 0x6) != 0) {
      mEnd = 2;
      break;
    }
    if ((M & 0x1) != 0 && Pushed == Total) {
      while (mGot < sizeof (mBuf) && (Rd (BUS + R_FIFO_STATE) & FIFO_RX_EMPTY) == 0) {
        mBuf[mGot++] = (UINT8)Rd (BUS + R_RXDATA);
      }
      mEnd = 1;
      break;
    }
    gBS->Stall (25);
  }
  mErr = Rd (BUS + R_TRANS_ERR);
  Wr (BUS + R_INT_MASK, 0);
  Wr (BUS + R_INT_CLR, 0x1F);
  if (mEnd != 1) {
    ClearFifos ();
  }
}

STATIC CONST UINT8  mRegHid[2]   = { 0x20, 0x00 };
STATIC CONST UINT8  mRegRdesc[2] = { 0x21, 0x00 };
STATIC CONST UINT8  mPowerOn[4]  = { 0x22, 0x00, 0x00, 0x08 };
STATIC CONST UINT8  mSleep[4]    = { 0x22, 0x00, 0x01, 0x08 };
STATIC CONST UINT8  mReset[4]    = { 0x22, 0x00, 0x00, 0x01 };

STATIC
VOID
Check (
  IN CONST CHAR16  *After
  )
{
  UINT32  I, Sum = 0;

  Xfer (mRegRdesc, 2, RDESC_LEN, 0);
  for (I = 0; I < mGot; I++) {
    Sum += mBuf[I];
  }
  Log (L"%s -> report descriptor %s  (%s, %u bytes, sum %u, starts %02x %02x %02x %02x %02x %02x %02x %02x, irq line %u)",
       After, (mEnd == 1 && mGot == RDESC_LEN && Sum == 28533) ? L"GOOD" : L"BAD ",
       mEnd == 1 ? L"complete" : mEnd == 2 ? L"ERROR flag" : L"TIMEOUT", mGot, Sum,
       mBuf[0], mBuf[1], mBuf[2], mBuf[3], mBuf[4], mBuf[5], mBuf[6], mBuf[7],
       (Rd (GPIO7_INPUT) >> 4) & 1);
}

STATIC
VOID
Cmd (
  IN CONST CHAR16  *Name,
  IN CONST UINT8   *Bytes
  )
{
  Xfer (Bytes, 4, 0, 0);
  if (mEnd != 1) {
    Log (L"  (%s command did not complete: end=%u TRANS_ERR=%08x)", Name, mEnd, mErr);
  }
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT32  OldAddr, OldMask, Ms;

  OpenLog (ImageHandle);
  Log (L"===== KIRIN I2C XFER 4 (touchpad report descriptor) =====");
  OldAddr = Rd (BUS + R_SLV_ADDR);
  OldMask = Rd (BUS + R_INT_MASK);

  Check (L"A nothing");

  Cmd (L"power-on", mPowerOn);
  Check (L"B power-on");

  Cmd (L"sleep", mSleep);
  Cmd (L"power-on", mPowerOn);
  Check (L"C sleep + power-on");

  Cmd (L"reset", mReset);
  for (Ms = 0; Ms < 6000 && ((Rd (GPIO7_INPUT) >> 4) & 1) != 0; Ms++) {
    gBS->Stall (1000);
  }
  Log (L"D reset: interrupt line %s (%u ms)", Ms >= 6000 ? L"never went low" : L"went low after", Ms);
  Check (L"D reset");

  Xfer (NULL, 0, 37, 0);
  Log (L"E input report read: %s, %u bytes, starts %02x %02x %02x %02x %02x %02x %02x %02x",
       mEnd == 1 ? L"complete" : L"FAILED", mGot,
       mBuf[0], mBuf[1], mBuf[2], mBuf[3], mBuf[4], mBuf[5], mBuf[6], mBuf[7]);
  Check (L"E 37-byte input read");

  Xfer (mRegHid, 2, 30, 0);
  Log (L"F HID descriptor read: %s, starts %02x %02x %02x %02x %02x %02x %02x %02x",
       mEnd == 1 ? L"complete" : L"FAILED",
       mBuf[0], mBuf[1], mBuf[2], mBuf[3], mBuf[4], mBuf[5], mBuf[6], mBuf[7]);
  Check (L"F HID descriptor read");

  Cmd (L"power-on", mPowerOn);
  Check (L"G power-on");

  Cmd (L"sleep", mSleep);
  gBS->Stall (50000);
  Cmd (L"power-on", mPowerOn);
  gBS->Stall (50000);
  Check (L"H sleep / 50 ms / power-on / 50 ms");

  gBS->Stall (3000000);
  Check (L"I 3 s later");

  Wr (BUS + R_SLV_ADDR, OldAddr);
  Wr (BUS + R_INT_MASK, OldMask);
  Log (L"===== END =====");
  if (mLog != NULL) {
    mLog->Close (mLog);
  }
  return EFI_SUCCESS;
}
