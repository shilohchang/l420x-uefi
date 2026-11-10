/**
  Kirin 9000C V5.1 conservative GOP metadata adapter.
  Test ONLY if a previous pixel probe confirmed physical B,G,R,X order.
  No new GOP handles, no DPU/MMIO, no fake modes, no virtual SetMode.
  Loads as a resident UEFI driver and modifies exactly one GOP in RAM.
**/
#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#define EXPECTED_WIDTH  2880U
#define EXPECTED_HEIGHT 1920U
#define MIN_FRAMEBUFFER_SIZE (2880ULL * 1920ULL * 4ULL)

typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE OriginalQuery;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE OriginalSet;
  EFI_PHYSICAL_ADDRESS InitialBase;
  UINTN InitialSize;
  UINT32 InitialMode;
  BOOLEAN Installed;
} V51_CONTEXT;

STATIC V51_CONTEXT mCtx;

STATIC BOOLEAN
NativeGeometry (CONST EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info) {
  return (BOOLEAN)(Info != NULL &&
    Info->HorizontalResolution == EXPECTED_WIDTH &&
    Info->VerticalResolution == EXPECTED_HEIGHT &&
    Info->PixelsPerScanLine == EXPECTED_WIDTH);
}

STATIC BOOLEAN
CurrentPhysicalMode (EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop) {
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
  if (Gop == NULL || Gop != mCtx.Gop || !mCtx.Installed) return FALSE;
  Mode = Gop->Mode;
  return (BOOLEAN)(Mode != NULL && Mode->Info != NULL &&
    NativeGeometry(Mode->Info) &&
    Mode->FrameBufferBase == mCtx.InitialBase &&
    Mode->FrameBufferSize == mCtx.InitialSize &&
    (UINT64)Mode->FrameBufferSize >= MIN_FRAMEBUFFER_SIZE &&
    Mode->Mode < Mode->MaxMode);
}

STATIC EFI_STATUS EFIAPI
SafeQuery (EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
           UINT32 ModeNumber, UINTN *SizeOfInfo,
           EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info) {
  EFI_STATUS Status;
  if (This != mCtx.Gop || !mCtx.Installed) return EFI_DEVICE_ERROR;
  if (SizeOfInfo == NULL || Info == NULL) return EFI_INVALID_PARAMETER;
  Status = mCtx.OriginalQuery (This, ModeNumber, SizeOfInfo, Info);
  if (EFI_ERROR(Status)) return Status;
  if (*Info == NULL || *SizeOfInfo < sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION))
    return Status; // preserve original driver behavior; do not touch suspect data
  // Crucially, do not claim that an inactive mode has an identified BGRA backing store.
  if (CurrentPhysicalMode(This) && ModeNumber == This->Mode->Mode &&
      NativeGeometry(*Info) &&
      (*Info)->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
    (*Info)->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  }
  return Status;
}

STATIC EFI_STATUS EFIAPI
SafeSet (EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 ModeNumber) {
  EFI_STATUS Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
  if (This != mCtx.Gop || !mCtx.Installed) return EFI_DEVICE_ERROR;
  // V4 Native/Dual had a fake SetMode(0) success; V5.1 MUST use real firmware.
  Status = mCtx.OriginalSet (This, ModeNumber);
  if (EFI_ERROR(Status)) return Status;
  Mode = This->Mode;
  if (CurrentPhysicalMode(This) && Mode != NULL && Mode->Info != NULL &&
      Mode->Mode == ModeNumber &&
      Mode->Info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
    Mode->Info->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  }
  return Status;
}

STATIC EFI_STATUS EFIAPI
V51Unload (EFI_HANDLE ImageHandle) {
  (VOID)ImageHandle;
  if (!mCtx.Installed) return EFI_SUCCESS;
  if (mCtx.Gop->QueryMode != SafeQuery || mCtx.Gop->SetMode != SafeSet)
    return EFI_ACCESS_DENIED;
  mCtx.Gop->QueryMode = mCtx.OriginalQuery;
  mCtx.Gop->SetMode = mCtx.OriginalSet;
  // The firmware owns the active Mode->Info; don't restore an old snapshot
  // over potentially reallocated mode info after SetMode().
  // Metadata may remain BGRA until reboot; cold reboot is mandatory.
  mCtx.Installed = FALSE;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI
KirinEntry (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  EFI_HANDLE *Handles = NULL;
  EFI_LOADED_IMAGE_PROTOCOL *Image = NULL;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath = NULL;
  EFI_STATUS Status;
  UINTN Count = 0, Index, CandidateCount = 0;
  (VOID)SystemTable;
  Print(L"Kirin GOP V5.1 Safe - native mode preservation\n");
  Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid,
                              (VOID **)&Image);
  if (EFI_ERROR(Status) || Image == NULL) return EFI_UNSUPPORTED;
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiGraphicsOutputProtocolGuid,
                                   NULL, &Count, &Handles);
  if (EFI_ERROR(Status)) return Status;
  for (Index = 0; Index < Count; ++Index) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *G = NULL;
    EFI_DEVICE_PATH_PROTOCOL *Dp = NULL;
    if (EFI_ERROR(gBS->HandleProtocol(Handles[Index],
                  &gEfiGraphicsOutputProtocolGuid, (VOID **)&G)) || G == NULL)
      continue;
    if (EFI_ERROR(gBS->HandleProtocol(Handles[Index],
                  &gEfiDevicePathProtocolGuid, (VOID **)&Dp)) || Dp == NULL)
      continue;
    if (G->Mode == NULL || G->Mode->Info == NULL || G->QueryMode == NULL ||
        G->SetMode == NULL || G->Blt == NULL ||
        G->Mode->Mode >= G->Mode->MaxMode ||
        !NativeGeometry(G->Mode->Info) ||
        G->Mode->Info->PixelFormat != PixelRedGreenBlueReserved8BitPerColor ||
        G->Mode->FrameBufferBase == 0 ||
        (UINT64)G->Mode->FrameBufferSize < MIN_FRAMEBUFFER_SIZE)
      continue;
    Gop = G;
    DevicePath = Dp;
    CandidateCount++;
  }
  FreePool(Handles);
  // Do not guess which physical GOP is Windows' boot GOP if ambiguous.
  if (CandidateCount != 1 || Gop == NULL || DevicePath == NULL) {
    Print(L"Refused: eligible GOP with DevicePath count=%u (need exactly 1)\n",
          (UINT32)CandidateCount);
    return EFI_UNSUPPORTED;
  }
  Mode = Gop->Mode;
  ZeroMem(&mCtx, sizeof(mCtx));
  mCtx.Gop = Gop;
  mCtx.OriginalQuery = Gop->QueryMode;
  mCtx.OriginalSet = Gop->SetMode;
  mCtx.InitialBase = Mode->FrameBufferBase;
  mCtx.InitialSize = Mode->FrameBufferSize;
  mCtx.InitialMode = Mode->Mode;
  Print(L"Selected GOP=%p mode=%u/%u fb=0x%lx size=0x%lx\n",
        Gop, Mode->Mode, Mode->MaxMode, (UINT64)Mode->FrameBufferBase,
        (UINT64)Mode->FrameBufferSize);
  Print(L"NOTE: BGRA physical order must already be confirmed by pixel probe.\n");
  Image->Unload = V51Unload;
  // No Print() after touching the live GOP.
  mCtx.Installed = TRUE;
  Mode->Info->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  Gop->QueryMode = SafeQuery;
  Gop->SetMode = SafeSet;
  return EFI_SUCCESS;
}
