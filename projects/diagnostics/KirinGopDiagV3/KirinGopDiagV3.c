/** KirinGopDiagV3 - GOP diagnostic for L420X (Kirin 9000C).

  Prints every line to the screen AND appends it to
  \EFI\BOOT\logs\GOPDIAG.TXT on the volume this program was started from.

  Default run is read-only: no SetMode, no Blt, no framebuffer access.
  With the argument  /bars  it additionally draws two rows of colour blocks
  for 8 seconds (row 1 through Blt, row 2 by writing the framebuffer directly
  assuming BGR byte order) so the real pixel layout can be seen by eye.
**/
#include <Uefi.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DevicePath.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/PrintLib.h>

STATIC EFI_FILE_PROTOCOL  *mLog = NULL;

STATIC
VOID
EFIAPI
Log (
  IN CONST CHAR16  *Format,
  ...
  )
{
  STATIC CHAR16  Wide[512];
  STATIC CHAR8   Narrow[512];
  VA_LIST        Args;
  UINTN          Len;
  UINTN          I;

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
EFI_STATUS
OpenLog (
  IN EFI_LOADED_IMAGE_PROTOCOL  *Li
  )
{
  EFI_STATUS                       S;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root = NULL;
  EFI_FILE_PROTOCOL                *Dir  = NULL;
  UINT64                           Rw    = EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE;

  S = gBS->HandleProtocol (Li->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (S)) {
    return S;
  }
  S = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (S)) {
    return S;
  }
  S = Root->Open (Root, &Dir, L"\\EFI\\BOOT\\logs", Rw | EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY);
  if (!EFI_ERROR (S)) {
    S = Dir->Open (Dir, &mLog, L"GOPDIAG.TXT", Rw | EFI_FILE_MODE_CREATE, 0);
    if (!EFI_ERROR (S)) {
      mLog->SetPosition (mLog, MAX_UINT64);   // append
    }
    Dir->Close (Dir);
  }
  Root->Close (Root);
  return S;
}

STATIC
CONST CHAR16 *
FormatName (
  IN EFI_GRAPHICS_PIXEL_FORMAT  F
  )
{
  switch (F) {
    case PixelRedGreenBlueReserved8BitPerColor: return L"RGBX8888 (Windows: NOT accepted)";
    case PixelBlueGreenRedReserved8BitPerColor: return L"BGRX8888 (Windows: accepted)";
    case PixelBitMask:                          return L"BITMASK";
    case PixelBltOnly:                          return L"BLT_ONLY (no framebuffer)";
    default:                                    return L"UNKNOWN";
  }
}

STATIC
VOID
LogInfo (
  IN CONST CHAR16                          *Tag,
  IN EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info
  )
{
  Log (L"  %s %ux%u stride=%u format=%u %s", Tag,
       Info->HorizontalResolution, Info->VerticalResolution,
       Info->PixelsPerScanLine, (UINT32)Info->PixelFormat, FormatName (Info->PixelFormat));
  if (Info->PixelFormat == PixelBitMask) {
    Log (L"    masks R=%08x G=%08x B=%08x X=%08x",
         Info->PixelInformation.RedMask, Info->PixelInformation.GreenMask,
         Info->PixelInformation.BlueMask, Info->PixelInformation.ReservedMask);
  }
}

STATIC
VOID
LogMemoryDescriptorFor (
  IN EFI_PHYSICAL_ADDRESS  Addr
  )
{
  EFI_STATUS             S;
  EFI_MEMORY_DESCRIPTOR  *Map = NULL;
  EFI_MEMORY_DESCRIPTOR  *D;
  UINTN                  Size = 0, Key = 0, DescSize = 0, I;
  UINT32                 Ver = 0;
  BOOLEAN                Found = FALSE;

  S = gBS->GetMemoryMap (&Size, NULL, &Key, &DescSize, &Ver);
  if (S != EFI_BUFFER_TOO_SMALL || DescSize == 0) {
    Log (L"  memory map probe failed: %r", S);
    return;
  }
  Size += 16 * DescSize;
  Map = AllocatePool (Size);
  if (Map == NULL) {
    return;
  }
  S = gBS->GetMemoryMap (&Size, Map, &Key, &DescSize, &Ver);
  if (!EFI_ERROR (S)) {
    for (I = 0; I < Size / DescSize; I++) {
      D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + I * DescSize);
      if (Addr >= D->PhysicalStart &&
          Addr < D->PhysicalStart + LShiftU64 (D->NumberOfPages, 12)) {
        Log (L"  FB memory map entry: type=%u start=%lx pages=%lx attr=%lx",
             D->Type, D->PhysicalStart, D->NumberOfPages, D->Attribute);
        Found = TRUE;
      }
    }
    if (!Found) {
      Log (L"  FB memory map entry: NONE (framebuffer address is not described in the UEFI memory map)");
    }
  }
  FreePool (Map);
}

STATIC
VOID
DrawBars (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop
  )
{
  // Order on screen, left to right, if everything is as expected: RED GREEN BLUE WHITE
  STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  BltColour[4] = {
    { 0x00, 0x00, 0xFF, 0 }, { 0x00, 0xFF, 0x00, 0 }, { 0xFF, 0x00, 0x00, 0 }, { 0xFF, 0xFF, 0xFF, 0 }
  };
  STATIC CONST UINT32  RawBgr[4] = { 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFFFF };

  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info = Gop->Mode->Info;
  UINT32      W = Info->HorizontalResolution, H = Info->VerticalResolution;
  UINT32      Bw = W / 4, Bh = H / 8, Y1 = H / 2, Y2 = Y1 + Bh;
  UINT32      I, X, Y;
  EFI_STATUS  S;
  UINT32      *Fb;
  UINT64      Need;

  if (Bw == 0 || Bh == 0) {
    return;
  }
  for (I = 0; I < 4; I++) {
    S = Gop->Blt (Gop, &BltColour[I], EfiBltVideoFill, 0, 0, I * Bw, Y1, Bw, Bh, 0);
    if (EFI_ERROR (S)) {
      Log (L"  bars row1 (Blt) block %u: %r", I, S);
      break;
    }
  }
  Log (L"  bars row1 drawn through Blt: expect RED GREEN BLUE WHITE");

  Need = MultU64x32 ((UINT64)Info->PixelsPerScanLine * 4, Y2 + Bh);
  if ((Info->PixelFormat != PixelRedGreenBlueReserved8BitPerColor &&
       Info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor) ||
      Gop->Mode->FrameBufferBase == 0 || Need > Gop->Mode->FrameBufferSize) {
    Log (L"  bars row2 (direct framebuffer) SKIPPED: format/base/size not suitable");
  } else {
    Fb = (UINT32 *)(UINTN)Gop->Mode->FrameBufferBase;
    for (Y = Y2; Y < Y2 + Bh; Y++) {
      for (X = 0; X < 4 * Bw; X++) {
        Fb[(UINTN)Y * Info->PixelsPerScanLine + X] = RawBgr[X / Bw];
      }
    }
    Log (L"  bars row2 written directly as BGR: RED GREEN BLUE WHITE = real layout is BGR;");
    Log (L"                                     BLUE GREEN RED WHITE = real layout is RGB;");
    Log (L"                                     nothing visible      = framebuffer address is not the live screen");
  }
  gBS->Stall (8 * 1000 * 1000);
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                            S;
  EFI_LOADED_IMAGE_PROTOCOL             *Li = NULL;
  EFI_HANDLE                            *Handles = NULL;
  UINTN                                 Count = 0, I, InfoSize;
  UINT32                                M;
  BOOLEAN                               Bars = FALSE;
  EFI_GRAPHICS_OUTPUT_PROTOCOL          *Gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
  EFI_DEVICE_PATH_PROTOCOL              *Dp;
  CHAR16                                *Text;
  UINTN                                 Usable = 0;

  S = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Li);
  if (!EFI_ERROR (S)) {
    S = OpenLog (Li);
    if (Li->LoadOptions != NULL && Li->LoadOptionsSize >= 6 * sizeof (CHAR16) &&
        ((CHAR16 *)Li->LoadOptions)[Li->LoadOptionsSize / sizeof (CHAR16) - 1] == 0) {
      Bars = (StrStr ((CHAR16 *)Li->LoadOptions, L"/bars") != NULL);
    }
  }

  Log (L"===== KIRIN GOP DIAG V3 START =====");
  Log (L"log file: %r   bars test: %s", S, Bars ? L"YES" : L"no");
  Log (L"firmware: %s rev 0x%x, UEFI 0x%x",
       gST->FirmwareVendor != NULL ? gST->FirmwareVendor : L"?", gST->FirmwareRevision, gST->Hdr.Revision);
  Log (L"ConsoleOutHandle=%lx", (UINT64)(UINTN)gST->ConsoleOutHandle);

  S = gBS->LocateHandleBuffer (ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles);
  Log (L"GOP handles: %u (%r)", (UINT32)Count, S);

  for (I = 0; !EFI_ERROR (S) && I < Count; I++) {
    Gop = NULL;
    Log (L"--- GOP[%u] handle=%lx %s", (UINT32)I, (UINT64)(UINTN)Handles[I],
         Handles[I] == gST->ConsoleOutHandle ? L"(is ConsoleOutHandle)" : L"");

    Dp = NULL;
    if (!EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiDevicePathProtocolGuid, (VOID **)&Dp)) && Dp != NULL) {
      Text = ConvertDevicePathToText (Dp, FALSE, FALSE);
      Log (L"  device path: %s", Text != NULL ? Text : L"(cannot convert)");
      if (Text != NULL) {
        FreePool (Text);
      }
    } else {
      Log (L"  device path: none (virtual/console-splitter GOP)");
    }

    if (EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop)) ||
        Gop == NULL || Gop->Mode == NULL || Gop->Mode->Info == NULL) {
      Log (L"  GOP protocol or Mode pointer missing");
      continue;
    }

    Log (L"  current mode=%u MaxMode=%u SizeOfInfo=%u",
         Gop->Mode->Mode, Gop->Mode->MaxMode, (UINT32)Gop->Mode->SizeOfInfo);
    LogInfo (L"CURRENT", Gop->Mode->Info);
    Log (L"  FrameBufferBase=%lx FrameBufferSize=%lx",
         (UINT64)Gop->Mode->FrameBufferBase, (UINT64)Gop->Mode->FrameBufferSize);
    if (Gop->Mode->FrameBufferBase != 0) {
      LogMemoryDescriptorFor (Gop->Mode->FrameBufferBase);
    }

    for (M = 0; M < Gop->Mode->MaxMode; M++) {
      Info = NULL;
      InfoSize = 0;
      if (!EFI_ERROR (Gop->QueryMode (Gop, M, &InfoSize, &Info)) && Info != NULL) {
        CHAR16  Tag[16];
        UnicodeSPrint (Tag, sizeof (Tag), L"mode[%u]", M);
        LogInfo (Tag, Info);
        FreePool (Info);
      } else {
        Log (L"  mode[%u] QueryMode failed", M);
      }
    }

    if (Gop->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor &&
        Gop->Mode->FrameBufferBase != 0 &&
        Gop->Mode->FrameBufferSize >= (UINT64)Gop->Mode->Info->PixelsPerScanLine * 4 * Gop->Mode->Info->VerticalResolution) {
      Log (L"  VERDICT: usable by Windows boot graphics (BGR, linear framebuffer present)");
      Usable++;
    } else {
      Log (L"  VERDICT: NOT usable by Windows boot graphics as reported");
    }

    if (Bars) {
      DrawBars (Gop);
    }
  }

  Log (L"SUMMARY: %u of %u GOP instance(s) look usable by Windows", (UINT32)Usable, (UINT32)Count);
  Log (L"===== KIRIN GOP DIAG V3 END =====");

  if (Handles != NULL) {
    FreePool (Handles);
  }
  if (mLog != NULL) {
    mLog->Close (mLog);
  }
  return EFI_SUCCESS;
}
