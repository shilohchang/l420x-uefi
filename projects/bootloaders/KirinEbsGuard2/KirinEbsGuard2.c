/** KirinEbsGuard2 - keep the L420X panel alive across ExitBootServices
    WITHOUT starving the firmware's runtime drivers.

  History on this device:
    * the stock firmware powers the panel down from an "exit boot services"
      notification;
    * holding back ALL such notifications keeps the panel, but the firmware's
      runtime drivers then never learn that boot services are gone and Windows
      bugchecks (0x1E / C0000005 reading gBS+0x18) on its first runtime call;
    * the panel driver and the runtime drivers use the same priority level, so
      they cannot be separated by priority.

  So this version separates them by OWNER instead. When the OS loader calls
  ExitBootServices, the wrapper walks the firmware's pending "exit boot
  services" event records and, for every record whose handler does NOT live in
  runtime-services code, swaps the handler for an empty function. Runtime
  drivers keep their notification; boot-time drivers (panel, USB, ...) do not
  get to run their shutdown code. No priority tricks any more.

  Marker squares, top-right corner:
    YELLOW : wrapper entered
    GREEN  : at least one boot-time handler was neutralised (WHITE = none found)
    CYAN   : firmware returned SUCCESS (RED = error; loader normally retries)

  Load after KirinGopPinA, with `load -nc`. Cannot be unloaded; cold boot to undo.
**/
#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DevicePath.h>
#include <Guid/EventGroup.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#define SQUARE  160U

// Layout of the DXE core's private event record (EDK II, 64-bit). Only the
// leading fields are needed; this has been stable for many years.
#define KIRIN_EVENT_SIGNATURE  0x746E7665ULL        // 'e','v','n','t'
typedef struct {
  UINT64               Signature;
  UINT32               Type;
  UINT32               SignalCount;
  LIST_ENTRY           SignalLink;
  UINT64               NotifyTpl;
  EFI_EVENT_NOTIFY     NotifyFunction;
  VOID                 *NotifyContext;
  EFI_GUID             EventGroup;
} KIRIN_EVENT_HEAD;

STATIC EFI_EXIT_BOOT_SERVICES  mOriginalExit;
STATIC UINT32                  *mFb;
STATIC UINT32                  mW, mH, mStride;
STATIC EFI_MEMORY_DESCRIPTOR   *mMap;
STATIC UINTN                   mMapCapacity;

STATIC
VOID
EFIAPI
EmptyNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
}

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

  if (mFb == NULL || mW < 5 * SQUARE || mH < 2 * SQUARE) {
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

// Memory type of the map entry that contains Addr, or EfiMaxMemoryType.
STATIC
UINT32
TypeOf (
  IN UINT64  Addr,
  IN UINTN   MapSize,
  IN UINTN   DescSize
  )
{
  UINTN                  I;
  EFI_MEMORY_DESCRIPTOR  *D;

  for (I = 0; I + DescSize <= MapSize; I += DescSize) {
    D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)mMap + I);
    if (Addr >= D->PhysicalStart && Addr < D->PhysicalStart + LShiftU64 (D->NumberOfPages, 12)) {
      return D->Type;
    }
  }
  return EfiMaxMemoryType;
}

/* Walk every boot-services-data region looking for "exit boot services" event
   records. Patch = FALSE only counts. Returns the number of boot-time handlers
   (found or neutralised); *RuntimeKept gets the number left alone. */
STATIC
UINTN
SweepEvents (
  IN  BOOLEAN  Patch,
  OUT UINTN    *RuntimeKept
  )
{
  UINTN                  MapSize, Key, DescSize, I, Boot = 0;
  UINT32                 Ver, HandlerType;
  EFI_MEMORY_DESCRIPTOR  *D;
  UINT64                 *P, *End;
  KIRIN_EVENT_HEAD       *E;

  *RuntimeKept = 0;
  MapSize = mMapCapacity;
  if (mMap == NULL || EFI_ERROR (gBS->GetMemoryMap (&MapSize, mMap, &Key, &DescSize, &Ver)) || DescSize == 0) {
    return 0;
  }
  for (I = 0; I + DescSize <= MapSize; I += DescSize) {
    D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)mMap + I);
    if (D->Type != EfiBootServicesData || D->NumberOfPages == 0) {
      continue;
    }
    P   = (UINT64 *)(UINTN)D->PhysicalStart;
    End = (UINT64 *)(UINTN)(D->PhysicalStart + LShiftU64 (D->NumberOfPages, 12) - sizeof (KIRIN_EVENT_HEAD));
    for ( ; P <= End; P++) {
      if (*P != KIRIN_EVENT_SIGNATURE) {
        continue;
      }
      E = (KIRIN_EVENT_HEAD *)P;
      if (E->NotifyFunction == NULL || E->NotifyFunction == EmptyNotify ||
          E->NotifyTpl == 0 || E->NotifyTpl > TPL_HIGH_LEVEL ||
          E->SignalLink.ForwardLink == NULL || E->SignalLink.BackLink == NULL ||
          !CompareGuid (&E->EventGroup, &gEfiEventExitBootServicesGuid)) {
        continue;
      }
      HandlerType = TypeOf ((UINT64)(UINTN)E->NotifyFunction, MapSize, DescSize);
      if (HandlerType == EfiRuntimeServicesCode) {
        (*RuntimeKept)++;                      // runtime driver: must still be told
        continue;
      }
      if (HandlerType != EfiBootServicesCode && HandlerType != EfiLoaderCode) {
        continue;                              // not a plausible handler address: stale record
      }
      Boot++;
      if (Patch) {
        E->NotifyFunction = EmptyNotify;
      }
    }
  }
  return Boot;
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
  UINTN       Kept;

  Square (0, 0x00FFFF00);                                     // yellow: entered
  Square (1, SweepEvents (TRUE, &Kept) != 0 ? 0x0000C000 : 0x00FFFFFF);   // green / white
  Status = mOriginalExit (ImageHandle, MapKey);
  Square (2, EFI_ERROR (Status) ? 0x00FF0000 : 0x0000FFFF);   // red / cyan
  return Status;
}

EFI_STATUS
EFIAPI
KirinEbsGuard2Entry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE                    *Handles = NULL;
  UINTN                         Count = 0, I, Key, DescSize, Boot, Kept;
  UINT32                        Ver, Crc = 0;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *G;
  EFI_DEVICE_PATH_PROTOCOL      *Dp;

  if (gBS->ExitBootServices == GuardExitBootServices) {
    Print (L"KirinEbsGuard2: already installed in this boot.\n");
    return EFI_ALREADY_STARTED;
  }

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

  // The memory map buffer must exist beforehand: nothing may be allocated
  // inside the wrapper. The OS loader adds many entries, so be generous.
  mMapCapacity = 0;
  gBS->GetMemoryMap (&mMapCapacity, NULL, &Key, &DescSize, &Ver);
  mMapCapacity = mMapCapacity * 4 + 0x10000;
  mMap = AllocatePool (mMapCapacity);
  if (mMap == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Boot = SweepEvents (FALSE, &Kept);     // dry run, only to report
  Print (L"KirinEbsGuard2: exit-boot-services handlers found now: %u boot-time (will be neutralised), %u runtime (kept).\n",
         (UINT32)Boot, (UINT32)Kept);
  if (Boot == 0 && Kept == 0) {
    Print (L"WARNING: none found - this firmware may use a different event layout. Tell Claude.\n");
  }

  mOriginalExit         = gBS->ExitBootServices;
  gBS->ExitBootServices = GuardExitBootServices;
  gBS->Hdr.CRC32 = 0;
  gBS->CalculateCrc32 (gBS, gBS->Hdr.HeaderSize, &Crc);
  gBS->Hdr.CRC32 = Crc;

  Print (L"KirinEbsGuard2 installed. Marker framebuffer=%lx\n", (UINT64)(UINTN)mFb);
  return EFI_SUCCESS;
}
