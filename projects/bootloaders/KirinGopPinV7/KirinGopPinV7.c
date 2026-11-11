/** KirinGopPinV7 - pin the L420X GOP to ONE fixed linear framebuffer.

  Why: the stock firmware GOP reports RGB and flips between two buffers
  (0x51DD0000 / 0x532E8000) inside its own Blt/SetMode. Windows needs a BGR
  linear framebuffer whose address never changes, and it writes that address
  directly after ExitBootServices.

  What this resident driver does after `load`:
    * picks one of the two buffers (variant A = the one reported right now,
      variant B = the other one; built as two .efi files),
    * replaces QueryMode / SetMode / Blt on every GOP instance with software
      versions that only touch that buffer, so the firmware display code is
      never called again and nothing flips any more,
    * reports exactly one mode, BGRX8888, with that fixed FrameBufferBase.

  Self test: right after patching it paints a GREEN strip across the top of
  the screen and prints a line. If you can see them, this buffer is the one on
  screen. If the screen freezes / shows nothing new, it is the wrong buffer:
  cold boot and use the other variant.

  No MMIO, NVRAM or disk writes. Cannot be unloaded; cold boot to undo.
**/
#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DevicePath.h>
#include <Protocol/Cpu.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#define BUF0  0x51DD0000ULL
#define BUF1  0x532E8000ULL

#ifdef KIRIN_PIN_OTHER
#define VARIANT  L"B (the buffer NOT currently reported)"
#else
#define VARIANT  L"A (the buffer currently reported)"
#endif

STATIC UINT32                                *mFb;
STATIC UINT32                                mW, mH, mStride;
STATIC EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  mInfo;

/* The framebuffer is ordinary RAM that the display controller reads directly.
   The firmware maps it as cached memory, so pixels written by the CPU can sit in
   the CPU cache for a long time before they reach RAM: that is the "striped"
   picture that slowly heals. Push every written range out to RAM explicitly. */
STATIC
VOID
FlushRange (
  IN VOID   *Start,
  IN UINTN  Length
  )
{
  UINT64  Ctr;
  UINTN   Line, Addr, End;

  __asm__ __volatile__ ("mrs %0, ctr_el0" : "=r" (Ctr));
  Line = (UINTN)4 << ((Ctr >> 16) & 0xF);
  Addr = (UINTN)Start & ~(Line - 1);
  End  = (UINTN)Start + Length;
  for ( ; Addr < End; Addr += Line) {
    __asm__ __volatile__ ("dc cvac, %0" : : "r" (Addr));
  }
  __asm__ __volatile__ ("dsb sy");
}

STATIC
EFI_STATUS
EFIAPI
PinQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  )
{
  if (SizeOfInfo == NULL || Info == NULL || ModeNumber != 0) {
    return EFI_INVALID_PARAMETER;
  }
  *Info = AllocateCopyPool (sizeof (mInfo), &mInfo);
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  *SizeOfInfo = sizeof (mInfo);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
PinSetMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
  IN UINT32                        ModeNumber
  )
{
  UINT32  Y;

  if (ModeNumber != 0) {
    return EFI_UNSUPPORTED;
  }
  for (Y = 0; Y < mH; Y++) {
    SetMem32 (mFb + (UINTN)Y * mStride, (UINTN)mW * 4, 0);   // spec: SetMode clears to black
  }
  FlushRange (mFb, (UINTN)mStride * mH * 4);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
PinBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL      *BltBuffer  OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  Op,
  IN UINTN                              Sx,
  IN UINTN                              Sy,
  IN UINTN                              Dx,
  IN UINTN                              Dy,
  IN UINTN                              W,
  IN UINTN                              H,
  IN UINTN                              Delta
  )
{
  UINTN   Y, Row;
  UINT32  Colour;
  UINT8   *Buf = (UINT8 *)BltBuffer;

  if (W == 0 || H == 0 || W > mW || H > mH || (UINTN)Op >= EfiGraphicsOutputBltOperationMax) {
    return EFI_INVALID_PARAMETER;
  }
  if (Delta == 0) {
    Delta = W * 4;
  }

  switch (Op) {
    case EfiBltVideoFill:
      if (Buf == NULL || Dx > mW - W || Dy > mH - H) {
        return EFI_INVALID_PARAMETER;
      }
      CopyMem (&Colour, Buf, 4);
      Colour &= 0x00FFFFFF;
      for (Y = 0; Y < H; Y++) {
        SetMem32 (mFb + (Dy + Y) * mStride + Dx, W * 4, Colour);
        FlushRange (mFb + (Dy + Y) * mStride + Dx, W * 4);
      }
      break;

    case EfiBltVideoToBltBuffer:
      if (Buf == NULL || Sx > mW - W || Sy > mH - H) {
        return EFI_INVALID_PARAMETER;
      }
      for (Y = 0; Y < H; Y++) {
        CopyMem (Buf + (Dy + Y) * Delta + Dx * 4, mFb + (Sy + Y) * mStride + Sx, W * 4);
      }
      break;

    case EfiBltBufferToVideo:
      if (Buf == NULL || Dx > mW - W || Dy > mH - H) {
        return EFI_INVALID_PARAMETER;
      }
      for (Y = 0; Y < H; Y++) {
        CopyMem (mFb + (Dy + Y) * mStride + Dx, Buf + (Sy + Y) * Delta + Sx * 4, W * 4);
        FlushRange (mFb + (Dy + Y) * mStride + Dx, W * 4);
      }
      break;

    case EfiBltVideoToVideo:
      if (Sx > mW - W || Sy > mH - H || Dx > mW - W || Dy > mH - H) {
        return EFI_INVALID_PARAMETER;
      }
      for (Y = 0; Y < H; Y++) {
        Row = (Dy <= Sy) ? Y : (H - 1 - Y);     // safe order for overlapping scroll
        CopyMem (mFb + (Dy + Row) * mStride + Dx, mFb + (Sy + Row) * mStride + Sx, W * 4);
        FlushRange (mFb + (Dy + Row) * mStride + Dx, W * 4);
      }
      break;

    default:
      return EFI_INVALID_PARAMETER;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
KirinGopPinEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                     S;
  EFI_HANDLE                     *Handles = NULL;
  UINTN                          Count = 0, I, Patched = 0;
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *G, *Phys = NULL;
  EFI_DEVICE_PATH_PROTOCOL       *Dp;
  EFI_PHYSICAL_ADDRESS           Now, Pin;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Green = { 0x00, 0xC0, 0x00, 0x00 };
  EFI_CPU_ARCH_PROTOCOL          *Cpu = NULL;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE     *NewMode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *NewInfo;
  EFI_STATUS                     WcStatus = EFI_NOT_FOUND;

  Print (L"KirinGopPinV7.2 variant %s\n", VARIANT);

  S = gBS->LocateHandleBuffer (ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (S)) {
    Print (L"No GOP: %r\n", S);
    return S;
  }

  // The physical GOP is the one that has a device path.
  for (I = 0; I < Count; I++) {
    G  = NULL;
    Dp = NULL;
    if (EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiGraphicsOutputProtocolGuid, (VOID **)&G)) || G == NULL) {
      continue;
    }
    if (EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiDevicePathProtocolGuid, (VOID **)&Dp)) || Dp == NULL) {
      continue;
    }
    if (G->Mode != NULL && G->Mode->Info != NULL && G->Mode->FrameBufferBase != 0) {
      Phys = G;
      break;
    }
  }
  if (Phys == NULL) {
    Print (L"Refused: no physical GOP with a framebuffer.\n");
    FreePool (Handles);
    return EFI_UNSUPPORTED;
  }
  if (Phys->Blt == PinBlt) {
    Print (L"Refused: already pinned in this boot. Cold boot first.\n");
    FreePool (Handles);
    return EFI_ALREADY_STARTED;
  }

  Now = Phys->Mode->FrameBufferBase;
  if (Now != BUF0 && Now != BUF1) {
    Print (L"Refused: FrameBufferBase %lx is neither %lx nor %lx.\n", (UINT64)Now, BUF0, BUF1);
    FreePool (Handles);
    return EFI_UNSUPPORTED;
  }
#ifdef KIRIN_PIN_OTHER
  Pin = (Now == BUF0) ? BUF1 : BUF0;
#else
  Pin = Now;
#endif

  mW      = Phys->Mode->Info->HorizontalResolution;
  mH      = Phys->Mode->Info->VerticalResolution;
  mStride = Phys->Mode->Info->PixelsPerScanLine;
  if (mW == 0 || mH == 0 || mStride < mW ||
      (UINT64)mStride * mH * 4 > (UINT64)Phys->Mode->FrameBufferSize) {
    Print (L"Refused: geometry %ux%u stride %u does not fit framebuffer size %lx.\n",
           mW, mH, mStride, (UINT64)Phys->Mode->FrameBufferSize);
    FreePool (Handles);
    return EFI_UNSUPPORTED;
  }
  mFb = (UINT32 *)(UINTN)Pin;

  ZeroMem (&mInfo, sizeof (mInfo));
  mInfo.Version              = 0;
  mInfo.HorizontalResolution = mW;
  mInfo.VerticalResolution   = mH;
  mInfo.PixelFormat          = PixelBlueGreenRedReserved8BitPerColor;
  mInfo.PixelsPerScanLine    = mStride;

  Print (L"Reported now %lx, pinning %lx, %ux%u stride %u. Patching %u GOP instance(s)...\n",
         (UINT64)Now, (UINT64)Pin, mW, mH, mStride, (UINT32)Count);

  // Seed the pinned buffer with what is on screen now, so the picture survives the switch.
  if (Pin != Now) {
    CopyMem (mFb, (VOID *)(UINTN)Now, (UINTN)mStride * mH * 4);
  }

  // Everything already drawn must be in RAM before the mapping is changed.
  FlushRange (mFb, (UINTN)mStride * mH * 4);

  // Ask the firmware to map the buffer write-combined (uncached). Windows Boot
  // Manager and winload write the framebuffer themselves, without any flush, so
  // this is what makes THEIR picture (logo, text) come out clean as well.
  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&Cpu)) && Cpu != NULL) {
    WcStatus = Cpu->SetMemoryAttributes (Cpu, Pin, ((UINT64)mStride * mH * 4 + 0xFFF) & ~0xFFFULL, EFI_MEMORY_WC);
  }

  // From here on the firmware display code is never entered again.
  for (I = 0; I < Count; I++) {
    G = NULL;
    if (EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiGraphicsOutputProtocolGuid, (VOID **)&G)) ||
        G == NULL || G->Mode == NULL || G->Mode->Info == NULL) {
      continue;
    }
    // Give every instance its OWN mode block. The firmware keeps updating the
    // FrameBufferBase inside its private mode block (seen on the device: it went
    // back to the other buffer right after pinning), so clients must no longer
    // look at that block at all.
    NewMode = AllocateZeroPool (sizeof (*NewMode));
    NewInfo = AllocateCopyPool (sizeof (mInfo), &mInfo);
    if (NewMode == NULL || NewInfo == NULL) {
      continue;
    }
    NewMode->MaxMode         = 1;
    NewMode->Mode            = 0;
    NewMode->Info            = NewInfo;
    NewMode->SizeOfInfo      = sizeof (mInfo);
    NewMode->FrameBufferBase = Pin;
    NewMode->FrameBufferSize = (UINTN)mStride * mH * 4;
    G->QueryMode = PinQueryMode;
    G->SetMode   = PinSetMode;
    G->Blt       = PinBlt;
    G->Mode      = NewMode;
    Patched++;
  }
  FreePool (Handles);

  // Self test through the new path.
  PinBlt (Phys, &Green, EfiBltVideoFill, 0, 0, 0, 0, mW, 64, 0);
  Print (L"PIN OK (%u patched). A GREEN strip at the top + this line visible = correct buffer.\n", (UINT32)Patched);
  Print (L"Uncached mapping of the framebuffer: %r\n", WcStatus);
  return EFI_SUCCESS;
}
