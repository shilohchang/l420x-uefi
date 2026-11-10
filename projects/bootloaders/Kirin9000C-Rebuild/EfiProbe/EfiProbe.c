#include <Uefi.h>
#include <Guid/Acpi.h>
#include <Guid/SmBios.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/BlockIo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>

STATIC VOID ShowProtocol(IN CONST CHAR16 *Name, IN EFI_GUID *Guid) {
  EFI_STATUS Status;
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0;
  Status = gBS->LocateHandleBuffer(ByProtocol, Guid, NULL, &Count, &Handles);
  Print(L"%s: %r, handles=%u\n", Name, Status, (UINT32)Count);
  if (!EFI_ERROR(Status) && Handles != NULL) {
    gBS->FreePool(Handles);
  }
}

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
  EFI_GUID Acpi10Guid = ACPI_TABLE_GUID;
  EFI_GUID Acpi20Guid = EFI_ACPI_20_TABLE_GUID;
  EFI_GUID Smbios3Guid = SMBIOS3_TABLE_GUID;
  EFI_GUID AcpiProtocolGuid = EFI_ACPI_TABLE_PROTOCOL_GUID;
  EFI_GUID GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
  EFI_GUID BlockGuid = EFI_BLOCK_IO_PROTOCOL_GUID;
  EFI_GUID FsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
  BOOLEAN HasAcpi = FALSE;
  UINTN I;
  (VOID)ImageHandle;

  Print(L"=== Kirin9000C UEFI read-only probe v1 ===\n");
  Print(L"FirmwareVendor: %s\n", SystemTable->FirmwareVendor);
  Print(L"FirmwareRevision: 0x%08x\n", SystemTable->FirmwareRevision);
  Print(L"UEFI Revision: %u.%02u\n", (UINT32)(SystemTable->Hdr.Revision >> 16), (UINT32)(SystemTable->Hdr.Revision & 0xffff));
  Print(L"ConfigurationTable count: %u\n", (UINT32)SystemTable->NumberOfTableEntries);
  for (I = 0; I < SystemTable->NumberOfTableEntries; ++I) {
    EFI_CONFIGURATION_TABLE *T = &SystemTable->ConfigurationTable[I];
    CONST CHAR16 *Label = L"Other";
    if (CompareMem(&T->VendorGuid, &Acpi10Guid, sizeof(EFI_GUID)) == 0) { Label = L"ACPI 1.0"; HasAcpi = TRUE; }
    if (CompareMem(&T->VendorGuid, &Acpi20Guid, sizeof(EFI_GUID)) == 0) { Label = L"ACPI 2.0+"; HasAcpi = TRUE; }
    if (CompareMem(&T->VendorGuid, &Smbios3Guid, sizeof(EFI_GUID)) == 0) { Label = L"SMBIOS3"; }
    Print(L"Table[%u]: %g  pointer=%p  %s\n", (UINT32)I, &T->VendorGuid, T->VendorTable, Label);
  }
  Print(L"ACPI configuration present: %s\n", HasAcpi ? L"YES" : L"NO");
  ShowProtocol(L"AcpiTable protocol", &AcpiProtocolGuid);
  ShowProtocol(L"GraphicsOutput protocol", &GopGuid);
  ShowProtocol(L"BlockIo protocol", &BlockGuid);
  ShowProtocol(L"SimpleFileSystem protocol", &FsGuid);
  Print(L"Done. Press any key to return to Shell.\n");
  if (SystemTable->ConIn != NULL) {
    EFI_INPUT_KEY Key;
    UINTN EventIndex;
    gBS->WaitForEvent(1, &SystemTable->ConIn->WaitForKey, &EventIndex);
    SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key);
  }
  return EFI_SUCCESS;
}
