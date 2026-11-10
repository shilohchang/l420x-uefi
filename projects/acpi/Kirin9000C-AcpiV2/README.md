# Kirin9000C ACPI v2 — FADT / DSDT offline prototype

**NOT BOOTABLE. DO NOT LOAD THE FADT INTO YOUR EFI SHELL FOR OS BOOT.** No firmware, NVRAM, disk partition or MMIO writes are performed by these scripts.

This is an ADD-ON to `Kirin9000C-AcpiV1`; it deliberately does not modify MADT/GTDT from v1.

## Files

- `build_platform.py` constructs an ACPI 6.5 revision 6, 276-byte **UNRELOCATED** FADT draft, and writes the processor-only `Kirin9000C-Processors.asl`.
- `output/FADT-UNRELOCATED.dat` has HW_REDUCED_ACPI=1 (FADT Flags bit 20), ARM_BOOT_ARCH PSCI_COMPLIANT=1, PSCI_USE_HVC=0 (SMC), and X_DSDT=0 **deliberately**.
- `Kirin9000C-Processors.asl` declares 12 processor Device objects (HID ACPI0007, UID 0..11) with UID mapping matching the v1 MADT.
- `validate_platform.py` checks basic structure, flags, checksum, and UID consistency.

## WSL build

```sh
cd ~/uefi-work
unzip -o /mnt/c/Users/ICY/Downloads/Kirin9000C-AcpiV2.zip
cd Kirin9000C-AcpiV2
python3 build_platform.py
python3 validate_platform.py
sudo apt-get update && sudo apt-get install -y acpica-tools
mkdir -p output
cd output
iasl -tc ../Kirin9000C-Processors.asl
```

The `iasl` compiler emits `Kirin9000C-Processors.aml` (and usually `.hex`) in the output directory. If the compiler differs, `find . -name '*.aml'` can identify the file. Do not assume `iasl` output passed without examining its errors.

## Why an unrelocted FADT?

ACPI FADT must hold the physical address of an allocated DSDT in `X_DSDT` (byte offset 140), not its file path. Firmware must place valid DSDT AML in appropriate ACPI memory and then update FADT X_DSDT and its checksum. Installing a zero-X_DSDT FADT would be incorrect. FADT is *not* simply one independent table in XSDT: its DSDT pointer is essential.

### Remaining hardware / architecture questions

- PSCI is `arm,psci` using SMC from UOS live DT; PSCI behavior required by OS still needs verification.
- Processor topology/power state and idle/power management objects, platform device resources (UFS/USB/GPU/PCIe), power/reset, IORT/SMMU and Windows drivers are absent.
- ACPI v1 GICC maintenance interrupt, CPU interface number and GTDT flags are provisional and require correction/verification before firmware publication for OS boot.
- ACPI namespace CPU objects must have `_UID` matching the corresponding GICC `AcpiProcessorUid`.

## Authoritative references

- https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html (FADT offsets 112,129,131,140; DSDT)
- https://uefi.org/specs/ACPI/6.5/08_Processor_Configuration_and_Control.html (ACPI0007 processor objects)
- https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/other-acpi-namespace-objects
