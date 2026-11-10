#include <Uefi.h>
#include <Protocol/CpuIo2.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

#define GICR_BASE       0xFF040000ULL
#define GICR_STRIDE     0x20000ULL
#define GICR_COUNT      12
#define GICR_TYPER_OFF  0x8ULL

STATIC CONST UINT32 ExpectedAff[GICR_COUNT] = {
  0x000, 0x100, 0x200, 0x300, 0x400, 0x401,
  0x500, 0x501, 0x600, 0x601, 0x700, 0x701
};

STATIC BOOLEAN
ReadKey (OUT CHAR16 *Ch)
{
  EFI_INPUT_KEY Key;
  EFI_STATUS Status;
  UINTN Index;
  while (TRUE) {
    Status = gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
    if (Status == EFI_SUCCESS) {
      *Ch = Key.UnicodeChar;
      return TRUE;
    }
    if (Status != EFI_NOT_READY) return FALSE;
    Status = gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &Index);
    if (EFI_ERROR(Status)) return FALSE;
  }
}

STATIC EFI_STATUS
TakeMap (OUT EFI_MEMORY_DESCRIPTOR **Map, OUT UINTN *MapSize,
         OUT UINTN *DescSize)
{
  UINTN Size = 0, Key = 0, DSize = 0, Attempt, Capacity;
  UINT32 Version = 0;
  EFI_MEMORY_DESCRIPTOR *Buffer;
  EFI_STATUS Status;

  Status = gBS->GetMemoryMap(&Size, NULL, &Key, &DSize, &Version);
  if (Status != EFI_BUFFER_TOO_SMALL || DSize < sizeof(EFI_MEMORY_DESCRIPTOR))
    return EFI_DEVICE_ERROR;
  for (Attempt = 0; Attempt < 5; Attempt++) {
    if (Size > MAX_UINTN - DSize * 32) return EFI_OUT_OF_RESOURCES;
    Capacity = Size + DSize * 32;
    Buffer = AllocatePool(Capacity);
    if (Buffer == NULL) return EFI_OUT_OF_RESOURCES;
    Size = Capacity;
    Status = gBS->GetMemoryMap(&Size, Buffer, &Key, &DSize, &Version);
    if (!EFI_ERROR(Status)) {
      *Map = Buffer;
      *MapSize = Size;
      *DescSize = DSize;
      return EFI_SUCCESS;
    }
    FreePool(Buffer);
    if (Status != EFI_BUFFER_TOO_SMALL) return Status;
  }
  return EFI_BUFFER_TOO_SMALL;
}

STATIC EFI_MEMORY_DESCRIPTOR *
Lookup (EFI_MEMORY_DESCRIPTOR *Map, UINTN MapSize, UINTN DescSize, UINT64 Address)
{
  UINTN Offset;
  for (Offset = 0; Offset + DescSize <= MapSize; Offset += DescSize) {
    EFI_MEMORY_DESCRIPTOR *Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Offset);
    if (Desc->NumberOfPages > MAX_UINT64 / EFI_PAGE_SIZE) continue;
    UINT64 Length = Desc->NumberOfPages * EFI_PAGE_SIZE;
    if (Address >= Desc->PhysicalStart && Address - Desc->PhysicalStart < Length)
      return Desc;
  }
  return NULL;
}

STATIC BOOLEAN
KnownAff (UINT32 Aff)
{
  UINTN I;
  for (I = 0; I < GICR_COUNT; ++I) {
    if (Aff == ExpectedAff[I]) return TRUE;
  }
  return FALSE;
}

STATIC EFI_STATUS
ReadTyper (EFI_CPU_IO2_PROTOCOL *CpuIo, UINTN Frame, UINT64 *Value)
{
  UINT64 Address = GICR_BASE + Frame * GICR_STRIDE + GICR_TYPER_OFF;
  *Value = 0;
  return CpuIo->Mem.Read(CpuIo, EfiCpuIoWidthUint64, Address, 1, Value);
}

STATIC VOID
Report (UINTN Frame, UINT64 Value)
{
  UINT32 Aff = (UINT32)(Value >> 32);
  Print(L"Frame %02u TYPER=0x%016lx Aff=0x%08x Last=%u PLPIS=%u matchDT=%s\n",
    (UINT32)Frame, Value, Aff, (UINT32)((Value >> 4) & 1),
    (UINT32)(Value & 1), KnownAff(Aff) ? L"YES" : L"NO");
}

EFI_STATUS EFIAPI
UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_MEMORY_DESCRIPTOR *Map = NULL, *Desc;
  UINTN MapSize = 0, DescSize = 0, I;
  EFI_CPU_IO2_PROTOCOL *CpuIo = NULL;
  EFI_STATUS Status;
  UINT64 Addr, Value;
  CHAR16 Ch;
  BOOLEAN Forbidden = FALSE;
  UINT32 FirstAff;

  Print(L"\nKirin9000C GICR Probe v2 -- EXPERIMENTAL READ ONLY\n");
  Print(L"No disk / NVRAM / ACPI / MMIO register writes.\n");
  Print(L"GICR base 0x%lx, stride 0x%lx, frames %u\n",
    GICR_BASE, GICR_STRIDE, (UINT32)GICR_COUNT);
  Print(L"A fault or hang is still possible even on a read.\n\n");

  Status = TakeMap(&Map, &MapSize, &DescSize);
  if (EFI_ERROR(Status)) {
    Print(L"GetMemoryMap failed: %r. No reads.\n", Status);
    return EFI_SUCCESS;
  }
  for (I = 0; I < GICR_COUNT; ++I) {
    Addr = GICR_BASE + I * GICR_STRIDE + GICR_TYPER_OFF;
    Desc = Lookup(Map, MapSize, DescSize, Addr);
    if (Desc == NULL) {
      Print(L"Frame %02u 0x%lx: EFI map ABSENT (not proof of invalid MMIO)\n", (UINT32)I, Addr);
    } else {
      Print(L"Frame %02u 0x%lx: EFI type=%u attr=0x%lx\n", (UINT32)I,
        Addr, (UINT32)Desc->Type, Desc->Attribute);
      if (Desc->Type != EfiMemoryMappedIO) Forbidden = TRUE;
    }
  }
  FreePool(Map);

  Status = gBS->LocateProtocol(&gEfiCpuIo2ProtocolGuid, NULL, (VOID **)&CpuIo);
  Print(L"\nCPU I/O2 protocol: %r\n", Status);
  if (EFI_ERROR(Status) || CpuIo == NULL || Forbidden) {
    Print(L"Stopped: CPU I/O2 missing OR a target has non-MMIO EFI descriptor.\n");
    return EFI_SUCCESS;
  }

  Print(L"\nEFI map absence does NOT establish safe MMIO access.\n");
  Print(L"Read one register first. Press R to attempt Frame 00, other key exits: ");
  if (!ReadKey(&Ch) || (Ch != L'R' && Ch != L'r')) {
    Print(L"\nCancelled.\n");
    return EFI_SUCCESS;
  }
  Print(L"\nReading Frame 00...\n");
  Status = ReadTyper(CpuIo, 0, &Value);
  if (EFI_ERROR(Status)) {
    Print(L"Frame 00 read failed: %r. Stop.\n", Status);
    return EFI_SUCCESS;
  }
  Report(0, Value);
  FirstAff = (UINT32)(Value >> 32);
  if (!KnownAff(FirstAff)) {
    Print(L"Unexpected Frame 00 Affinity; refusing remaining reads.\n");
    return EFI_SUCCESS;
  }
  if ((Value & (1ULL << 4)) != 0) {
    Print(L"Frame 00 indicates LAST. Stop.\n");
    return EFI_SUCCESS;
  }
  Print(L"\nFrame 00 plausible. Press A to read frames 01..11, other key exits: ");
  if (!ReadKey(&Ch) || (Ch != L'A' && Ch != L'a')) {
    Print(L"\nStopped after Frame 00.\n");
    return EFI_SUCCESS;
  }
  Print(L"\n");
  for (I = 1; I < GICR_COUNT; ++I) {
    Status = ReadTyper(CpuIo, I, &Value);
    if (EFI_ERROR(Status)) {
      Print(L"Frame %02u read failed: %r. Stop.\n", (UINT32)I, Status);
      break;
    }
    Report(I, Value);
    if (!KnownAff((UINT32)(Value >> 32))) {
      Print(L"Unexpected Affinity: stop.\n");
      break;
    }
    if ((Value & (1ULL << 4)) != 0) {
      Print(L"Last Redistributor encountered. End scan.\n");
      break;
    }
  }
  Print(L"Done. Restart to leave experimental UEFI session.\n");
  return EFI_SUCCESS;
}
