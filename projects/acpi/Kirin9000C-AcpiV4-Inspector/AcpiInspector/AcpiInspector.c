/**
  Kirin9000C ACPI Inspector v4 (READ ONLY).
  Inspect only the ACPI pointers published via UEFI ConfigurationTable.
  Intended to run AFTER experimental session loader, BEFORE reboot.
  The program does not install/uninstall ACPI, write MMIO, variables or disks.
  A corrupt firmware pointer could still cause a CPU exception.
**/
#include <Uefi.h>
#include <Guid/Acpi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

#define MAX_TABLE_SIZE (1024U * 1024U)
#define MAX_XSDT_ENTRIES 128U

STATIC UINT32 Load32(CONST VOID *P) {
  UINT32 V;
  CopyMem(&V, P, sizeof(V));
  return V;
}
STATIC UINT64 Load64(CONST VOID *P) {
  UINT64 V;
  CopyMem(&V, P, sizeof(V));
  return V;
}
STATIC BOOLEAN ChecksumOK(CONST UINT8 *B, UINT32 L) {
  UINT32 I;
  UINT8 Sum = 0;
  for (I = 0; I < L; I++) Sum = (UINT8)(Sum + B[I]);
  return Sum == 0;
}
STATIC BOOLEAN Signature(CONST UINT8 *B, CONST CHAR8 *Sig, UINTN Size) {
  return CompareMem(B, Sig, Size) == 0;
}
STATIC BOOLEAN ValidateSdt(CONST UINT8 *P, CONST CHAR8 *Name, UINT32 *Length) {
  UINT32 L;
  if (P == NULL) {
    Print(L"%a: NULL pointer\n", Name);
    return FALSE;
  }
  if (!Signature(P, Name, 4)) {
    Print(L"%a: unexpected signature %.4a\n", Name, P);
    return FALSE;
  }
  L = Load32(P + 4);
  if (L < 36 || L > MAX_TABLE_SIZE) {
    Print(L"%a: invalid length=%u\n", Name, L);
    return FALSE;
  }
  Print(L"  %a @0x%lx: length=%u revision=%u checksum=%s\n",
        Name, (UINT64)(UINTN)P, L, (UINT32)P[8], ChecksumOK(P,L) ? L"OK" : L"BAD");
  *Length = L;
  return ChecksumOK(P,L);
}
STATIC VOID InspectMadt(CONST UINT8 *P, UINT32 L) {
  UINT32 Off = 44, Gicc = 0, Gicd = 0, Gicr = 0;
  if (L < 44) { Print(L"    MADT short\n"); return; }
  while (Off + 2 <= L) {
    UINT8 Type = P[Off];
    UINT8 Size = P[Off+1];
    if (Size < 2 || Off + Size > L) { Print(L"    MADT malformed subtable @%u\n",Off); return; }
    if (Type == 0x0B) {
      Gicc++;
      if (Size >= 80) {
        UINT32 Uid=Load32(P+Off+8);
        UINT64 Mpidr=Load64(P+Off+68);
        Print(L"    GICC[%u] UID=%u MPIDR=0x%lx\n", Gicc-1, Uid, Mpidr);
      }
    } else if (Type == 0x0C) {
      Gicd++;
    } else if (Type == 0x0E) {
      Gicr++;
    }
    Off += Size;
  }
  Print(L"    MADT summary: GICC=%u GICD=%u GICR=%u\n",Gicc,Gicd,Gicr);
}
STATIC VOID InspectFadt(CONST UINT8 *P, UINT32 L) {
  UINT64 Dsdt64 = 0;
  UINT32 Dsdt32;
  CONST UINT8 *D;
  UINT32 DL = 0;
  if (L < 148) { Print(L"    FADT too short for X_DSDT\n"); return; }
  Dsdt32 = Load32(P+40);
  Dsdt64 = Load64(P+140);
  Print(L"    FADT DSDT32=0x%x X_DSDT=0x%lx FLAGS=0x%x ARM_BOOT_ARCH=0x%x\n",
      Dsdt32, Dsdt64, Load32(P+112), (UINT32)(P[129] | ((UINT32)P[130] << 8)));
  if (Dsdt64 == 0 && Dsdt32 == 0) {
    Print(L"    FAIL: DSDT pointers both zero\n"); return;
  }
  D = (CONST UINT8 *)(UINTN)(Dsdt64 != 0 ? Dsdt64 : Dsdt32);
  if (ValidateSdt(D,"DSDT",&DL)) Print(L"    FADT -> DSDT linkage VALID\n");
  else Print(L"    FADT -> DSDT linkage INVALID\n");
}
EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
  UINTN I;
  CONST UINT8 *Rsdp = NULL;
  UINT32 RsdpLength, XL, EntryCount, Index;
  UINT64 XsdtAddress;
  CONST UINT8 *Xsdt;
  UINT32 SeenFadt=0, SeenMadt=0, SeenGtdt=0;
  Print(L"Kirin9000C ACPI Inspector v4 - READ ONLY\n");
  Print(L"Reads active session ACPI pointers. No firmware, disk, variable, or MMIO writes.\n");
  for (I=0; I<gST->NumberOfTableEntries; I++) {
    if (CompareGuid(&gST->ConfigurationTable[I].VendorGuid,&gEfiAcpi20TableGuid)) {
      Rsdp=(CONST UINT8 *)gST->ConfigurationTable[I].VendorTable;
      break;
    }
  }
  if (Rsdp==NULL) { Print(L"ACPI 2.0 ConfigurationTable NOT FOUND\n"); return EFI_NOT_FOUND; }
  if (!Signature(Rsdp,"RSD PTR ",8)) { Print(L"Invalid RSDP signature\n"); return EFI_COMPROMISED_DATA; }
  Print(L"RSDP @0x%lx revision=%u legacy-checksum=%s\n",(UINT64)(UINTN)Rsdp,
      (UINT32)Rsdp[15],ChecksumOK(Rsdp,20)?L"OK":L"BAD");
  if (Rsdp[15] < 2) { Print(L"RSDP revision <2, no XSDT\n"); return EFI_UNSUPPORTED; }
  RsdpLength=Load32(Rsdp+20);
  if (RsdpLength < 36 || RsdpLength > 4096) { Print(L"Bad RSDP length=%u\n",RsdpLength); return EFI_COMPROMISED_DATA; }
  Print(L"RSDP length=%u extended checksum=%s\n",RsdpLength,
      ChecksumOK(Rsdp,RsdpLength)?L"OK":L"BAD");
  if (!ChecksumOK(Rsdp,20) || !ChecksumOK(Rsdp,RsdpLength)) return EFI_COMPROMISED_DATA;
  XsdtAddress=Load64(Rsdp+24);
  if (XsdtAddress == 0) { Print(L"XSDT pointer ZERO\n");return EFI_COMPROMISED_DATA; }
  Xsdt=(CONST UINT8 *)(UINTN)XsdtAddress;
  if (!ValidateSdt(Xsdt,"XSDT",&XL)) return EFI_COMPROMISED_DATA;
  if ((XL-36)%8 != 0) { Print(L"XSDT entries misaligned\n");return EFI_COMPROMISED_DATA; }
  EntryCount=(XL-36)/8;
  if (EntryCount > MAX_XSDT_ENTRIES) { Print(L"Too many XSDT entries=%u\n",EntryCount);return EFI_COMPROMISED_DATA; }
  Print(L"XSDT entries=%u\n",EntryCount);
  for (Index=0;Index<EntryCount;Index++) {
    UINT64 Address=Load64(Xsdt+36+Index*8);
    CONST UINT8 *T=(CONST UINT8 *)(UINTN)Address;
    UINT32 Len;
    CHAR8 Name[5];
    if (Address==0) { Print(L"  entry %u NULL\n",Index);continue; }
    CopyMem(Name,T,4); Name[4]=0;
    Print(L"XSDT[%u]: ",Index);
    if (!ValidateSdt(T,Name,&Len)) continue;
    if (Signature(T,"FACP",4)) { SeenFadt++; InspectFadt(T,Len); }
    if (Signature(T,"APIC",4)) { SeenMadt++; InspectMadt(T,Len); }
    if (Signature(T,"GTDT",4)) { SeenGtdt++; }
  }
  Print(L"SUMMARY: FADT=%u MADT=%u GTDT=%u\n",SeenFadt,SeenMadt,SeenGtdt);
  Print(L"These are structural observations, NOT Windows compatibility certification.\n");
  Print(L"REBOOT after session-only ACPI tests. DO NOT boot OS from this session.\n");
  return (SeenFadt==1 && SeenMadt==1 && SeenGtdt==1)?EFI_SUCCESS:EFI_NOT_FOUND;
}
