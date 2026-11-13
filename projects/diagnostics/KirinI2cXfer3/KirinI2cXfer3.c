/** KirinI2cXfer3 - touchpad start-up sequence, replayed from the UEFI shell.

  In Windows the touchpad (bus 0xFF88E000, device 0x2C) answers every I2C
  request without error, yet the HID driver refuses to start it. The driver's
  trace shows the 501-byte report descriptor coming back as "05 01" followed
  by bytes that belong to the HID descriptor read earlier - as if the touchpad
  had only prepared the first two bytes when the host started reading.

  This program replays the same sequence with variations, to find out whether
  the touchpad needs a pause between "select register" and "read":

    1  read HID descriptor (register 0x20)
    2  read report descriptor (register 0x21), no pause        - before reset
    3  power on + reset, wait for the interrupt line (GPIO7 pin 4) to go low,
       read the 2-byte reset acknowledge
    4  read report descriptor, no pause                        - what Windows does
    5  wait 100 ms, read report descriptor, no pause
    6  read report descriptor with a 2 ms pause between the register write
       and the read (bus held by the controller meanwhile)
    7  same as 6 but as two separate transfers (stop, 2 ms, read)

  The touchpad bus is not used by the firmware and its clock is always on, so
  no clock or pin register is touched. Output: screen + I2CXFER3.TXT
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
    if (!EFI_ERROR (Dir->Open (Dir, &mLog, L"I2CXFER3.TXT", Rw | EFI_FILE_MODE_CREATE, 0))) {
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

STATIC
VOID
Show (
  IN CONST CHAR16  *What,
  IN UINT32        Expect
  )
{
  STATIC CHAR16  Hex[16 * 3 + 4];
  UINT32         I, J, Sum = 0, N;

  for (I = 0; I < mGot; I++) {
    Sum += mBuf[I];
  }
  Log (L"%s: %s, %u of %u bytes, byte-sum %u, TRANS_ERR=%08x", What,
       mEnd == 1 ? L"complete" : mEnd == 2 ? L"ERROR flag" : L"TIMEOUT", mGot, Expect, Sum, mErr);
  N = mGot < 48 ? mGot : 48;
  for (I = 0; I < N; I += 16) {
    for (J = 0; J < 16 && I + J < N; J++) {
      UnicodeSPrint (Hex + J * 3, 4 * sizeof (CHAR16), L"%02x ", mBuf[I + J]);
    }
    Log (L"    %03x: %s", I, Hex);
  }
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  STATIC CONST UINT8  RegHid[2]   = { 0x20, 0x00 };
  STATIC CONST UINT8  RegRdesc[2] = { 0x21, 0x00 };
  STATIC CONST UINT8  PowerOn[4]  = { 0x22, 0x00, 0x00, 0x08 };
  STATIC CONST UINT8  Reset[4]    = { 0x22, 0x00, 0x00, 0x01 };
  UINT32  OldAddr, OldMask, Ms;

  OpenLog (ImageHandle);
  Log (L"===== KIRIN I2C XFER 3 (touchpad start-up) =====");
  OldAddr = Rd (BUS + R_SLV_ADDR);
  OldMask = Rd (BUS + R_INT_MASK);
  Log (L"TRANS_STATE=%08x  FRAME_CTRL=%08x  interrupt line (GPIO7 pin 4) = %u",
       Rd (BUS + R_TRANS_STATE), Rd (BUS + 0x00), (Rd (GPIO7_INPUT) >> 4) & 1);

  Xfer (RegHid, 2, 30, 0);
  Show (L"1 HID descriptor", 30);

  Xfer (RegRdesc, 2, RDESC_LEN, 0);
  Show (L"2 report descriptor, before reset, no pause", RDESC_LEN);

  Xfer (PowerOn, 4, 0, 0);
  Log (L"3 power on: %s", mEnd == 1 ? L"complete" : L"FAILED");
  Xfer (Reset, 4, 0, 0);
  Log (L"3 reset: %s", mEnd == 1 ? L"complete" : L"FAILED");
  for (Ms = 0; Ms < 2000 && ((Rd (GPIO7_INPUT) >> 4) & 1) != 0; Ms++) {
    gBS->Stall (1000);
  }
  Log (L"3 interrupt line went low after %u ms%s", Ms, Ms >= 2000 ? L" (NEVER - timeout)" : L"");
  Xfer (NULL, 0, 2, 0);
  Show (L"3 reset acknowledge (expect 00 00)", 2);
  Log (L"  interrupt line now = %u", (Rd (GPIO7_INPUT) >> 4) & 1);

  Xfer (RegRdesc, 2, RDESC_LEN, 0);
  Show (L"4 report descriptor, right after reset, no pause", RDESC_LEN);

  gBS->Stall (100000);
  Xfer (RegRdesc, 2, RDESC_LEN, 0);
  Show (L"5 report descriptor, 100 ms later, no pause", RDESC_LEN);

  Xfer (RegRdesc, 2, RDESC_LEN, 2000);
  Show (L"6 report descriptor, 2 ms pause inside the transfer", RDESC_LEN);

  Xfer (RegRdesc, 2, 0, 0);
  gBS->Stall (2000);
  Xfer (NULL, 0, RDESC_LEN, 0);
  Show (L"7 report descriptor, register write / stop / 2 ms / read", RDESC_LEN);

  Wr (BUS + R_SLV_ADDR, OldAddr);
  Wr (BUS + R_INT_MASK, OldMask);
  Log (L"===== END =====");
  if (mLog != NULL) {
    mLog->Close (mLog);
  }
  return EFI_SUCCESS;
}
