#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DevicePathToText.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

STATIC CONST CHAR16 *FormatName (EFI_GRAPHICS_PIXEL_FORMAT Format) {
  switch (Format) {
    case PixelRedGreenBlueReserved8BitPerColor: return L"RGB8";
    case PixelBlueGreenRedReserved8BitPerColor: return L"BGRA8";
    case PixelBitMask: return L"BITMASK";
    case PixelBltOnly: return L"BLT_ONLY";
    default: return L"UNKNOWN";
  }
}

STATIC VOID PrintModeInfo (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info, BOOLEAN IsCurrent) {
  BOOLEAN ShapeOk;
  if (Info == NULL) {
    Print(L"  No mode info\n");
    return;
  }
  ShapeOk = (Info->HorizontalResolution > 0 &&
             Info->VerticalResolution > 0 &&
             Info->PixelsPerScanLine == Info->HorizontalResolution);
  Print(L"  %sResolution=%ux%u Stride=%u PixelFormat=%u (%s) Version=%u\n",
        IsCurrent ? L"[CURRENT] " : L"",
        Info->HorizontalResolution, Info->VerticalResolution,
        Info->PixelsPerScanLine, (UINT32)Info->PixelFormat,
        FormatName(Info->PixelFormat), Info->Version);
  if (Info->PixelFormat == PixelBitMask) {
    Print(L"    Masks R=%08x G=%08x B=%08x A=%08x\n",
          Info->PixelInformation.RedMask, Info->PixelInformation.GreenMask,
          Info->PixelInformation.BlueMask, Info->PixelInformation.ReservedMask);
  }
  Print(L"    Windows mode layout: %s (native resolution not verified)\n",
        (ShapeOk && Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor)
        ? L"CANDIDATE" : L"INCOMPATIBLE");
}

STATIC VOID PrintMemoryDescriptor (EFI_PHYSICAL_ADDRESS Base) {
  EFI_STATUS Status;
  UINTN MapSize = 0, MapKey = 0, DescSize = 0;
  UINT32 DescVersion = 0;
  EFI_MEMORY_DESCRIPTOR *Map = NULL;
  UINTN Index;
  BOOLEAN Found = FALSE;

  Status = gBS->GetMemoryMap(&MapSize, NULL, &MapKey, &DescSize, &DescVersion);
  if (Status != EFI_BUFFER_TOO_SMALL || DescSize == 0) {
    Print(L"  Memory map query: %r\n", Status);
    return;
  }
  MapSize += 16 * DescSize;
  Map = AllocatePool(MapSize);
  if (Map == NULL) {
    Print(L"  Memory map allocation failed\n");
    return;
  }
  Status = gBS->GetMemoryMap(&MapSize, Map, &MapKey, &DescSize, &DescVersion);
  if (EFI_ERROR(Status)) {
    Print(L"  Memory map read: %r\n", Status);
    FreePool(Map);
    return;
  }
  for (Index = 0; Index < MapSize / DescSize; Index++) {
    EFI_MEMORY_DESCRIPTOR *D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Index * DescSize);
    UINT64 Start = D->PhysicalStart;
    UINT64 Size;
    if (D->NumberOfPages > (MAX_UINT64 / EFI_PAGE_SIZE)) continue;
    Size = D->NumberOfPages * EFI_PAGE_SIZE;
    if ((UINT64)Base >= Start && (UINT64)Base - Start < Size) {
      Print(L"  Base memory map: Type=%u Range=%lx-%lx Attributes=%lx\n",
            (UINT32)D->Type, Start, Start + Size - 1, D->Attribute);
      Found = TRUE;
      break;
    }
  }
  if (!Found) Print(L"  Base memory map: NOT FOUND\n");
  FreePool(Map);
}

EFI_STATUS EFIAPI UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0;
  EFI_STATUS Status;
  UINTN Index;
  EFI_DEVICE_PATH_TO_TEXT_PROTOCOL *PathToText = NULL;
  UINTN CandidateCount = 0;

  Print(L"KirinGopDiag V2 / READ ONLY / No SetMode or framebuffer writes\n");
  Print(L"ConsoleOutHandle=%p\n", gST->ConsoleOutHandle);
  Status = gBS->LocateProtocol(&gEfiDevicePathToTextProtocolGuid, NULL, (VOID **)&PathToText);
  if (EFI_ERROR(Status)) PathToText = NULL;

  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiGraphicsOutputProtocolGuid,
                                  NULL, &Count, &Handles);
  if (EFI_ERROR(Status)) {
    Print(L"LocateHandleBuffer(GOP) failed: %r\n", Status);
    return Status;
  }
  Print(L"GOP handle count: %u\n", (UINT32)Count);

  for (Index = 0; Index < Count; Index++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
    EFI_DEVICE_PATH_PROTOCOL *Path = NULL;
    CHAR16 *Text = NULL;
    UINT32 ModeNumber;
    BOOLEAN CurrentOk = FALSE;

    Print(L"\n========== GOP %u Handle=%p ==========\n", (UINT32)Index, Handles[Index]);
    Print(L"  Same handle as ConsoleOut: %s (not a reliable ConsoleOut-to-GOP mapping)\n",
          Handles[Index] == gST->ConsoleOutHandle ? L"YES" : L"NO");
    Status = gBS->HandleProtocol(Handles[Index], &gEfiGraphicsOutputProtocolGuid,
                                (VOID **)&Gop);
    if (EFI_ERROR(Status) || Gop == NULL || Gop->Mode == NULL) {
      Print(L"  GOP protocol/mode unavailable: %r\n", Status);
      continue;
    }
    Status = gBS->HandleProtocol(Handles[Index], &gEfiDevicePathProtocolGuid,
                                (VOID **)&Path);
    if (!EFI_ERROR(Status) && Path != NULL && PathToText != NULL) {
      Text = PathToText->ConvertDevicePathToText(Path, FALSE, FALSE);
      if (Text != NULL) {
        Print(L"  DevicePath: %s\n", Text);
        FreePool(Text);
      }
    } else {
      Print(L"  DevicePath: unavailable\n");
    }

    Print(L"  CurrentMode=%u MaxMode=%u SizeOfInfo=%u\n",
          Gop->Mode->Mode, Gop->Mode->MaxMode, Gop->Mode->SizeOfInfo);
    Print(L"  FrameBufferBase=%lx FrameBufferSize=%lx\n",
          (UINT64)Gop->Mode->FrameBufferBase, (UINT64)Gop->Mode->FrameBufferSize);
    if (Gop->Mode->Info != NULL) {
      PrintModeInfo(Gop->Mode->Info, TRUE);
      CurrentOk = (Gop->Mode->Info->HorizontalResolution != 0 &&
                   Gop->Mode->Info->VerticalResolution != 0 &&
                   Gop->Mode->Info->PixelsPerScanLine == Gop->Mode->Info->HorizontalResolution &&
                   Gop->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor &&
                   Gop->Mode->FrameBufferBase != 0 && Gop->Mode->FrameBufferSize != 0);
    }
    if (Gop->Mode->FrameBufferBase != 0 && Gop->Mode->FrameBufferSize != 0)
      PrintMemoryDescriptor(Gop->Mode->FrameBufferBase);

    // Avoid unbounded QueryMode calls if the firmware reports corrupt MaxMode.
    if (Gop->Mode->MaxMode > 128) {
      Print(L"  WARNING: MaxMode > 128, limiting inspection to first 128 modes\n");
    }
    for (ModeNumber = 0; ModeNumber < Gop->Mode->MaxMode && ModeNumber < 128; ModeNumber++) {
      EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info = NULL;
      UINTN InfoSize = 0;
      Status = Gop->QueryMode(Gop, ModeNumber, &InfoSize, &Info);
      if (EFI_ERROR(Status) || Info == NULL) {
        Print(L"  Mode[%u] QueryMode=%r\n", ModeNumber, Status);
        if (Info != NULL) FreePool(Info);
        continue;
      }
      Print(L"  Mode[%u] InfoSize=%u\n", ModeNumber, (UINT32)InfoSize);
      if (InfoSize >= sizeof(*Info)) {
        PrintModeInfo(Info, ModeNumber == Gop->Mode->Mode);
      } else {
        Print(L"    WARNING: InfoSize too small\n");
      }
      FreePool(Info);
    }
    Print(L"  CURRENT GOP Windows field check: %s\n",
          CurrentOk ? L"CANDIDATE (hardware not verified)" : L"FAIL");
    if (CurrentOk) CandidateCount++;
  }
  Print(L"\nSUMMARY: current-mode field candidates=%u / GOPs=%u\n",
        (UINT32)CandidateCount, (UINT32)Count);
  Print(L"No registers changed. No pixels accessed. ExitBootServices behavior NOT tested.\n");
  FreePool(Handles);
  return EFI_SUCCESS;
}
