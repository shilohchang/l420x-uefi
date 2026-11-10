# Kirin9000C ACPI Inspector v4 (read only)

Analyzes ACPI configuration installed in this UEFI session: RSDP -> XSDT -> FADT -> DSDT plus MADT and GTDT. Does **not** load, install, modify or delete tables. Does not write disks, NVRAM, firmware or MMIO. It dereferences UEFI-published firmware pointers; malformed pointers can still crash the session. Intended ONLY with previously tested session loader and an immediate reboot afterward; do not boot an OS in that session.

## Build (WSL Ubuntu, existing edk2)

Unzip into `~/uefi-work/Kirin9000C-AcpiV4-Inspector/`.

```bash
cd ~/uefi-work/edk2
python3 ../Kirin9000C-AcpiV4-Inspector/prepare.py
source edksetup.sh
export GCC_AARCH64_PREFIX=aarch64-linux-gnu-
build -a AARCH64 -t GCC -b RELEASE -p MdeModulePkg/KirinAcpiInspector.dsc -m AcpiInspector/AcpiInspector.inf
find Build -type f -name KirinAcpiInspector.efi -exec file {} \;
```

## Hardware test (FAT32 USB, clean UEFI Shell boot)

```
FS8:\> load AcpiTableDxe.efi
FS8:\> KirinAcpiSessionLoader.efi
FS8:\> KirinAcpiInspector.efi
```

No confirmation key is needed. Copy the inspector to USB; use actual FS mapping instead of FS8 if different. Photograph full output. Reboot immediately afterwards without booting Linux/Windows.

## Important limitations

The program checks checksums, lengths, signatures, XSDT entries, FADT -> DSDT pointer and GICC/GICD/GICR structure counts. It does NOT verify Windows firmware or driver requirements. For corrupted pointers, an unexpected fault is possible. All addresses are read-only in code.
