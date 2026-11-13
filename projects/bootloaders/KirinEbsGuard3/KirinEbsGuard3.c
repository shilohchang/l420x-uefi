/** KirinEbsGuard3 - keep the L420X panel alive across ExitBootServices
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

  Version 3 adds the multi-processor fix. Windows' loader leaves a small
  resident routine in memory that every CPU uses to step down from EL2 to EL1.
  For the boot CPU a separate code path clears HCR_EL2.TSC ("trap the OS's SMC
  calls into EL2"); the resident routine used by secondary CPUs does not. On
  ordinary firmware that does not matter because secondaries arrive with a
  zero HCR_EL2. On this machine the secure firmware delivers every secondary
  with HCR_EL2 = 0x30080080007 (TSC set), and reloads that at each CPU_ON, so
  each secondary would run Windows with its SMC calls trapped: it comes
  "online" and then hangs (bugcheck 0x1D5 after 360 s).
  The wrapper therefore finds that routine in memory (unique 5-instruction
  sequence) and replaces its first instruction "mrs x6, HCR_EL2" by
  "mov x6, #0", so the routine always ends up writing HCR_EL2 = 0x80000000
  on every CPU. Done in RAM only, at hand-off time; no file is modified.

  Marker squares, top-right corner:
    YELLOW : wrapper entered
    GREEN  : at least one boot-time handler was neutralised (WHITE = none found)
    MAGENTA: the EL2->EL1 routine was found and patched (GREY = not found)
    CYAN   : firmware returned SUCCESS (RED = error; loader normally retries)
    BLUE   : I2C buses left ready for the OS (ORANGE = read-back mismatch)

  Version 3.1 adds the I2C hand-off. The firmware gates the keyboard bus clock
  and parks its two pins between its own transfers, so the OS would find a dead
  bus. Once the firmware has finished ExitBootServices (nothing of it runs any
  more) the wrapper switches the two I2C bus clocks on and puts the keyboard
  bus pins (GPIO230/231) into their I2C function - exactly the state in which
  KirinI2cXfer2 read the keyboard's HID descriptor. The Windows I2C driver
  therefore never has to touch clock or pin registers.

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

  if (mFb == NULL || mW < 6 * SQUARE || mH < 2 * SQUARE) {
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

// mrs x6,HCR_EL2 ; mov x7,#0x39 ; orr x7,x7,#0x8000000 ; bic x6,x6,x7 ; orr x6,x6,#0x80000000
// Stored XOR-ed so that this table itself can never match the search.
#define PAT_KEY  0x5A5A5A5AU
STATIC CONST UINT32  mPattern[5] = {
  0xD53C1106U ^ PAT_KEY, 0xD2800727U ^ PAT_KEY, 0xB26500E7U ^ PAT_KEY, 0x8A2700C6U ^ PAT_KEY, 0xB26100C6U ^ PAT_KEY
};
#define INSN_MOV_X6_ZERO  0xD2800006U

STATIC UINTN  mPatched;

#define CRG_PEREN1     0xFFB85010ULL     // write-1-to-set clock enable
#define CRG_PERCLKEN1  0xFFB85018ULL     // status
#define I2C_CLOCKS     0xC0000000U       // bit 31 = keyboard bus (i2c7), bit 30 = touchpad bus (i2c6)

STATIC CONST UINT64  mKbdPad[2] = { 0xFB229794ULL, 0xFB229798ULL };   // pad function, 4 = I2C
STATIC CONST UINT64  mKbdCfg[2] = { 0xFB229F94ULL, 0xFB229F98ULL };   // electrical config, bit 0 = pull-up

// Runs after ExitBootServices succeeded: plain register writes only.
STATIC
BOOLEAN
PrepareI2c (
  VOID
  )
{
  UINTN    I;
  UINT32   V;
  BOOLEAN  Ok;

  *(volatile UINT32 *)(UINTN)CRG_PEREN1 = I2C_CLOCKS;
  for (I = 0; I < 2; I++) {
    V = *(volatile UINT32 *)(UINTN)mKbdCfg[I];
    if ((V & 3U) != 1U) {
      *(volatile UINT32 *)(UINTN)mKbdCfg[I] = (V & ~3U) | 1U;
    }
    if (*(volatile UINT32 *)(UINTN)mKbdPad[I] != 4U) {
      *(volatile UINT32 *)(UINTN)mKbdPad[I] = 4U;
    }
  }
  Ok = (BOOLEAN)((*(volatile UINT32 *)(UINTN)CRG_PERCLKEN1 & I2C_CLOCKS) == I2C_CLOCKS);
  for (I = 0; I < 2; I++) {
    Ok = (BOOLEAN)(Ok && *(volatile UINT32 *)(UINTN)mKbdPad[I] == 4U);
  }
  return Ok;
}

/* Search the memory an OS loader can own for the routine and patch each copy.
   Patch = FALSE only counts. */
STATIC
UINTN
SweepEl2Routine (
  IN BOOLEAN  Patch
  )
{
  UINTN                  MapSize, Key, DescSize, I, Found = 0;
  UINT32                 Ver;
  EFI_MEMORY_DESCRIPTOR  *D;
  volatile UINT32        *P;
  UINT32                 *End;
  UINT32                 First = mPattern[0] ^ PAT_KEY;

  MapSize = mMapCapacity;
  if (mMap == NULL || EFI_ERROR (gBS->GetMemoryMap (&MapSize, mMap, &Key, &DescSize, &Ver)) || DescSize == 0) {
    return 0;
  }
  for (I = 0; I + DescSize <= MapSize; I += DescSize) {
    D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)mMap + I);
    // Only memory that an OS loader can own: loader code/data, boot-services
    // code/data and OS-defined types. Never free, reserved, runtime or device memory.
    if (D->NumberOfPages == 0 ||
        !(D->Type == EfiLoaderCode || D->Type == EfiLoaderData ||
          D->Type == EfiBootServicesCode || D->Type == EfiBootServicesData ||
          D->Type >= 0x70000000U)) {
      continue;
    }
    P   = (volatile UINT32 *)(UINTN)D->PhysicalStart;
    End = (UINT32 *)(UINTN)(D->PhysicalStart + LShiftU64 (D->NumberOfPages, 12) - sizeof (mPattern));
    for ( ; (UINT32 *)P <= End; P++) {
      if (P[0] != First) {
        continue;
      }
      if (P[1] != (mPattern[1] ^ PAT_KEY) || P[2] != (mPattern[2] ^ PAT_KEY) ||
          P[3] != (mPattern[3] ^ PAT_KEY) || P[4] != (mPattern[4] ^ PAT_KEY)) {
        continue;
      }
      Found++;
      if (Patch) {
        P[0] = INSN_MOV_X6_ZERO;
        __asm__ __volatile__ ("dc cvau, %0" : : "r" ((UINTN)P));
      }
    }
  }
  if (Patch && Found != 0) {
    __asm__ __volatile__ ("dsb ish\n\tic ialluis\n\tdsb ish\n\tisb");
  }
  return Found;
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
  mPatched += SweepEl2Routine (TRUE);
  Square (2, mPatched != 0 ? 0x00FF00FF : 0x00606060);         // magenta / grey
  Status = mOriginalExit (ImageHandle, MapKey);
  Square (3, EFI_ERROR (Status) ? 0x00FF0000 : 0x0000FFFF);   // red / cyan
  if (!EFI_ERROR (Status)) {
    Square (4, PrepareI2c () ? 0x000000FF : 0x00FF8000);      // blue / orange
  }
  return Status;
}

EFI_STATUS
EFIAPI
KirinEbsGuard3Entry (
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
    Print (L"KirinEbsGuard3: already installed in this boot.\n");
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
  Print (L"KirinEbsGuard3: exit-boot-services handlers found now: %u boot-time (will be neutralised), %u runtime (kept).\n",
         (UINT32)Boot, (UINT32)Kept);
  if (Boot == 0 && Kept == 0) {
    Print (L"WARNING: none found - this firmware may use a different event layout. Tell Claude.\n");
  }

  mOriginalExit         = gBS->ExitBootServices;
  gBS->ExitBootServices = GuardExitBootServices;
  gBS->Hdr.CRC32 = 0;
  gBS->CalculateCrc32 (gBS, gBS->Hdr.HeaderSize, &Crc);
  gBS->Hdr.CRC32 = Crc;

  Print (L"KirinEbsGuard3 v3.1 installed (multi-processor fix + I2C hand-off). Marker framebuffer=%lx\n", (UINT64)(UINTN)mFb);
  return EFI_SUCCESS;
}
