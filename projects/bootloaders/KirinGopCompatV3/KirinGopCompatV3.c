/** @file
  L420x Kirin 9000C GOP BGRA metadata workaround V3.
  Experimental, manual-load only. Not a hardware RGB/BGRA converter.
  Uses in-place GOP protocol modification to avoid UEFI reconnect dispatch
  from ReinstallProtocolInterface. Requires the physical native GOP in mode 0.
**/
#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>

#define PANEL_WIDTH  2880U
#define PANEL_HEIGHT 1920U
#define PANEL_BYTES  (2880ULL * 1920ULL * 4ULL)

typedef struct {
  EFI_HANDLE Handle;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE SetMode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION OriginalInfo;
  UINT32 OriginalMaxMode;
  UINT32 OriginalMode;
  EFI_PHYSICAL_ADDRESS OriginalBase;
  UINTN OriginalSize;
  BOOLEAN Installed;
} GOP_PATCH;

STATIC GOP_PATCH mPatch;

STATIC BOOLEAN
CheckUnderlyingState (VOID)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
  if (!mPatch.Installed || mPatch.Gop == NULL || mPatch.Gop->Mode == NULL) {
    return FALSE;
  }
  Mode = mPatch.Gop->Mode;
  return (BOOLEAN)(
    Mode->Mode == 0 &&
    Mode->MaxMode == 1 &&
    Mode->Info != NULL &&
    Mode->Info->HorizontalResolution == PANEL_WIDTH &&
    Mode->Info->VerticalResolution == PANEL_HEIGHT &&
    Mode->Info->PixelsPerScanLine == PANEL_WIDTH &&
    Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor &&
    Mode->FrameBufferBase == mPatch.OriginalBase &&
    Mode->FrameBufferSize == mPatch.OriginalSize);
}

STATIC EFI_STATUS EFIAPI
PatchedQueryMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  IN UINT32 ModeNumber,
  OUT UINTN *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info
  )
{
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Result;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Copy;
  if (This != mPatch.Gop || SizeOfInfo == NULL || Info == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *SizeOfInfo = 0;
  *Info = NULL;
  if (ModeNumber != 0) return EFI_INVALID_PARAMETER;
  if (!CheckUnderlyingState()) return EFI_DEVICE_ERROR;
  CopyMem(&Result, &mPatch.OriginalInfo, sizeof(Result));
  Result.PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  Copy = AllocateCopyPool(sizeof(Result), &Result);
  if (Copy == NULL) return EFI_OUT_OF_RESOURCES;
  *Info = Copy;
  *SizeOfInfo = sizeof(Result);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
PatchedSetMode (IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber)
{
  if (This != mPatch.Gop) return EFI_INVALID_PARAMETER;
  if (ModeNumber != 0) return EFI_UNSUPPORTED;
  // The one exposed mode is already active. Deliberately avoid DPU
  // reprogramming, which previously relocated the physical framebuffer.
  return CheckUnderlyingState() ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

STATIC EFI_STATUS EFIAPI
KirinUnload (IN EFI_HANDLE ImageHandle)
{
  EFI_STATUS Status;
  (VOID)ImageHandle;
  if (!mPatch.Installed) return EFI_SUCCESS;
  // Avoid undoing modifications installed after this driver.
  if (mPatch.Gop->QueryMode != PatchedQueryMode ||
      mPatch.Gop->SetMode != PatchedSetMode ||
      !CheckUnderlyingState()) {
    return EFI_ACCESS_DENIED;
  }
  mPatch.Gop->QueryMode = mPatch.QueryMode;
  mPatch.Gop->SetMode = mPatch.SetMode;
  CopyMem(mPatch.Gop->Mode->Info, &mPatch.OriginalInfo, sizeof(mPatch.OriginalInfo));
  mPatch.Gop->Mode->Mode = mPatch.OriginalMode;
  mPatch.Gop->Mode->MaxMode = mPatch.OriginalMaxMode;
  mPatch.Installed = FALSE;
  Status = EFI_SUCCESS;
  return Status;
}

EFI_STATUS EFIAPI
KirinEntry (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles = NULL;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;
  UINTN Count = 0;
  UINTN Index;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  (VOID)SystemTable;

  Print(L"KirinGopCompat V3: in-place BGRA metadata workaround (EXPERIMENTAL)\n");
  Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid,
                               (VOID **)&LoadedImage);
  if (EFI_ERROR(Status) || LoadedImage == NULL) return EFI_UNSUPPORTED;
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiGraphicsOutputProtocolGuid,
                                   NULL, &Count, &Handles);
  if (EFI_ERROR(Status)) return Status;

  for (Index = 0; Index < Count; ++Index) {
    DevicePath = NULL;
    Status = gBS->HandleProtocol(Handles[Index], &gEfiDevicePathProtocolGuid,
                                 (VOID **)&DevicePath);
    if (EFI_ERROR(Status) || DevicePath == NULL) continue;
    Gop = NULL;
    Status = gBS->HandleProtocol(Handles[Index], &gEfiGraphicsOutputProtocolGuid,
                                 (VOID **)&Gop);
    if (EFI_ERROR(Status) || Gop == NULL || Gop->Mode == NULL ||
        Gop->QueryMode == NULL || Gop->SetMode == NULL || Gop->Blt == NULL) continue;
    Mode = Gop->Mode;
    Info = Mode->Info;
    if (Info == NULL || Mode->Mode != 0 || Mode->MaxMode < 1 ||
        Info->HorizontalResolution != PANEL_WIDTH ||
        Info->VerticalResolution != PANEL_HEIGHT ||
        Info->PixelsPerScanLine != PANEL_WIDTH ||
        Info->PixelFormat != PixelRedGreenBlueReserved8BitPerColor ||
        Mode->FrameBufferBase == 0 ||
        (UINT64)Mode->FrameBufferSize < PANEL_BYTES) continue;

    // No framebuffer read/write here. BGRA bytes were independently confirmed
    // by the three-color KirinPixelProbe on this physical display.
    ZeroMem(&mPatch, sizeof(mPatch));
    mPatch.Handle = Handles[Index];
    mPatch.Gop = Gop;
    mPatch.QueryMode = Gop->QueryMode;
    mPatch.SetMode = Gop->SetMode;
    CopyMem(&mPatch.OriginalInfo, Info, sizeof(mPatch.OriginalInfo));
    mPatch.OriginalMaxMode = Mode->MaxMode;
    mPatch.OriginalMode = Mode->Mode;
    mPatch.OriginalBase = Mode->FrameBufferBase;
    mPatch.OriginalSize = Mode->FrameBufferSize;
    LoadedImage->Unload = KirinUnload;

    Print(L"Physical GOP=%p fb=0x%lx size=0x%lx; installing in-place patch\n",
          Handles[Index], (UINT64)Mode->FrameBufferBase, (UINT64)Mode->FrameBufferSize);
    // No ReinstallProtocolInterface: avoid reconnect/notify callbacks.
    // Modify current mode and functions directly, preserving BLT pointer.
    mPatch.Installed = TRUE;
    Info->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
    Mode->MaxMode = 1;
    Gop->QueryMode = PatchedQueryMode;
    Gop->SetMode = PatchedSetMode;
    FreePool(Handles);
    // Print no further text through the live console after the patch.
    return EFI_SUCCESS;
  }
  FreePool(Handles);
  Print(L"No physical native RGB8 GOP in mode 0: nothing patched\n");
  return EFI_UNSUPPORTED;
}
