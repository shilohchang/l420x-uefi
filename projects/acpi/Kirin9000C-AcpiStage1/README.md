# Kirin9000C ACPI Stage 1

This package contains a **host-side build preparation script only**, not firmware and not an ACPI table implementation. It creates a separate EDK II DSC file so you can compile the unmodified upstream AcpiTableDxe driver.

## Build

Run in Ubuntu/WSL2 after obtaining EDK II and building BaseTools:

```bash
cd ~/uefi-work/edk2
python3 /path/to/prepare_acpi_driver.py
source edksetup.sh
export GCC_AARCH64_PREFIX=aarch64-linux-gnu-
build -a AARCH64 -t GCC -b RELEASE \
  -p MdeModulePkg/KirinAcpiTest.dsc \
  -m MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableDxe.inf
find Build -type f -name AcpiTableDxe.efi -exec file {} \;
```

## Important

- This driver is a **DXE_DRIVER** (not an EFI application). It is not an executable Windows boot solution.
- Compiling it does not make it safe to boot Windows. If subsequently loaded, it may publish ACPI pointers in RAM for the current boot and could conflict with existing firmware.
- Do not install fabricated MADT/FADT/DSDT tables, modify firmware partitions, or alter the UOS boot entry.
- On the actual device, only load from a detachable test USB after ensuring a recovery path. Reboot to discard in-memory changes. Be aware of possible firmware hangs.
