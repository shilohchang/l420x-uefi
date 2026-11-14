/** KirinI2cXfer5 - does the touchpad's data damage depend on bus timing?

  In Windows the touchpad now starts, but its input reports show the same
  damage as the report descriptor did: the first 2-3 bytes are fresh, bytes up
  to offset 29 repeat the HID descriptor, the rest is correct. That looks like
  a race inside the touchpad - it refills a 30-byte transmit buffer while the
  host is already clocking data out - which a slower bus, or a host that
  honours clock stretching, would not lose.

  The firmware never uses this bus, so its timing registers still hold reset
  defaults (about 116 kHz, SDA hold = 1). The factory Linux programs its own
  values. This program tries several timings. For each one it sends a
  power-on command and reads the 501-byte report descriptor:
  GOOD = byte-sum 28533, BAD = damaged.

  The original register values are restored at the end.
  Output: screen + I2CXFER5.TXT
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
    if (!EFI_ERROR (Dir->Open (Dir, &mLog, L"I2CXFER5.TXT", Rw | EFI_FILE_MODE_CREATE, 0))) {
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

  for (mIter = 0; mIter < 80000; mIter++) {             // 80000 x 25 us = 2 s at least
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

STATIC CONST UINT8  mRegRdesc[2] = { 0x21, 0x00 };
STATIC CONST UINT8  mPowerOn[4]  = { 0x22, 0x00, 0x00, 0x08 };

#define R_FRAME_CTRL   0x00
#define R_SS_SCL_HCNT  0x10
#define R_SS_SCL_LCNT  0x14
#define R_FS_SCL_HCNT  0x18
#define R_FS_SCL_LCNT  0x1C
#define R_SDA_HOLD     0x30

STATIC UINT32  mFrame0, mSsH0, mSsL0, mFsH0, mFsL0, mHold0;

STATIC
VOID
Try (
  IN CONST CHAR16  *Name,
  IN UINT32        Fast,       // 0 = standard-mode counters, 1 = fast-mode counters
  IN UINT32        High,
  IN UINT32        Low,
  IN UINT32        Hold
  )
{
  UINT32  I, Sum = 0, CmdEnd;

  Wr (BUS + R_FRAME_CTRL, (mFrame0 & ~3U) | Fast);
  if (Fast != 0) {
    Wr (BUS + R_FS_SCL_HCNT, High);
    Wr (BUS + R_FS_SCL_LCNT, Low);
  } else {
    Wr (BUS + R_SS_SCL_HCNT, High);
    Wr (BUS + R_SS_SCL_LCNT, Low);
  }
  Wr (BUS + R_SDA_HOLD, Hold);
  gBS->Stall (1000);

  Xfer (mPowerOn, 4, 0, 0);
  CmdEnd = mEnd;
  Xfer (mRegRdesc, 2, RDESC_LEN, 0);
  for (I = 0; I < mGot; I++) {
    Sum += mBuf[I];
  }
  Log (L"%s [mode %u, high %u, low %u, hold %08x] -> %s  (cmd %s, read %s, %u bytes, sum %u, starts %02x %02x %02x %02x %02x %02x)",
       Name, Fast, High, Low, Hold,
       (mEnd == 1 && mGot == RDESC_LEN && Sum == 28533) ? L"GOOD" : L"BAD ",
       CmdEnd == 1 ? L"ok" : L"FAILED",
       mEnd == 1 ? L"complete" : mEnd == 2 ? L"ERROR flag" : L"TIMEOUT", mGot, Sum,
       mBuf[0], mBuf[1], mBuf[2], mBuf[3], mBuf[4], mBuf[5]);
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT32  OldAddr, OldMask;

  OpenLog (ImageHandle);
  Log (L"===== KIRIN I2C XFER 5 (touchpad vs bus timing) =====");
  OldAddr = Rd (BUS + R_SLV_ADDR);
  OldMask = Rd (BUS + R_INT_MASK);
  mFrame0 = Rd (BUS + R_FRAME_CTRL);
  mSsH0   = Rd (BUS + R_SS_SCL_HCNT);
  mSsL0   = Rd (BUS + R_SS_SCL_LCNT);
  mFsH0   = Rd (BUS + R_FS_SCL_HCNT);
  mFsL0   = Rd (BUS + R_FS_SCL_LCNT);
  mHold0  = Rd (BUS + R_SDA_HOLD);
  Log (L"as found: FRAME_CTRL=%08x SS=%u/%u FS=%u/%u SDA_HOLD=%08x", mFrame0, mSsH0, mSsL0, mFsH0, mFsL0, mHold0);

  Try (L"1 as found            ", 0, mSsH0, mSsL0, mHold0);
  Try (L"2 Linux SDA hold      ", 0, mSsH0, mSsL0, 0x00430043);
  Try (L"3 half speed          ", 0, mSsH0 * 2, mSsL0 * 2, mHold0);
  Try (L"4 quarter speed       ", 0, mSsH0 * 4, mSsL0 * 4, mHold0);
  Try (L"5 1/8 speed           ", 0, mSsH0 * 8, mSsL0 * 8, mHold0);
  Try (L"6 1/8 speed + hold    ", 0, mSsH0 * 8, mSsL0 * 8, 0x00430043);
  Try (L"7 fast mode (kbd bus) ", 1, 0x50, 0xC0, mHold0);
  Try (L"8 fast mode + hold    ", 1, 0x50, 0xC0, 0x00430043);
  Try (L"9 as found again      ", 0, mSsH0, mSsL0, mHold0);

  Wr (BUS + R_FRAME_CTRL, mFrame0);
  Wr (BUS + R_SS_SCL_HCNT, mSsH0);
  Wr (BUS + R_SS_SCL_LCNT, mSsL0);
  Wr (BUS + R_FS_SCL_HCNT, mFsH0);
  Wr (BUS + R_FS_SCL_LCNT, mFsL0);
  Wr (BUS + R_SDA_HOLD, mHold0);
  Wr (BUS + R_SLV_ADDR, OldAddr);
  Wr (BUS + R_INT_MASK, OldMask);
  Log (L"===== END =====");
  if (mLog != NULL) {
    mLog->Close (mLog);
  }
  return EFI_SUCCESS;
}
