/**
 * L420X one-shot, RAM-only SSDT staging application.
 * Reads exactly one file from the same EFI filesystem as this image:
 *   \EFI\BOOT\SSDT\EXPERIMENT.aml
 * Requires an installed EFI_ACPI_TABLE_PROTOCOL (AcpiTableDxe).
 * No NVRAM, firmware flash, block I/O writes, or MMIO operations.
 * IMPORTANT: Run at most once per cold-boot ACPI test session.
 */
#include <Uefi.h>
#include <Guid/FileInfo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/AcpiTable.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>
#define MAX_AML_SIZE (256 * 1024)
#define AML_PATH L"\\EFI\\BOOT\\SSDT\\EXPERIMENT.aml"

STATIC EFI_STATUS LoadFile(EFI_HANDLE ImageHandle, VOID **Buffer, UINTN *Size) {
  EFI_STATUS St;
  EFI_LOADED_IMAGE_PROTOCOL *Li = NULL;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
  EFI_FILE_PROTOCOL *Root = NULL, *File = NULL;
  EFI_FILE_INFO *Info = NULL;
  UINTN InfoSize = 0, Count;
  VOID *Data = NULL;
  *Buffer=NULL; *Size=0;
  St = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Li);
  if (EFI_ERROR(St)) goto Done;
  St = gBS->HandleProtocol(Li->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR(St)) goto Done;
  St = Fs->OpenVolume(Fs, &Root);
  if (EFI_ERROR(St)) goto Done;
  St = Root->Open(Root, &File, AML_PATH, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(St)) goto Done;
  St = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, NULL);
  if (St != EFI_BUFFER_TOO_SMALL || InfoSize == 0 || InfoSize > 16384) { St=EFI_COMPROMISED_DATA; goto Done; }
  Info=AllocateZeroPool(InfoSize);
  if (!Info) {St=EFI_OUT_OF_RESOURCES;goto Done;}
  St = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, Info);
  if (EFI_ERROR(St)) goto Done;
  if (Info->FileSize < 36 || Info->FileSize > MAX_AML_SIZE) {St=EFI_BAD_BUFFER_SIZE;goto Done;}
  Data=AllocateZeroPool((UINTN)Info->FileSize);
  if (!Data) {St=EFI_OUT_OF_RESOURCES;goto Done;}
  Count=(UINTN)Info->FileSize;
  St=File->Read(File,&Count,Data);
  if (EFI_ERROR(St) || Count != (UINTN)Info->FileSize) {St=EFI_DEVICE_ERROR;goto Done;}
  *Buffer=Data;*Size=Count;Data=NULL;
Done:
  if (Data) FreePool(Data);
  if (Info) FreePool(Info);
  if (File) File->Close(File);
  if (Root) Root->Close(Root);
  return St;
}

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
  EFI_STATUS St;
  VOID *Data=NULL;
  UINTN Size=0, Key=0, I;
  EFI_ACPI_TABLE_PROTOCOL *Acpi=NULL;
  UINT32 Length=0;
  UINT8 Sum=0;
  Print(L"L420X RAM-only SSDT loader. ONE TABLE PER BOOT.\n");
  Print(L"Input: %s (same filesystem as this EFI executable)\n", AML_PATH);
  St=LoadFile(ImageHandle,&Data,&Size);
  if (EFI_ERROR(St)) {Print(L"Read failed: %r\n",St);return St;}
  if (CompareMem(Data,"SSDT",4)!=0) {St=EFI_COMPROMISED_DATA;goto Finish;}
  CopyMem(&Length,(UINT8*)Data+4,sizeof(Length));
  if (Length!=Size) {St=EFI_COMPROMISED_DATA;goto Finish;}
  for (I=0;I<Size;I++) Sum=(UINT8)(Sum+((UINT8*)Data)[I]);
  if (Sum!=0) {St=EFI_CRC_ERROR;goto Finish;}
  St=gBS->LocateProtocol(&gEfiAcpiTableProtocolGuid,NULL,(VOID**)&Acpi);
  if (EFI_ERROR(St) || Acpi==NULL) {Print(L"Missing ACPI table protocol. Load AcpiTableDxe first.\n");goto Finish;}
  Print(L"SSDT signature/length/checksum OK, %u bytes. Installing now...\n",(UINT32)Size);
  St=Acpi->InstallAcpiTable(Acpi,Data,Size,&Key);
  Print(L"InstallAcpiTable: %r key=%u\n",St,(UINT32)Key);
  if (!EFI_ERROR(St)) Print(L"Installation is in THIS EFI SESSION ONLY. Do not run twice.\n");
Finish:
  if (EFI_ERROR(St)) Print(L"Rejected SSDT: %r\n",St);
  if (Data) FreePool(Data);
  return St;
}
