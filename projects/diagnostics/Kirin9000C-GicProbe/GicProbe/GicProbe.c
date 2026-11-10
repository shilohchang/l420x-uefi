#include <Uefi.h>
#include <Protocol/CpuIo2.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

#define GICD_BASE 0xFF000000ULL
#define GICR_BASE 0xFF040000ULL
#define GICR_STRIDE 0x20000ULL
#define GICR_FRAME_COUNT 12
#define GICR_TYPER_OFFSET 0x8ULL

STATIC BOOLEAN
FindDescriptor (IN EFI_MEMORY_DESCRIPTOR *Map, IN UINTN MapSize,
                IN UINTN DescriptorSize, IN UINT64 Address,
                OUT EFI_MEMORY_DESCRIPTOR **Found)
{
  UINTN Offset;
  for (Offset = 0; Offset + DescriptorSize <= MapSize; Offset += DescriptorSize) {
    EFI_MEMORY_DESCRIPTOR *D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Offset);
    UINT64 Start = D->PhysicalStart;
    UINT64 Bytes;
    if (D->NumberOfPages > (MAX_UINT64 / EFI_PAGE_SIZE)) continue;
    Bytes = D->NumberOfPages * EFI_PAGE_SIZE;
    if (Address >= Start && (Address - Start) < Bytes) {
      *Found = D;
      return TRUE;
    }
  }
  *Found = NULL;
  return FALSE;
}

STATIC EFI_STATUS
TakeMemoryMap (OUT EFI_MEMORY_DESCRIPTOR **Map, OUT UINTN *MapSize,
               OUT UINTN *DescriptorSize)
{
  EFI_STATUS Status;
  UINTN Size = 0, Key, Version, DescSize = 0;
  UINTN Attempt;
  EFI_MEMORY_DESCRIPTOR *Buffer = NULL;
  Status = gBS->GetMemoryMap(&Size, NULL, &Key, &DescSize, &Version);
  if (Status != EFI_BUFFER_TOO_SMALL) return Status;
  if (DescSize == 0) return EFI_COMPROMISED_DATA;
  for (Attempt = 0; Attempt < 4; Attempt++) {
    UINTN Capacity = Size + DescSize * 32;
    Buffer = AllocatePool(Capacity);
    if (Buffer == NULL) return EFI_OUT_OF_RESOURCES;
    Size = Capacity;
    Status = gBS->GetMemoryMap(&Size, Buffer, &Key, &DescSize, &Version);
    if (!EFI_ERROR(Status)) {
      *Map = Buffer;
      *MapSize = Size;
      *DescriptorSize = DescSize;
      return EFI_SUCCESS;
    }
    FreePool(Buffer);
    Buffer = NULL;
    if (Status != EFI_BUFFER_TOO_SMALL) return Status;
  }
  return EFI_BUFFER_TOO_SMALL;
}

EFI_STATUS EFIAPI
UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_MEMORY_DESCRIPTOR *Map = NULL, *Desc;
  UINTN MapSize = 0, DescSize = 0, Index;
  EFI_CPU_IO2_PROTOCOL *CpuIo = NULL;
  BOOLEAN Allowed[GICR_FRAME_COUNT];
  BOOLEAN AllAllowed = TRUE;
  EFI_INPUT_KEY Key;
  UINTN EventIndex;
  UINT64 Address, Value;
  UINT32 Aff;

  Print(L"\nKirin9000C GICR Probe v1 - MMIO PRECHECK FIRST\n");
  Print(L"READ ONLY, no register/disk/NVRAM writes.\n");
  Print(L"GICD=0x%lx, GICR=0x%lx, stride=0x%lx\n", GICD_BASE, GICR_BASE, GICR_STRIDE);
  Print(L"Read target: GICR_TYPER at +0x8, frames=12\n\n");

  Status = TakeMemoryMap(&Map, &MapSize, &DescSize);
  if (EFI_ERROR(Status)) {
    Print(L"GetMemoryMap failed: %r. No MMIO read.\n", Status);
    return EFI_SUCCESS;
  }

  for (Index=0; Index<GICR_FRAME_COUNT; Index++) {
    Address = GICR_BASE + Index*GICR_STRIDE + GICR_TYPER_OFFSET;
    Allowed[Index] = FALSE;
    if (FindDescriptor(Map, MapSize, DescSize, Address, &Desc)) {
      Print(L"Frame %02u: 0x%lx type=%u attr=0x%lx", (UINT32)Index, Address,
        (UINT32)Desc->Type, Desc->Attribute);
      if (Desc->Type == EfiMemoryMappedIO) {
        Allowed[Index] = TRUE;
        Print(L" [MMIO candidate]\n");
      } else {
        Print(L" [NOT MMIO, skip]\n");
      }
    } else {
      Print(L"Frame %02u: 0x%lx not in EFI memory map, skip\n", (UINT32)Index, Address);
    }
    if (!Allowed[Index]) AllAllowed = FALSE;
  }
  FreePool(Map);

  Status = gBS->LocateProtocol(&gEfiCpuIo2ProtocolGuid, NULL, (VOID **)&CpuIo);
  Print(L"\nEFI_CPU_IO2_PROTOCOL: %r\n", Status);
  if (EFI_ERROR(Status) || CpuIo == NULL) {
    Print(L"CPU IO2 unavailable. No MMIO read attempted.\n");
    return EFI_SUCCESS;
  }
  if (!AllAllowed) {
    Print(L"One or more frames lack EfiMemoryMappedIO coverage. No MMIO read attempted.\n");
    return EFI_SUCCESS;
  }
  Print(L"\nWARNING: Firmware MMIO access can still hang on faulty implementations.\n");
  Print(L"Only test in a separate UEFI Shell session, then reboot.\n");
  Print(L"Press 'R' to READ 12 GICR_TYPER registers, any other key to cancel: ");
  while (TRUE) {
    Status = gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
    if (Status == EFI_NOT_READY) {
      Status = gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &EventIndex);
      if (EFI_ERROR(Status)) return EFI_SUCCESS;
      continue;
    }
    if (EFI_ERROR(Status)) return EFI_SUCCESS;
    break;
  }
  Print(L"\n");
  if (Key.UnicodeChar != L'R' && Key.UnicodeChar != L'r') {
    Print(L"Cancelled; no MMIO reads.\n");
    return EFI_SUCCESS;
  }

  for (Index=0; Index<GICR_FRAME_COUNT; Index++) {
    Address = GICR_BASE + Index*GICR_STRIDE + GICR_TYPER_OFFSET;
    Value = 0;
    Status = CpuIo->Mem.Read(CpuIo, EfiCpuIoWidthUint64, Address, 1, &Value);
    if (EFI_ERROR(Status)) {
      Print(L"Frame %02u READ FAILED at 0x%lx: %r\n", (UINT32)Index, Address, Status);
      break;
    }
    Aff = (UINT32)(Value >> 32);
    Print(L"Frame %02u TYPER=0x%lx Aff=0x%08x Last=%u PLPIS=%u\n",
      (UINT32)Index, Value, Aff, (UINT32)((Value >> 4)&1), (UINT32)(Value&1));
    if (((Value >> 4)&1) != 0 && Index != GICR_FRAME_COUNT-1) {
      Print(L"Last flag appeared early: stopping scan.\n");
      break;
    }
  }
  Print(L"Done. Reboot before entering an OS.\n");
  return EFI_SUCCESS;
}
