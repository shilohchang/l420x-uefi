#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DevicePath.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

STATIC EFI_STATUS TestPixel(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop) {
  EFI_STATUS Status, RestoreStatus;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Saved, Red;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
  volatile UINT8 *Pixel;
  UINTN Offset;

  if (Gop == NULL || Gop->Mode == NULL || Gop->Mode->Info == NULL) return EFI_INVALID_PARAMETER;
  Mode = Gop->Mode;
  if (Mode->Info->HorizontalResolution < 2 || Mode->Info->VerticalResolution < 2 ||
      Mode->Info->PixelsPerScanLine != Mode->Info->HorizontalResolution ||
      Mode->Info->PixelFormat == PixelBltOnly || Mode->FrameBufferBase == 0 ||
      Mode->FrameBufferSize < 4) return EFI_UNSUPPORTED;
  // Probe pixel (0,0): use BLT so firmware chooses its native pixel layout.
  Status = Gop->Blt(Gop, &Saved, EfiBltVideoToBltBuffer, 0, 0, 0, 0, 1, 1, 0);
  if (EFI_ERROR(Status)) return Status;
  Red.Blue = 0; Red.Green = 0; Red.Red = 255; Red.Reserved = 0;
  Status = Gop->Blt(Gop, &Red, EfiBltBufferToVideo, 0, 0, 0, 0, 1, 1, 0);
  if (EFI_ERROR(Status)) return Status;
  Pixel = (volatile UINT8 *)(UINTN)Mode->FrameBufferBase;
  // Ensure we only read within the advertised buffer. A valid MMIO mapping is still required.
  Offset = 0;
  Print(L"  Red pixel raw bytes: %02x %02x %02x %02x\n", Pixel[Offset], Pixel[Offset+1], Pixel[Offset+2], Pixel[Offset+3]);
  if (Pixel[0] == 0 && Pixel[1] == 0 && Pixel[2] == 255) {
    Print(L"  RESULT: BGRA memory order (B G R X) detected\n");
  } else if (Pixel[0] == 255 && Pixel[1] == 0 && Pixel[2] == 0) {
    Print(L"  RESULT: RGBA memory order (R G B X) detected; metadata-only fix unsafe\n");
  } else {
    Print(L"  RESULT: INCONCLUSIVE (BLT path or scanout may be indirect)\n");
  }
  RestoreStatus = Gop->Blt(Gop, &Saved, EfiBltBufferToVideo, 0, 0, 0, 0, 1, 1, 0);
  if (EFI_ERROR(RestoreStatus)) Print(L"  WARNING: restore pixel failed: %r\n", RestoreStatus);
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0, I;
  EFI_STATUS Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_DEVICE_PATH_PROTOCOL *Dp;
  Print(L"KirinPixelProbe V1 -- probe paints/restores one pixel; no SetMode\n");
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR(Status)) return Status;
  for (I = 0; I < Count; ++I) {
    Status = gBS->HandleProtocol(Handles[I], &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop);
    if (EFI_ERROR(Status) || Gop == NULL || Gop->Mode == NULL || Gop->Mode->Info == NULL) continue;
    Dp = NULL;
    gBS->HandleProtocol(Handles[I], &gEfiDevicePathProtocolGuid, (VOID **)&Dp);
    Print(L"GOP[%u] handle=%p DevicePath=%s mode=%u %ux%u format=%u fb=0x%lx size=0x%lx\n",
      (UINT32)I, Handles[I], Dp ? L"YES" : L"NO", Gop->Mode->Mode,
      Gop->Mode->Info->HorizontalResolution, Gop->Mode->Info->VerticalResolution,
      Gop->Mode->Info->PixelFormat, Gop->Mode->FrameBufferBase, (UINT64)Gop->Mode->FrameBufferSize);
    if (Dp == NULL) { Print(L"  skipped duplicate/virtual GOP without DevicePath\n"); continue; }
    // Only touch physical display GOP; probe is voluntary, and visible pixel may flash.
    Status = TestPixel(Gop);
    Print(L"  probe status=%r\n", Status);
  }
  FreePool(Handles);
  return EFI_SUCCESS;
}
