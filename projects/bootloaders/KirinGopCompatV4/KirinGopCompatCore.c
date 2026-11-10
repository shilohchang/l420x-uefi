/** @file
 L420x Kirin 9000C GOP experimental compatibility profiles.
 Tested conceptually against Kirin GOP logs, not hardware validated.
 Each image must be tested in a fresh boot. Do not stack profiles.
*/
#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>

#ifndef KIRIN_PROFILE
#error KIRIN_PROFILE must be 1, 2 or 3
#endif
#define WIDTH 2880U
#define HEIGHT 1920U
#define MIN_BYTES (2880ULL * 1920ULL * 4ULL)
#define MAX_PATCHES 2

typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE OldQuery;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE OldSet;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION OldInfo;
  UINT32 OldMode;
  UINT32 OldMaxMode;
  EFI_PHYSICAL_ADDRESS OldBase;
  UINTN OldSize;
  BOOLEAN Active;
} PATCH;
STATIC PATCH mP[MAX_PATCHES];
STATIC UINTN mCount = 0;

STATIC PATCH *FindPatch(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop) {
  UINTN I;
  for (I = 0; I < mCount; I++) if (mP[I].Gop == Gop && mP[I].Active) return &mP[I];
  return NULL;
}
STATIC BOOLEAN __attribute__((unused)) NativeNow(PATCH *P) {
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *M;
  if (P == NULL || P->Gop == NULL || P->Gop->Mode == NULL) return FALSE;
  M = P->Gop->Mode;
  if (M->Info == NULL) return FALSE;
  return (BOOLEAN)(M->Info->HorizontalResolution == WIDTH &&
    M->Info->VerticalResolution == HEIGHT &&
    M->Info->PixelsPerScanLine == WIDTH &&
    M->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor &&
    M->FrameBufferBase == P->OldBase && M->FrameBufferSize == P->OldSize);
}
STATIC EFI_STATUS EFIAPI CompatQuery(
  EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 ModeNumber,
  UINTN *SizeOfInfo, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info) {
  PATCH *P = FindPatch(This);
  if (P == NULL) return EFI_DEVICE_ERROR;
  if (SizeOfInfo == NULL || Info == NULL) return EFI_INVALID_PARAMETER;
  *SizeOfInfo = 0; *Info = NULL;
#if KIRIN_PROFILE == 1
  // Keep all original modes. Only correct reported native mode format.
  EFI_STATUS S;
  S = P->OldQuery(This, ModeNumber, SizeOfInfo, Info);
  if (!EFI_ERROR(S) && *Info != NULL &&
      (*Info)->HorizontalResolution == WIDTH &&
      (*Info)->VerticalResolution == HEIGHT &&
      (*Info)->PixelsPerScanLine == WIDTH &&
      (*Info)->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
    (*Info)->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  }
  return S;
#else
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Copy;
  if (ModeNumber != 0) return EFI_INVALID_PARAMETER;
  if (!NativeNow(P)) return EFI_DEVICE_ERROR;
  Copy = AllocateCopyPool(sizeof(P->OldInfo), &P->OldInfo);
  if (Copy == NULL) return EFI_OUT_OF_RESOURCES;
  Copy->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  *Info = Copy; *SizeOfInfo = sizeof(*Copy);
  return EFI_SUCCESS;
#endif
}
STATIC EFI_STATUS EFIAPI CompatSet(EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 ModeNumber) {
  PATCH *P = FindPatch(This);
  if (P == NULL) return EFI_DEVICE_ERROR;
#if KIRIN_PROFILE == 1
  // For a mode change, the original driver owns the transition.
  // Its QueryMode may again be RGB; patch freshly assigned Mode->Info if native.
  EFI_STATUS S = P->OldSet(This, ModeNumber);
  if (!EFI_ERROR(S) && This->Mode != NULL && This->Mode->Info != NULL &&
      This->Mode->Info->HorizontalResolution == WIDTH &&
      This->Mode->Info->VerticalResolution == HEIGHT &&
      This->Mode->Info->PixelsPerScanLine == WIDTH &&
      This->Mode->Info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
    This->Mode->Info->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  }
  return S;
#else
  // Mode 0 is the virtual single native mode. No DPU reinitialization.
  if (ModeNumber != 0) return EFI_UNSUPPORTED;
  return NativeNow(P) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
#endif
}
STATIC EFI_STATUS EFIAPI Unload(EFI_HANDLE ImageHandle) {
  UINTN I;
  (VOID)ImageHandle;
  // Return failure without partial rollback if other code modified either GOP.
  for (I = 0; I < mCount; I++) {
    if (mP[I].Gop->QueryMode != CompatQuery || mP[I].Gop->SetMode != CompatSet)
      return EFI_ACCESS_DENIED;
  }
  for (I = mCount; I > 0; I--) {
    PATCH *P = &mP[I - 1];
    P->Gop->QueryMode = P->OldQuery;
    P->Gop->SetMode = P->OldSet;
    if (P->Gop->Mode != NULL && P->Gop->Mode->Info != NULL) {
      // Mode/Info may have been reassigned by the firmware; restore saved
      // metadata only if the physical framebuffer has not changed.
      if (P->Gop->Mode->FrameBufferBase == P->OldBase &&
          P->Gop->Mode->FrameBufferSize == P->OldSize) {
        CopyMem(P->Gop->Mode->Info, &P->OldInfo, sizeof(P->OldInfo));
        P->Gop->Mode->Mode = P->OldMode;
        P->Gop->Mode->MaxMode = P->OldMaxMode;
      }
    }
    P->Active = FALSE;
  }
  mCount = 0;
  return EFI_SUCCESS;
}
EFI_STATUS EFIAPI KirinEntry(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  EFI_HANDLE *Handles = NULL;
  EFI_LOADED_IMAGE_PROTOCOL *Image = NULL;
  EFI_STATUS S;
  UINTN N = 0, I;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *G;
  EFI_DEVICE_PATH_PROTOCOL *Dp;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *M;
  UINTN CandidateCount = 0;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Candidates[MAX_PATCHES];
  (VOID)SystemTable;
  Print(L"KirinGopCompat V4 profile=%d (EXPERIMENTAL)\n", KIRIN_PROFILE);
  S = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image);
  if (EFI_ERROR(S) || Image == NULL) return EFI_UNSUPPORTED;
  S = gBS->LocateHandleBuffer(ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &N, &Handles);
  if (EFI_ERROR(S)) return S;
  ZeroMem(mP, sizeof(mP));
  for (I = 0; I < N; I++) {
    G = NULL; Dp = NULL;
    if (EFI_ERROR(gBS->HandleProtocol(Handles[I], &gEfiGraphicsOutputProtocolGuid, (VOID **)&G)) || G == NULL || G->Mode == NULL) continue;
    M = G->Mode;
    S = gBS->HandleProtocol(Handles[I], &gEfiDevicePathProtocolGuid, (VOID **)&Dp);
    Print(L"Handle[%u]=%p devicepath=%d mode=%u max=%u fb=0x%lx format=%d\n",
      (UINT32)I, Handles[I], !EFI_ERROR(S) && Dp != NULL,
      M->Mode, M->MaxMode, (UINT64)M->FrameBufferBase,
      M->Info == NULL ? 999 : M->Info->PixelFormat);
    if (G->QueryMode == NULL || G->SetMode == NULL || G->Blt == NULL ||
        M->Info == NULL || M->MaxMode < 1 ||
        M->Info->HorizontalResolution != WIDTH || M->Info->VerticalResolution != HEIGHT ||
        M->Info->PixelsPerScanLine != WIDTH ||
        M->Info->PixelFormat != PixelRedGreenBlueReserved8BitPerColor ||
        (M->Mode != 0 && M->Mode != 3) ||
        M->FrameBufferBase == 0 || (UINT64)M->FrameBufferSize < MIN_BYTES)
      continue;
#if KIRIN_PROFILE != 3
    if (EFI_ERROR(S) || Dp == NULL) continue;
#else
    // Always select the physical device before a virtual GOP.
    if (CandidateCount == 0 && (EFI_ERROR(S) || Dp == NULL)) continue;
    if (CandidateCount == 1 && Candidates[0] == G) continue;
    if (CandidateCount == 1 && Candidates[0]->Mode->FrameBufferBase != M->FrameBufferBase) {
      Print(L"Skip secondary: framebuffers differ\n");
      continue;
    }
#endif
    if (CandidateCount < MAX_PATCHES) Candidates[CandidateCount++] = G;
#if KIRIN_PROFILE != 3
    break;
#endif
  }
  FreePool(Handles);
  if (CandidateCount == 0) {
    Print(L"No candidate: inspect mode, native geometry, format and DevicePath\n");
    return EFI_UNSUPPORTED;
  }
  for (I = 0; I < CandidateCount; I++) {
    G = Candidates[I]; M = G->Mode;
    mP[I].Gop = G; mP[I].OldQuery = G->QueryMode; mP[I].OldSet = G->SetMode;
    mP[I].OldMode = M->Mode; mP[I].OldMaxMode = M->MaxMode;
    mP[I].OldBase = M->FrameBufferBase; mP[I].OldSize = M->FrameBufferSize;
    CopyMem(&mP[I].OldInfo, M->Info, sizeof(mP[I].OldInfo));
    Print(L"Prepare[%u] original mode=%u fb=0x%lx size=0x%lx\n", (UINT32)I,
      M->Mode, (UINT64)M->FrameBufferBase, (UINT64)M->FrameBufferSize);
  }
  // Register unload before modifications; no new allocation afterwards.
  Image->Unload = Unload;
  // Keep driver image resident, do not use ReinstallProtocolInterface.
  // Deliberately stop printing after this point: live Shell may use GOP.
  mCount = CandidateCount;
  for (I = 0; I < CandidateCount; I++) {
    PATCH *P = &mP[I];
    M = P->Gop->Mode;
    P->Active = TRUE;
    M->Info->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
#if KIRIN_PROFILE != 1
    M->Mode = 0;
    M->MaxMode = 1;
#endif
    P->Gop->QueryMode = CompatQuery;
    P->Gop->SetMode = CompatSet;
  }
  return EFI_SUCCESS;
}
