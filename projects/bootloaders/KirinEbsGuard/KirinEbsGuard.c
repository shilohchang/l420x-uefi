/** KirinEbsGuard 1.2 - keep the L420X panel alive across ExitBootServices,
    and show on screen what happened.

  The stock firmware powers the display down from an "exit boot services"
  notification. Entering ExitBootServices at TPL_NOTIFY leaves those
  notifications queued and never dispatched (verified with KirinEbsDisplayTest).

  This resident driver wraps gBS->ExitBootServices so the OS loader's call is
  made at TPL_NOTIFY, and paints two marker squares in the top-right corner:

    YELLOW square : the loader has called ExitBootServices (wrapper entered)
    CYAN square   : the firmware returned SUCCESS; the OS now owns the machine
    RED square    : the firmware returned an error (loader will normally retry)

  If the squares appear and are later wiped, the OS itself redrew the screen.

  Load order: AcpiTableDxe, ACPI loader, KirinGopPinA, KirinEbsGuard, then boot.
  Cannot be unloaded; cold boot to undo.
**/
#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DevicePath.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#define SQUARE  160U

// Which firmware "exit boot services" notifications are held back:
//   TPL_CALLBACK : only those registered at TPL_CALLBACK; TPL_NOTIFY ones still run
//                  (firmware RUNTIME drivers use TPL_NOTIFY to learn that boot
//                  services are gone - they must get it, or the OS crashes later
//                  inside a runtime service: seen as bugcheck 0x1E/C0000005
//                  reading gBS+0x18 = RaiseTPL).
//   TPL_NOTIFY   : everything (first experiment; breaks runtime services).
#ifndef GUARD_TPL
#define GUARD_TPL  TPL_CALLBACK
#endif

STATIC EFI_EXIT_BOOT_SERVICES  mOriginalExit;
STATIC BOOLEAN                 mRaised;
STATIC UINT32                  *mFb;
STATIC UINT32                  mW, mH, mStride;

STATIC
VOID
Square (
  IN UINT32  Index,
  IN UINT32  Colour
  )
{
  UINT32  X, Y, *Row;
  UINT64  Ctr;
  UINTN   Line, Addr, End;

  if (mFb == NULL || mW < 4 * SQUARE || mH < 2 * SQUARE) {
    return;
  }
  X = mW - (Index + 1) * (SQUARE + 16);
  __asm__ __volatile__ ("mrs %0, ctr_el0" : "=r" (Ctr));
  Line = (UINTN)4 << ((Ctr >> 16) & 0xF);
  for (Y = 16; Y < 16 + SQUARE; Y++) {
    Row = mFb + (UINTN)Y * mStride + X;
    SetMem32 (Row, SQUARE * 4, Colour);
    End = (UINTN)Row + SQUARE * 4;
    for (Addr = (UINTN)Row & ~(Line - 1); Addr < End; Addr += Line) {
      __asm__ __volatile__ ("dc cvac, %0" : : "r" (Addr));
    }
  }
  __asm__ __volatile__ ("dsb sy");
}

STATIC
EFI_STATUS
EFIAPI
GuardExitBootServices (
  IN EFI_HANDLE  ImageHandle,
  IN UINTN       MapKey
  )
{
  EFI_STATUS  Status;

  Square (0, 0x00FFFF00);                 // yellow: wrapper entered
  if (!mRaised) {                         // loader may call twice; raise once, never restore
    gBS->RaiseTPL (GUARD_TPL);
    mRaised = TRUE;
  }
  Status = mOriginalExit (ImageHandle, MapKey);
  Square (1, EFI_ERROR (Status) ? 0x00FF0000 : 0x0000FFFF);   // red on error, cyan on success
  return Status;
}

EFI_STATUS
EFIAPI
KirinEbsGuardEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE                    *Handles = NULL;
  UINTN                         Count = 0, I;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *G;
  EFI_DEVICE_PATH_PROTOCOL      *Dp;
  UINT32                        Crc = 0;

  if (gBS->ExitBootServices == GuardExitBootServices) {
    Print (L"KirinEbsGuard: already installed in this boot.\n");
    return EFI_ALREADY_STARTED;
  }

  // Remember where the (already pinned) framebuffer is, for the marker squares.
  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &Count, &Handles))) {
    for (I = 0; I < Count; I++) {
      G = NULL; Dp = NULL;
      if (!EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiGraphicsOutputProtocolGuid, (VOID **)&G)) &&
          !EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiDevicePathProtocolGuid, (VOID **)&Dp)) &&
          G != NULL && G->Mode != NULL && G->Mode->Info != NULL && G->Mode->FrameBufferBase != 0) {
        mFb     = (UINT32 *)(UINTN)G->Mode->FrameBufferBase;
        mW      = G->Mode->Info->HorizontalResolution;
        mH      = G->Mode->Info->VerticalResolution;
        mStride = G->Mode->Info->PixelsPerScanLine;
        break;
      }
    }
    FreePool (Handles);
  }

  mOriginalExit         = gBS->ExitBootServices;
  gBS->ExitBootServices = GuardExitBootServices;
  gBS->Hdr.CRC32 = 0;
  gBS->CalculateCrc32 (gBS, gBS->Hdr.HeaderSize, &Crc);
  gBS->Hdr.CRC32 = Crc;

  Print (L"KirinEbsGuard 1.2 installed, hold level %u (8=CALLBACK 16=NOTIFY). Marker framebuffer=%lx %ux%u\n", (UINT32)GUARD_TPL, (UINT64)(UINTN)mFb, mW, mH);
  return EFI_SUCCESS;
}
