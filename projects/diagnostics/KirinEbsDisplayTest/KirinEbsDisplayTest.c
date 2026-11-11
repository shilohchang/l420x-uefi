/** KirinEbsDisplayTest - does the L420X panel stay alive after ExitBootServices?

  Run AFTER `load KirinGopPinA.efi` (no ACPI, no Windows needed).
  Optional argument /skip : see below.

  Phase 1 (firmware still running): top half of the screen turns WHITE, 4 s.
  Then this program calls ExitBootServices itself, exactly like an OS loader.
  Phase 2 (firmware gone): bottom half cycles RED -> GREEN -> BLUE once per
  second for 20 s, written straight into the framebuffer.
  Then it asks the firmware runtime service for a cold reset.

  Reading the result:
    bottom half cycles colours      -> the panel survives ExitBootServices;
                                       the black screen is caused inside Windows.
    screen goes black after phase 1 -> the firmware switches the display off
                                       during ExitBootServices.
**/
#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>

STATIC UINT32  *mFb;
STATIC UINT32  mW, mH, mStride;

STATIC UINT64 ReadCounter (VOID) {
  UINT64  V;
  __asm__ __volatile__ ("isb\n\tmrs %0, cntvct_el0" : "=r" (V));
  return V;
}

STATIC UINT64 ReadFrequency (VOID) {
  UINT64  V;
  __asm__ __volatile__ ("mrs %0, cntfrq_el0" : "=r" (V));
  return V;
}

STATIC VOID FlushRange (VOID *Start, UINTN Length) {
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

STATIC VOID FillRect (UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Colour) {
  UINT32  Row;
  for (Row = Y; Row < Y + H && Row < mH; Row++) {
    SetMem32 (mFb + (UINTN)Row * mStride + X, (UINTN)W * 4, Colour);
    FlushRange (mFb + (UINTN)Row * mStride + X, (UINTN)W * 4);   // push pixels out of the CPU cache
  }
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  STATIC CONST UINT32            Cycle[3] = { 0x00FF0000, 0x0000FF00, 0x000000FF };  // R, G, B as BGRX
  EFI_STATUS                     S;
  EFI_HANDLE                     *Handles = NULL;
  UINTN                          Count = 0, I;
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *G, *Gop = NULL;
  EFI_DEVICE_PATH_PROTOCOL       *Dp;
  EFI_MEMORY_DESCRIPTOR          *Map;
  UINTN                          MapCap, MapSize, Key, DescSize;
  UINT32                         DescVer;
  UINT64                         Freq, Start, Now;
  UINT32                         Sec, Bar;
  EFI_LOADED_IMAGE_PROTOCOL      *Li = NULL;
  BOOLEAN                        Skip = FALSE;
  EFI_TPL                        Level = TPL_NOTIFY;

  // Argument /skip : raise the task priority to TPL_NOTIFY before ExitBootServices.
  // Firmware drivers' "exit boot services" callbacks are then queued but never run,
  // so a display driver that powers the panel down in such a callback cannot do it.
  if (!EFI_ERROR (gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Li)) &&
      Li != NULL && Li->LoadOptions != NULL && Li->LoadOptionsSize >= 6 * sizeof (CHAR16) &&
      ((CHAR16 *)Li->LoadOptions)[Li->LoadOptionsSize / sizeof (CHAR16) - 1] == 0) {
    Skip = (StrStr ((CHAR16 *)Li->LoadOptions, L"/skip") != NULL);
    if (StrStr ((CHAR16 *)Li->LoadOptions, L"/callback") != NULL) {   // hold back TPL_CALLBACK-level notifications only
      Skip  = TRUE;
      Level = TPL_CALLBACK;
    }
  }

  S = gBS->LocateHandleBuffer (ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles);
  for (I = 0; !EFI_ERROR (S) && I < Count; I++) {
    G = NULL; Dp = NULL;
    if (!EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiGraphicsOutputProtocolGuid, (VOID **)&G)) &&
        !EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiDevicePathProtocolGuid, (VOID **)&Dp)) &&
        G != NULL && G->Mode != NULL && G->Mode->Info != NULL && G->Mode->FrameBufferBase != 0) {
      Gop = G;
      break;
    }
  }
  if (Gop == NULL) {
    Print (L"No physical GOP with a framebuffer.\n");
    return EFI_UNSUPPORTED;
  }
  mFb     = (UINT32 *)(UINTN)Gop->Mode->FrameBufferBase;
  mW      = Gop->Mode->Info->HorizontalResolution;
  mH      = Gop->Mode->Info->VerticalResolution;
  mStride = Gop->Mode->Info->PixelsPerScanLine;
  Freq    = ReadFrequency ();
  if (Freq == 0) {
    Freq = 1920000;
  }

  Print (L"KirinEbsDisplayTest: fb=%lx %ux%u format=%u\n", (UINT64)(UINTN)mFb, mW, mH,
         (UINT32)Gop->Mode->Info->PixelFormat);
  Print (L"Mode: %s\n", !Skip ? L"normal" : (Level == TPL_CALLBACK ? L"/callback (only CALLBACK-level exit notifications held back)" : L"/skip (all exit notifications held back)"));
  Print (L"Phase 1: top half WHITE for 4 s. Then ExitBootServices.\n");
  Print (L"Phase 2: bottom half cycles RED/GREEN/BLUE for 20 s, then the machine resets.\n");
  gBS->Stall (3 * 1000 * 1000);

  // Buffer for the final memory map must exist before the last GetMemoryMap call.
  MapCap = 0;
  gBS->GetMemoryMap (&MapCap, NULL, &Key, &DescSize, &DescVer);
  MapCap += 64 * DescSize + 4096;
  Map = AllocatePool (MapCap);
  if (Map == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  FillRect (0, 0, mW, mH / 2, 0x00FFFFFF);
  gBS->Stall (4 * 1000 * 1000);

  // No console output and no allocations from here to ExitBootServices.
  if (Skip) {
    gBS->RaiseTPL (Level);             // deliberately never restored
  }
  MapSize = MapCap;
  S = gBS->GetMemoryMap (&MapSize, Map, &Key, &DescSize, &DescVer);
  if (!EFI_ERROR (S)) {
    S = gBS->ExitBootServices (ImageHandle, Key);
  }
  if (EFI_ERROR (S)) {                 // one retry is allowed by the UEFI specification
    MapSize = MapCap;
    S = gBS->GetMemoryMap (&MapSize, Map, &Key, &DescSize, &DescVer);
    if (!EFI_ERROR (S)) {
      S = gBS->ExitBootServices (ImageHandle, Key);
    }
  }
  if (EFI_ERROR (S)) {
    // Boot services may be half shut down: only signal on the framebuffer (top half YELLOW) and stop.
    FillRect (0, 0, mW, mH / 2, 0x00FFFF00);
    for ( ; ;) {
    }
  }

  // ---- Firmware boot services are gone. Only direct framebuffer writes from here. ----
  Start = ReadCounter ();
  for (Sec = 0; Sec < 20; Sec++) {
    FillRect (0, mH / 2, mW, mH / 2, Cycle[Sec % 3]);
    Bar = (mW / 20) * Sec;                                   // white marker walks left to right
    FillRect (Bar, mH / 2, mW / 20, mH / 8, 0x00FFFFFF);
    do {
      Now = ReadCounter ();
    } while (Now - Start < (UINT64)(Sec + 1) * Freq);
  }

  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  for ( ; ;) {
  }
}
