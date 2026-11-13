/** KirinI2cXfer2 - why the keyboard bus does not start, and a retry with the
    pins switched to their I2C function.

  KirinI2cXfer result: the touchpad bus transfers perfectly, the keyboard bus
  never starts. The controller's line-state bits (TRANS_STATE bits 5 and 6,
  the same bits the vendor Linux driver tests before every transfer) read 0 on
  the keyboard bus = the controller sees SCL and SDA LOW. Most likely the
  firmware parks the two pins (GPIO230/231) in a non-I2C function between its
  own transfers, just like it gates the clock.

  Steps (a line is logged before each one, so a crash is attributable):
    1  bus state + what GPIO bank 7 sees on pins 6/7
    2  read pin function registers  FB228794 / FB228798
    3  read pin pad registers       FB229794 / FB229798
    4  read pin electrical config   FB229F94 / FB229F98
    5  set function 4 (I2C) + pull-up where they differ, re-check the bus,
       read the keyboard HID descriptor, then put every changed register back
    6  reference only: the same three register pairs for the (working)
       touchpad pins - read only

  Output: screen + \\EFI\\BOOT\\logs\\I2CXFER2.TXT
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
    if (!EFI_ERROR (Dir->Open (Dir, &mLog, L"I2CXFER2.TXT", Rw | EFI_FILE_MODE_CREATE, 0))) {
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

#define CRG_BASE      0xFFB85000ULL
#define KBD_BASE      0xFF88F000ULL
#define KBD_CLK       0x80000000U
#define GPIO7_BASE    0xFB232000ULL
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
#define DESC_LEN      30U
#define RX_CAP        40U
#define TRACE_CAP     32U

typedef struct {
  UINT32  Iter, Mstat, Fifo, Tstate, Rx;
} TRACE;

STATIC UINT8   mData[RX_CAP];
STATIC TRACE   mTr[TRACE_CAP];
STATIC UINT32  mPushed, mRx, mNTr, mIter, mEnd, mErr;

STATIC
VOID
ClearFifos (
  IN UINT64  Base
  )
{
  UINT32  V = Rd (Base + R_FIFO_CTRL);

  Wr (Base + R_FIFO_CTRL, V | 3U);
  Wr (Base + R_FIFO_CTRL, V & ~3U);
}

// Caller holds TPL_HIGH_LEVEL and has the clock on.
STATIC
VOID
Xfer (
  IN UINT64  Base,
  IN UINT32  Slave,
  IN UINT32  DescReg
  )
{
  UINT32  I, Cmd, M, F, T, Grace = 0, OldAddr, OldMask;

  mPushed = mRx = mNTr = mIter = mEnd = mErr = 0;
  OldAddr = Rd (Base + R_SLV_ADDR);
  OldMask = Rd (Base + R_INT_MASK);
  ClearFifos (Base);
  Wr (Base + R_INT_CLR, 0x1F);
  Wr (Base + R_INT_MASK, 0x1F);
  Wr (Base + R_SLV_ADDR, Slave);
  for (I = 0; I < 2 + DESC_LEN; I++) {
    if ((Rd (Base + R_FIFO_STATE) & FIFO_TX_FULL) != 0) {
      break;
    }
    if (I == 0) {
      Cmd = DescReg & 0xFF;
    } else if (I == 1) {
      Cmd = (DescReg >> 8) & 0xFF;
    } else {
      Cmd = CMD_RW;
      if (I == 2) {
        Cmd |= CMD_RESTART;
      }
      if (I == 2 + DESC_LEN - 1) {
        Cmd |= CMD_STOP;
      }
    }
    Wr (Base + R_CMD_TXDATA, Cmd);
    mPushed++;
  }
  for (mIter = 0; mIter < 2000; mIter++) {
    while (mRx < RX_CAP && (Rd (Base + R_FIFO_STATE) & FIFO_RX_EMPTY) == 0) {
      mData[mRx++] = (UINT8)Rd (Base + R_RXDATA);
    }
    M = Rd (Base + R_INT_MSTAT);
    F = Rd (Base + R_FIFO_STATE);
    T = Rd (Base + R_TRANS_STATE);
    if (mNTr < TRACE_CAP &&
        (mNTr == 0 || mTr[mNTr - 1].Mstat != M || mTr[mNTr - 1].Fifo != F || mTr[mNTr - 1].Tstate != T)) {
      mTr[mNTr].Iter = mIter; mTr[mNTr].Mstat = M; mTr[mNTr].Fifo = F; mTr[mNTr].Tstate = T; mTr[mNTr].Rx = mRx;
      mNTr++;
    }
    if ((M & 0x6) != 0) {
      mEnd = 2;
      break;
    }
    if (mRx >= DESC_LEN) {
      if ((M & 0x1) != 0) {
        mEnd = 1;
        break;
      }
      if (++Grace > 40) {
        mEnd = 3;
        break;
      }
    }
    gBS->Stall (50);
  }
  mErr = Rd (Base + R_TRANS_ERR);
  Wr (Base + R_INT_MASK, OldMask);
  Wr (Base + R_INT_CLR, 0x1F);
  ClearFifos (Base);
  Wr (Base + R_SLV_ADDR, OldAddr);
}

STATIC
VOID
LogXfer (
  VOID
  )
{
  STATIC CHAR16  Hex[RX_CAP * 3 + 4];
  UINT32         I;

  Log (L"commands queued: %u of %u", mPushed, 2 + DESC_LEN);
  for (I = 0; I < mNTr; I++) {
    Log (L"  t=%4u  mstat=%08x fifo=%08x tstate=%08x rx=%u", mTr[I].Iter, mTr[I].Mstat, mTr[I].Fifo, mTr[I].Tstate, mTr[I].Rx);
  }
  Log (L"ended at t=%u: %s   TRANS_ERR=%08x", mIter,
       mEnd == 1 ? L"COMPLETE flag seen" :
       mEnd == 2 ? L"ERROR flag seen" :
       mEnd == 3 ? L"all bytes received, but no COMPLETE flag" : L"TIMEOUT", mErr);
  Log (L"bytes received: %u", mRx);
  for (I = 0; I < mRx; I++) {
    UnicodeSPrint (Hex + I * 3, 4 * sizeof (CHAR16), L"%02x ", mData[I]);
  }
  if (mRx != 0) {
    Log (L"  %s", Hex);
  }
  if (mRx >= DESC_LEN && mData[0] == 0x1E && mData[1] == 0x00) {
    Log (L"RESULT: HID DESCRIPTOR OK  vendor:product = %02x%02x:%02x%02x  report-descriptor %u bytes, max input %u bytes",
         mData[21], mData[20], mData[23], mData[22],
         (UINT32)(mData[4] | (mData[5] << 8)), (UINT32)(mData[10] | (mData[11] << 8)));
  } else {
    Log (L"RESULT: no valid HID descriptor");
  }
}

// Read two registers inside one short uninterrupted window.
STATIC
VOID
ReadPair (
  IN CONST CHAR16  *Step,
  IN CONST CHAR16  *What,
  IN UINT64        A0,
  IN UINT64        A1,
  OUT UINT32       *V
  )
{
  EFI_TPL  Old;

  Log (L"step %s: reading %s %lx / %lx (crash after this line = this step)", Step, What, A0, A1);
  Old = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  V[0] = Rd (A0);
  V[1] = Rd (A1);
  gBS->RestoreTPL (Old);
  Log (L"  = %08x / %08x", V[0], V[1]);
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  STATIC CONST UINT64  MuxA[2] = { 0xFB228794ULL, 0xFB228798ULL };
  STATIC CONST UINT64  PadA[2] = { 0xFB229794ULL, 0xFB229798ULL };
  STATIC CONST UINT64  CfgA[2] = { 0xFB229F94ULL, 0xFB229F98ULL };
  EFI_TPL  Old;
  UINT32   Clk, T0, Gin0, G6, G7, T1 = 0, Gin1 = 0, I;
  UINT32   Mux[2], Pad[2], Cfg[2], Mux2[2], Pad2[2], Cfg2[2], Ref[2];
  BOOLEAN  Tried = FALSE;

  OpenLog (ImageHandle);
  Log (L"===== KIRIN I2C XFER 2 =====");

  Log (L"step 1: keyboard bus state and GPIO bank 7 view of pins 6/7 (crash after this line = step 1)");
  Old = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  Clk = Rd (CRG_BASE + 0x18);
  Wr (CRG_BASE + 0x10, KBD_CLK);
  gBS->Stall (200);
  T0   = Rd (KBD_BASE + R_TRANS_STATE);
  Gin0 = Rd (GPIO7_BASE + 0xDC);
  G6   = Rd (GPIO7_BASE + 0x08 + 4 * 6);
  G7   = Rd (GPIO7_BASE + 0x08 + 4 * 7);
  if ((Clk & KBD_CLK) == 0) {
    Wr (CRG_BASE + 0x14, KBD_CLK);
  }
  gBS->RestoreTPL (Old);
  Log (L"  TRANS_STATE=%08x (bits 5,6 = lines high; 0x60 = bus idle)", T0);
  Log (L"  GPIO7 input=%08x -> pin6=%u pin7=%u   pin6 cfg=%08x pin7 cfg=%08x", Gin0, (Gin0 >> 6) & 1, (Gin0 >> 7) & 1, G6, G7);

  ReadPair (L"2", L"keyboard pin function", MuxA[0], MuxA[1], Mux);
  ReadPair (L"3", L"keyboard pin pad", PadA[0], PadA[1], Pad);
  ReadPair (L"4", L"keyboard pin electrical config", CfgA[0], CfgA[1], Cfg);

  Log (L"step 5: set I2C function (4) + pull-up where different, re-check, transfer, restore (crash after this line = step 5)");
  Old = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  Clk = Rd (CRG_BASE + 0x18);
  Wr (CRG_BASE + 0x10, KBD_CLK);
  for (I = 0; I < 2; I++) {
    if ((Cfg[I] & 3U) != 1U) {
      Wr (CfgA[I], (Cfg[I] & ~3U) | 1U);          // pull-up on, pull-down off
    }
    if (Pad[I] != 4U) {
      Wr (PadA[I], 4U);
    }
    if (Mux[I] != 4U) {
      Wr (MuxA[I], 4U);
    }
  }
  gBS->Stall (500);
  for (I = 0; I < 2; I++) {
    Mux2[I] = Rd (MuxA[I]);
    Pad2[I] = Rd (PadA[I]);
    Cfg2[I] = Rd (CfgA[I]);
  }
  T1   = Rd (KBD_BASE + R_TRANS_STATE);
  Gin1 = Rd (GPIO7_BASE + 0xDC);
  if ((T1 & 0x60U) == 0x60U) {
    Tried = TRUE;
    Xfer (KBD_BASE, 0x3A, 0x0001);
  }
  for (I = 0; I < 2; I++) {                        // put back exactly what was there
    if (Mux2[I] != Mux[I]) {
      Wr (MuxA[I], Mux[I]);
    }
    if (Pad2[I] != Pad[I]) {
      Wr (PadA[I], Pad[I]);
    }
    if (Cfg2[I] != Cfg[I]) {
      Wr (CfgA[I], Cfg[I]);
    }
  }
  if ((Clk & KBD_CLK) == 0) {
    Wr (CRG_BASE + 0x14, KBD_CLK);
  }
  gBS->RestoreTPL (Old);
  Log (L"  after the change: function %08x / %08x  pad %08x / %08x  config %08x / %08x",
       Mux2[0], Mux2[1], Pad2[0], Pad2[1], Cfg2[0], Cfg2[1]);
  Log (L"  TRANS_STATE=%08x   GPIO7 input=%08x -> pin6=%u pin7=%u", T1, Gin1, (Gin1 >> 6) & 1, (Gin1 >> 7) & 1);
  if (Tried) {
    Log (L"  bus is idle now - transfer attempted:");
    LogXfer ();
  } else {
    Log (L"RESULT: lines still not high - no transfer attempted");
  }

  ReadPair (L"6a", L"touchpad pin function (reference)", 0xFF810010ULL, 0xFF810014ULL, Ref);
  ReadPair (L"6b", L"touchpad pin pad (reference)", 0xFF811010ULL, 0xFF811014ULL, Ref);
  ReadPair (L"6c", L"touchpad pin electrical config (reference)", 0xFF811810ULL, 0xFF811814ULL, Ref);

  Log (L"===== END =====");
  if (mLog != NULL) {
    mLog->Close (mLog);
  }
  return EFI_SUCCESS;
}
