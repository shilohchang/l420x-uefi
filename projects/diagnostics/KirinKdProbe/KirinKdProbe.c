/** @file
  L420x Kirin 9000C V7: read-only ACPI/DBG2 and protocol survey.

  Does not touch MMIO, framebuffer, NVRAM, BCD or firmware tables.
  Output is UEFI Shell redirectable, for example:
    fs8:\efi\boot\tools\KirinKdProbe.efi > fs8:\efi\boot\logs\v7-kdprobe.txt

  Experimental and unverified on target hardware.
**/
#include <Uefi.h>
#include <Guid/Acpi.h>
#include <Protocol/SerialIo.h>
#include <Protocol/Usb2HostController.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#define MAX_TABLE_BYTES  0x100000U
#define MAX_TABLE_COUNT  256U
#define MAX_DBG_DEVICES  32U

STATIC
UINT16
Le16 (IN CONST UINT8 *P)
{
  return (UINT16)((UINT16)P[0] | ((UINT16)P[1] << 8));
}

STATIC
UINT32
Le32 (IN CONST UINT8 *P)
{
  return (UINT32)P[0] | ((UINT32)P[1] << 8) |
         ((UINT32)P[2] << 16) | ((UINT32)P[3] << 24);
}

STATIC
UINT64
Le64 (IN CONST UINT8 *P)
{
  return (UINT64)Le32 (P) | ((UINT64)Le32 (P + 4) << 32);
}

STATIC
BOOLEAN
ChecksumOk (IN CONST UINT8 *Ptr, IN UINTN Length)
{
  UINT8 Sum;
  UINTN Index;
  Sum = 0;
  for (Index = 0; Index < Length; Index++) {
    Sum = (UINT8)(Sum + Ptr[Index]);
  }
  return (BOOLEAN)(Sum == 0);
}

STATIC
BOOLEAN
GetAcpiTable (IN UINT64 Address, IN CONST UINT8 **Table, OUT UINT32 *Length)
{
  CONST UINT8 *P;
  UINT32 L;
  if (Address == 0 || (Address & 3) != 0) {
    return FALSE;
  }
  // The platform firmware must supply mapped, valid physical ACPI pointers.
  // Never dereference arbitrary addresses; use only firmware-published XSDT pointers.
  P = (CONST UINT8 *)(UINTN)Address;
  L = Le32 (P + 4);
  if (L < 36 || L > MAX_TABLE_BYTES) {
    return FALSE;
  }
  *Table = P;
  *Length = L;
  return TRUE;
}

STATIC
VOID
InspectDbg2 (IN CONST UINT8 *T, IN UINT32 L)
{
  UINT32 Offset;
  UINT32 Count;
  UINT32 I;
  if (L < 44) {
    Print (L"[KD7] DBG2_TRUNCATED\n");
    return;
  }
  Offset = Le32 (T + 36);
  Count = Le32 (T + 40);
  Print (L"[KD7] DBG2_DeviceOffset=0x%x Count=%u\n", Offset, Count);
  if (Count > MAX_DBG_DEVICES || Offset < 44 || Offset >= L) {
    Print (L"[KD7] DBG2_BAD_HEADER\n");
    return;
  }
  for (I = 0; I < Count; I++) {
    CONST UINT8 *D;
    UINT16 DLen;
    UINT8 RegCount;
    UINT16 Type;
    UINT16 Sub;
    UINT16 NameLen;
    UINT16 NameOff;
    UINT16 RegOff;
    UINT16 SizeOff;
    UINTN J;
    if (Offset > L || L - Offset < 22) {
      Print (L"[KD7] DBG2_DEVICE_TRUNCATED i=%u\n", I);
      break;
    }
    D = T + Offset;
    DLen = Le16 (D + 1);
    RegCount = D[3];
    NameLen = Le16 (D + 4);
    NameOff = Le16 (D + 6);
    Type = Le16 (D + 12);
    Sub = Le16 (D + 14);
    RegOff = Le16 (D + 18);
    SizeOff = Le16 (D + 20);
    if (DLen < 22 || DLen > L - Offset) {
      Print (L"[KD7] DBG2_DEVICE_BAD_LENGTH i=%u\n", I);
      break;
    }
    Print (L"[KD7] DBG2_DEVICE index=%u type=0x%x subtype=0x%x regs=%u\n",
      I, Type, Sub, RegCount);
    if (NameLen > 0 && NameOff >= 22 && NameOff <= DLen && NameLen <= DLen - NameOff) {
      // Print bounded ASCII one byte at a time to avoid assuming NUL termination.
      Print (L"[KD7] DBG2_Name_%u=", I);
      for (J = 0; J < NameLen && D[NameOff + J] != 0 && J < 80; J++) {
        if (D[NameOff + J] >= 32 && D[NameOff + J] < 127) {
          Print (L"%c", D[NameOff + J]);
        }
      }
      Print (L"\n");
    }
    if (RegCount > 0 && RegCount <= 8 &&
        RegOff >= 22 && RegOff <= DLen && (UINTN)RegCount * 12 <= DLen - RegOff &&
        SizeOff >= 22 && SizeOff <= DLen && (UINTN)RegCount * 4 <= DLen - SizeOff) {
      for (J = 0; J < RegCount; J++) {
        CONST UINT8 *Gas;
        Gas = D + RegOff + 12 * J;
        Print (L"[KD7] DBG2_Reg%u Space=%u Width=%u Access=%u Base=0x%lx Size=0x%x\n",
          (UINT32)J, Gas[0], Gas[1], Gas[3], Le64 (Gas + 4),
          Le32 (D + SizeOff + 4 * J));
      }
    }
    Offset += DLen;
  }
  Print (L"[KD7] DBG2_PRESENCE_IS_NOT_KDNET_SUPPORT\n");
}

STATIC
VOID
InspectRsdp (IN CONST UINT8 *R, IN BOOLEAN IsAcpi2)
{
  UINT8 Revision;
  UINT32 RsdpLen;
  UINT64 XsdtAddress;
  CONST UINT8 *Xsdt;
  UINT32 XsdtLen;
  UINT32 N;
  UINTN I;
  BOOLEAN SawDbg2;
  if (CompareMem (R, "RSD PTR ", 8) != 0 || !ChecksumOk (R, 20)) {
    Print (L"[KD7] RSDP_SIGNATURE_OR_CHECKSUM_BAD\n");
    return;
  }
  Revision = R[15];
  Print (L"[KD7] RSDP_Revision=%u RSDT=0x%x\n", Revision, Le32 (R + 16));
  if (Revision < 2 || !IsAcpi2) {
    Print (L"[KD7] NO_ACPI2_XSDT\n");
    return;
  }
  RsdpLen = Le32 (R + 20);
  if (RsdpLen < 36 || RsdpLen > 64 || !ChecksumOk (R, RsdpLen)) {
    Print (L"[KD7] RSDP_EXT_CHECKSUM_OR_LENGTH_BAD\n");
    return;
  }
  XsdtAddress = Le64 (R + 24);
  Print (L"[KD7] XSDT_Address=0x%lx\n", XsdtAddress);
  if (!GetAcpiTable (XsdtAddress, &Xsdt, &XsdtLen) ||
      CompareMem (Xsdt, "XSDT", 4) != 0 ||
      !ChecksumOk (Xsdt, XsdtLen) ||
      ((XsdtLen - 36) % 8) != 0) {
    Print (L"[KD7] XSDT_INVALID\n");
    return;
  }
  N = (XsdtLen - 36) / 8;
  Print (L"[KD7] XSDT_Length=%u Entries=%u\n", XsdtLen, N);
  if (N > MAX_TABLE_COUNT) {
    Print (L"[KD7] XSDT_TOO_MANY_ENTRIES\n");
    return;
  }
  SawDbg2 = FALSE;
  for (I = 0; I < N; I++) {
    UINT64 Address;
    CONST UINT8 *T;
    UINT32 L;
    Address = Le64 (Xsdt + 36 + 8 * I);
    if (!GetAcpiTable (Address, &T, &L)) {
      Print (L"[KD7] TABLE_%u_INVALID_PTR=0x%lx\n", (UINT32)I, Address);
      continue;
    }
    Print (L"[KD7] ACPI_Table_%u=%c%c%c%c Addr=0x%lx Len=%u Checksum=%a\n",
      (UINT32)I, T[0], T[1], T[2], T[3], Address, L,
      ChecksumOk (T, L) ? "OK" : "BAD");
    if (CompareMem (T, "DBG2", 4) == 0) {
      SawDbg2 = TRUE;
      if (ChecksumOk (T, L)) {
        InspectDbg2 (T, L);
      } else {
        Print (L"[KD7] DBG2_BAD_CHECKSUM\n");
      }
    }
  }
  if (!SawDbg2) {
    Print (L"[KD7] DBG2_NOT_PRESENT\n");
  }
}

STATIC
VOID
CountProtocol (IN EFI_GUID *Guid, IN CONST CHAR16 *Name)
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles;
  UINTN Count;
  Handles = NULL;
  Count = 0;
  Status = gBS->LocateHandleBuffer (ByProtocol, Guid, NULL, &Count, &Handles);
  Print (L"[KD7] %s status=%r count=%u\n", Name, Status,
    EFI_ERROR (Status) ? 0 : (UINT32)Count);
  if (Handles != NULL) {
    FreePool (Handles);
  }
}

EFI_STATUS
EFIAPI
UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  UINTN I;
  BOOLEAN Found;
  Print (L"[KD7] KirinKdProbe V7 read-only / no firmware writes\n");
  Print (L"[KD7] Revision=0x%x ConfigEntries=%u\n",
    gST->Hdr.Revision, (UINT32)gST->NumberOfTableEntries);
  Found = FALSE;
  for (I = 0; I < gST->NumberOfTableEntries; I++) {
    EFI_CONFIGURATION_TABLE *Entry;
    Entry = &gST->ConfigurationTable[I];
    if (CompareGuid (&Entry->VendorGuid, &gEfiAcpi20TableGuid)) {
      Print (L"[KD7] ACPI20_RSDP=0x%lx\n", (UINT64)(UINTN)Entry->VendorTable);
      InspectRsdp ((CONST UINT8 *)Entry->VendorTable, TRUE);
      Found = TRUE;
      break;
    }
  }
  if (!Found) {
    for (I = 0; I < gST->NumberOfTableEntries; I++) {
      EFI_CONFIGURATION_TABLE *Entry;
      Entry = &gST->ConfigurationTable[I];
      if (CompareGuid (&Entry->VendorGuid, &gEfiAcpi10TableGuid)) {
        Print (L"[KD7] ACPI10_RSDP=0x%lx\n", (UINT64)(UINTN)Entry->VendorTable);
        InspectRsdp ((CONST UINT8 *)Entry->VendorTable, FALSE);
        Found = TRUE;
        break;
      }
    }
  }
  if (!Found) {
    Print (L"[KD7] ACPI_RSDP_NOT_PRESENT\n");
  }
  CountProtocol (&gEfiSerialIoProtocolGuid, L"SerialIo_Protocols");
  CountProtocol (&gEfiUsb2HcProtocolGuid, L"Usb2HostController_Protocols");
  Print (L"[KD7] UEFI protocol counts do not confirm USB device-mode KDNET support\n");
  Print (L"[KD7] DONE\n");
  return EFI_SUCCESS;
}
