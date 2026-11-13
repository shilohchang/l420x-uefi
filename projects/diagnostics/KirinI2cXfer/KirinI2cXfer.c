/** KirinI2cXfer - first real I2C transfer on the L420X keyboard / touchpad buses.

  Probe4 only READ the controller registers. Before a Windows I2C controller
  driver is written, the transfer procedure itself has to be proven on this
  hardware, and that is much quicker to do from the UEFI shell than from
  Windows PE. This program reads the 30-byte "HID descriptor" from

    keyboard  bus 0xFF88F000, device 0x3A, register 0x0001  (expect 14F3:1400)
    touchpad  bus 0xFF88E000, device 0x2C, register 0x0020  (expect 0911:5288)

  using the register procedure of the mainline Linux driver i2c-hisi.c, and
  records how the status registers move while the transfer runs.

  Each run happens inside one TPL_HIGH_LEVEL window (firmware cannot touch the
  bus meanwhile); slave address, interrupt mask and clock gate are put back
  exactly as found before the window closes. The bus timing registers are not
  modified. Nothing is written to the keyboard or touchpad except the 2-byte
  register pointer.

  Runs: 1) keyboard, INT_MASK=0x1F   2) keyboard, INT_MASK=0   3) touchpad, 0x1F
  (two mask values, to learn whether INT_MSTAT needs the mask bits set)

  Output: screen + \\EFI\\BOOT\\logs\\I2CXFER.TXT
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
    if (!EFI_ERROR (Dir->Open (Dir, &mLog, L"I2CXFER.TXT", Rw | EFI_FILE_MODE_CREATE, 0))) {
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

STATIC
VOID
Run (
  IN CONST CHAR16  *What,
  IN UINT64        Base,
  IN UINT32        ClkBit,
  IN UINT32        RstBit,
  IN UINT32        Slave,
  IN UINT32        DescReg,
  IN UINT32        MaskVal
  )
{
  STATIC UINT32  Pre[24];
  STATIC UINT8   Data[RX_CAP];
  STATIC TRACE   Tr[TRACE_CAP];
  STATIC CHAR16  Hex[RX_CAP * 3 + 4];
  EFI_TPL        Old;
  UINT32         ClkBefore, ClkDuring, RstDuring, I, Cmd;
  UINT32         Pushed = 0, Rx = 0, NTr = 0, Iter = 0, Grace = 0;
  UINT32         M = 0, F = 0, T = 0, Err = 0, EndReason = 0;
  BOOLEAN        Ok;

  Log (L"");
  Log (L"--- %s: bus %lx, device 0x%02x, register 0x%04x, INT_MASK=0x%02x (crash after this line = this run) ---",
       What, Base, Slave, DescReg, MaskVal);

  Old = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  ClkBefore = Rd (CRG_BASE + 0x18);
  Wr (CRG_BASE + 0x10, ClkBit);                       // clock on (write-1-to-set)
  gBS->Stall (200);
  ClkDuring = Rd (CRG_BASE + 0x18);
  RstDuring = Rd (CRG_BASE + 0x68);
  Ok = (BOOLEAN)((ClkDuring & ClkBit) != 0 && (RstDuring & RstBit) == 0);
  if (Ok) {
    for (I = 0; I < 24; I++) {
      Pre[I] = (I == 3 || I == 18) ? 0xDEADDEAD : Rd (Base + I * 4);
    }
    ClearFifos (Base);
    Wr (Base + R_INT_CLR, 0x1F);
    Wr (Base + R_INT_MASK, MaskVal);
    Wr (Base + R_SLV_ADDR, Slave);

    // 2 bytes written (register pointer, little endian), repeated start, 30 bytes read, stop.
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
      Pushed++;
    }

    for (Iter = 0; Iter < 2000; Iter++) {             // at most 100 ms
      while (Rx < RX_CAP && (Rd (Base + R_FIFO_STATE) & FIFO_RX_EMPTY) == 0) {
        Data[Rx++] = (UINT8)Rd (Base + R_RXDATA);
      }
      M = Rd (Base + R_INT_MSTAT);
      F = Rd (Base + R_FIFO_STATE);
      T = Rd (Base + R_TRANS_STATE);
      if (NTr < TRACE_CAP &&
          (NTr == 0 || Tr[NTr - 1].Mstat != M || Tr[NTr - 1].Fifo != F || Tr[NTr - 1].Tstate != T)) {
        Tr[NTr].Iter = Iter; Tr[NTr].Mstat = M; Tr[NTr].Fifo = F; Tr[NTr].Tstate = T; Tr[NTr].Rx = Rx;
        NTr++;
      }
      if ((M & 0x6) != 0) {                           // transfer error / FIFO error
        EndReason = 2;
        break;
      }
      if (Rx >= DESC_LEN) {
        if ((M & 0x1) != 0) {                         // transfer complete reported
          EndReason = 1;
          break;
        }
        if (++Grace > 40) {                           // all data in, 2 ms later still no "complete"
          EndReason = 3;
          break;
        }
      }
      gBS->Stall (50);
    }
    Err = Rd (Base + R_TRANS_ERR);

    Wr (Base + R_INT_MASK, Pre[19]);                  // put everything back
    Wr (Base + R_INT_CLR, 0x1F);
    ClearFifos (Base);
    Wr (Base + R_SLV_ADDR, Pre[1]);
  }
  if ((ClkBefore & ClkBit) == 0) {
    Wr (CRG_BASE + 0x14, ClkBit);
  }
  gBS->RestoreTPL (Old);

  Log (L"clock before/during: %08x / %08x (bit %08x)   reset status: %08x (bit %08x)",
       ClkBefore, ClkDuring, ClkBit, RstDuring, RstBit);
  if (!Ok) {
    Log (L"clock did not come on or reset asserted - nothing done");
    return;
  }
  Log (L"registers before the transfer:");
  for (I = 0; I < 24; I += 4) {
    Log (L"  %02x: %08x %08x %08x %08x", I * 4, Pre[I], Pre[I + 1], Pre[I + 2], Pre[I + 3]);
  }
  Log (L"commands queued: %u of %u", Pushed, 2 + DESC_LEN);
  Log (L"status trace (time in 50us steps: INT_MSTAT FIFO_STATE TRANS_STATE bytes-received):");
  for (I = 0; I < NTr; I++) {
    Log (L"  t=%4u  mstat=%08x fifo=%08x tstate=%08x rx=%u", Tr[I].Iter, Tr[I].Mstat, Tr[I].Fifo, Tr[I].Tstate, Tr[I].Rx);
  }
  Log (L"ended at t=%u: %s   TRANS_ERR=%08x", Iter,
       EndReason == 1 ? L"COMPLETE flag seen" :
       EndReason == 2 ? L"ERROR flag seen" :
       EndReason == 3 ? L"all bytes received, but no COMPLETE flag" : L"TIMEOUT", Err);
  Log (L"bytes received: %u", Rx);
  for (I = 0; I < Rx; I++) {
    UnicodeSPrint (Hex + I * 3, 4 * sizeof (CHAR16), L"%02x ", Data[I]);
  }
  if (Rx != 0) {
    Log (L"  %s", Hex);
  }
  if (Rx >= DESC_LEN && Data[0] == 0x1E && Data[1] == 0x00) {
    Log (L"RESULT: HID DESCRIPTOR OK  vendor:product = %02x%02x:%02x%02x  report-descriptor %u bytes at reg 0x%02x%02x, max input %u bytes",
         Data[21], Data[20], Data[23], Data[22],
         (UINT32)(Data[4] | (Data[5] << 8)), Data[7], Data[6], (UINT32)(Data[10] | (Data[11] << 8)));
  } else {
    Log (L"RESULT: no valid HID descriptor");
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
  Log (L"===== KIRIN I2C XFER 1 =====");
  Run (L"keyboard", 0xFF88F000ULL, 1U << 31, 1U << 14, 0x3A, 0x0001, 0x1F);
  Run (L"keyboard", 0xFF88F000ULL, 1U << 31, 1U << 14, 0x3A, 0x0001, 0x00);
  Run (L"touchpad", 0xFF88E000ULL, 1U << 30, 1U << 19, 0x2C, 0x0020, 0x1F);
  Log (L"===== END =====");
  if (mLog != NULL) {
    mLog->Close (mLog);
  }
  return EFI_SUCCESS;
}
