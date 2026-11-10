/** Session-only ACPI table publication experiment. NO OS BOOT. */
#include <Uefi.h>
#include <Protocol/AcpiTable.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include "EmbeddedTables.h"

#define TABLE_COUNT 4

typedef struct {
  CONST CHAR16 *Name;
  CONST UINT8 *Data;
  UINTN Size;
  CONST CHAR8 *Signature;
} TABLE_DESC;

STATIC CONST TABLE_DESC mTables[TABLE_COUNT] = {
  {L"DSDT", kDsdt, sizeof(kDsdt), "DSDT"},
  {L"FADT", kFadt, sizeof(kFadt), "FACP"},
  {L"MADT", kMadt, sizeof(kMadt), "APIC"},
  {L"GTDT", kGtdt, sizeof(kGtdt), "GTDT"}
};

STATIC BOOLEAN
ValidateTable (CONST TABLE_DESC *T)
{
  UINT32 Length;
  UINT8 Checksum = 0;
  UINTN Index;

  if (T->Size < 36 || T->Size > 4096) {
    return FALSE;
  }
  if (CompareMem (T->Data, T->Signature, 4) != 0) {
    return FALSE;
  }
  CopyMem (&Length, T->Data + 4, sizeof(Length));
  if ((UINTN)Length != T->Size) {
    return FALSE;
  }
  for (Index = 0; Index < T->Size; ++Index) {
    Checksum = (UINT8)(Checksum + T->Data[Index]);
  }
  return (Checksum == 0);
}

STATIC EFI_STATUS
ReadConfirmation (VOID)
{
  EFI_STATUS Status;
  EFI_INPUT_KEY Key;
  UINTN Index;
  EFI_EVENT WaitEvent;

  Print (L"\nPress uppercase I to install experimental ACPI tables; any other key cancels: ");
  WaitEvent = gST->ConIn->WaitForKey;
  Status = gBS->WaitForEvent (1, &WaitEvent, &Index);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
  if (EFI_ERROR(Status) || Key.UnicodeChar != L'I') {
    Print (L"CANCELLED.\n");
    return EFI_ABORTED;
  }
  Print (L"INSTALL REQUESTED.\n");
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI
UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_ACPI_TABLE_PROTOCOL *Acpi = NULL;
  UINTN Keys[TABLE_COUNT] = {0};
  UINTN I;
  UINTN Installed = 0;

  Print (L"Kirin9000C ACPI v3 - SESSION EXPERIMENT ONLY\n");
  Print (L"NO disk, NVRAM, BIOS or MMIO writes. DOES MODIFY EFI ACPI CONFIGURATION IN RAM.\n");
  Print (L"DO NOT BOOT WINDOWS OR LINUX IN THIS SESSION. REBOOT AFTER TEST!\n\n");

  for (I = 0; I < TABLE_COUNT; ++I) {
    if (!ValidateTable (&mTables[I])) {
      Print (L"Embedded %s failed signature/length/checksum. STOP.\n", mTables[I].Name);
      return EFI_COMPROMISED_DATA;
    }
    Print (L"Validated %s: %u bytes\n", mTables[I].Name, (UINT32)mTables[I].Size);
  }
  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID**)&Acpi);
  Print (L"AcpiTable protocol: %r\n", Status);
  if (EFI_ERROR(Status) || Acpi == NULL) {
    Print (L"First load AcpiTableDxe.efi from the USB drive.\n");
    return EFI_NOT_FOUND;
  }
  Status = ReadConfirmation ();
  if (EFI_ERROR(Status)) {
    return Status;
  }

  for (I = 0; I < TABLE_COUNT; ++I) {
    Status = Acpi->InstallAcpiTable (Acpi, (VOID*)mTables[I].Data, mTables[I].Size, &Keys[I]);
    Print (L"Install %s -> %r (key=%u)\n", mTables[I].Name, Status, (UINT32)Keys[I]);
    if (EFI_ERROR(Status)) {
      break;
    }
    ++Installed;
  }

  if (EFI_ERROR(Status)) {
    Print (L"Installation failed; attempting best-effort rollback...\n");
    while (Installed != 0) {
      --Installed;
      Print (L"Uninstall %s -> %r\n", mTables[Installed].Name,
             Acpi->UninstallAcpiTable(Acpi, Keys[Installed]));
    }
    Print (L"REBOOT REQUIRED; do not boot OS in this session.\n");
    return Status;
  }
  Print (L"All four tables reported success. This does NOT establish OS compatibility.\n");
  Print (L"Run Kirin9000C-EfiProbe.efi to confirm ACPI2 configuration, THEN REBOOT.\n");
  return EFI_SUCCESS;
}
