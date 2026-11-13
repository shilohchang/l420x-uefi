/** KirinI2cGpioProbe - read-only register dump of the keyboard's I2C controller
    and GPIO controller, to identify the IP blocks before writing Windows drivers.

    Keyboard:  HID-over-I2C at 0x3A on the I2C controller at 0xFF88F000,
               interrupt = pin 8 of the GPIO controller at 0xFB232000.
    Both blocks are in use by the firmware itself (the built-in keyboard works
    in the UEFI shell), so they are clocked and safe to read.

    What is dumped (reads only, nothing is written):
      * the first 0x100 bytes of each block
      * 0xF0-0xFC  (a Synopsys DesignWare I2C carries "DW" 0x44570140 at 0xFC)
      * 0xFE0-0xFFC (ARM PrimeCell peripheral / component ID)

    Argument /tp additionally dumps the touchpad's I2C controller at 0xFF88E000.
    The firmware does not use that bus; if its clock is off the read can hang
    or crash - that outcome is itself the answer, cold boot afterwards.

    Output: screen + \EFI\BOOT\logs\I2CGPIO.TXT
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
    if (!EFI_ERROR (Dir->Open (Dir, &mLog, L"I2CGPIO.TXT", Rw | EFI_FILE_MODE_CREATE, 0))) {
      mLog->SetPosition (mLog, MAX_UINT64);
    } else {
      mLog = NULL;
    }
    Dir->Close (Dir);
  }
  Root->Close (Root);
}


STATIC UINT32 Rd (UINT64 A) { return *(volatile UINT32 *)(UINTN)A; }

STATIC
VOID
Dump (
  IN CONST CHAR16  *Name,
  IN UINT64        Base
  )
{
  UINT32  Off;

  Log (L"--- %s @ %lx ---", Name, Base);
  for (Off = 0; Off < 0x100; Off += 0x10) {
    Log (L"%03x: %08x %08x %08x %08x", Off, Rd (Base + Off), Rd (Base + Off + 4), Rd (Base + Off + 8), Rd (Base + Off + 12));
  }
  Log (L"0F0: %08x %08x %08x %08x   (0FC = 44570140 means DesignWare I2C)",
       Rd (Base + 0xF0), Rd (Base + 0xF4), Rd (Base + 0xF8), Rd (Base + 0xFC));
  Log (L"FE0: %08x %08x %08x %08x   (PrimeCell peripheral ID)",
       Rd (Base + 0xFE0), Rd (Base + 0xFE4), Rd (Base + 0xFE8), Rd (Base + 0xFEC));
  Log (L"FF0: %08x %08x %08x %08x   (PrimeCell component ID, 0D F0 05 B1 = PrimeCell)",
       Rd (Base + 0xFF0), Rd (Base + 0xFF4), Rd (Base + 0xFF8), Rd (Base + 0xFFC));
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_LOADED_IMAGE_PROTOCOL  *Li = NULL;
  BOOLEAN                    Touchpad = FALSE;

  if (!EFI_ERROR (gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Li)) &&
      Li != NULL && Li->LoadOptions != NULL && Li->LoadOptionsSize >= 4 * sizeof (CHAR16) &&
      ((CHAR16 *)Li->LoadOptions)[Li->LoadOptionsSize / sizeof (CHAR16) - 1] == 0) {
    Touchpad = (StrStr ((CHAR16 *)Li->LoadOptions, L"/tp") != NULL);
  }

  OpenLog (ImageHandle);
  Log (L"===== KIRIN I2C / GPIO PROBE (read-only) =====");
  Dump (L"GPIO controller (keyboard + touchpad interrupt pins)", 0xFB232000ULL);
  Dump (L"I2C controller of the keyboard", 0xFF88F000ULL);
  if (Touchpad) {
    Log (L"About to read the touchpad I2C controller. If nothing follows, its clock is off.");
    Dump (L"I2C controller of the touchpad", 0xFF88E000ULL);
  }
  Log (L"===== END =====");
  if (mLog != NULL) {
    mLog->Close (mLog);
  }
  return EFI_SUCCESS;
}
