/**
  Kirin 9000C temporary ACPI publication test.
  Publishes only a minimal SSDT with Name (KTST, One).
  THIS IS NOT A BOOTABLE WINDOWS ACPI PLATFORM. Reboot after test.
**/
#include <Uefi.h>
#include <IndustryStandard/Acpi.h>
#include <Guid/Acpi.h>
#include <Protocol/AcpiTable.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

#pragma pack(1)
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER Header;
  UINT8 Aml[6];
} TEST_SSDT;
#pragma pack()

STATIC BOOLEAN
Acpi20Present (IN EFI_SYSTEM_TABLE *SystemTable)
{
  UINTN Index;
  for (Index = 0; Index < SystemTable->NumberOfTableEntries; Index++) {
    if (CompareGuid (&SystemTable->ConfigurationTable[Index].VendorGuid, &gEfiAcpi20TableGuid)) {
      return TRUE;
    }
  }
  return FALSE;
}

EFI_STATUS EFIAPI
UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_ACPI_TABLE_PROTOCOL *Acpi;
  TEST_SSDT Table;
  UINT8 *Bytes;
  UINTN Index, Key;
  UINT8 Sum = 0;

  Print (L"=== Kirin9000C ACPI publication TEST ONLY ===\n");
  Print (L"ACPI20 before: %s\n", Acpi20Present(SystemTable) ? L"YES" : L"NO");
  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID **)&Acpi);
  if (EFI_ERROR(Status)) {
    Print (L"AcpiTable protocol unavailable: %r\n", Status);
    Print (L"First load AcpiTableDxe.efi from UEFI Shell.\n");
    return Status;
  }

  ZeroMem (&Table, sizeof(Table));
  Table.Header.Signature = SIGNATURE_32('S','S','D','T');
  Table.Header.Length = (UINT32)sizeof(Table);
  Table.Header.Revision = 2;
  CopyMem (Table.Header.OemId, "KIRIN9", 6);
  Table.Header.OemTableId = SIGNATURE_64('K','9','0','0','0','C','T','S');
  Table.Header.OemRevision = 1;
  Table.Header.CreatorId = SIGNATURE_32('T','E','S','T');
  Table.Header.CreatorRevision = 1;
  // Valid AML: Name (KTST, One). Not a functional hardware description.
  Table.Aml[0] = 0x08;
  Table.Aml[1] = 'K'; Table.Aml[2] = 'T';
  Table.Aml[3] = 'S'; Table.Aml[4] = 'T';
  Table.Aml[5] = 0x01;
  Bytes = (UINT8 *)&Table;
  for (Index = 0; Index < sizeof(Table); Index++) { Sum = (UINT8)(Sum + Bytes[Index]); }
  Table.Header.Checksum = (UINT8)(0 - Sum);

  Key = 0;
  Status = Acpi->InstallAcpiTable(Acpi, &Table, sizeof(Table), &Key);
  Print (L"InstallAcpiTable SSDT: %r\n", Status);
  if (!EFI_ERROR(Status)) {
    Print (L"Table key: %lu\n", (UINT64)Key);
  }
  Print (L"ACPI20 after: %s\n", Acpi20Present(SystemTable) ? L"YES" : L"NO");
  Print (L"Do NOT boot Windows/Linux with this incomplete test ACPI!\n");
  Print (L"Reboot now to return to the original firmware state.\n");
  return Status;
}
