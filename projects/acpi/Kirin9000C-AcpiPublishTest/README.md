# Kirin9000C ACPI publication test (not a bootable ACPI platform)

This UEFI application installs a tiny, syntactically valid SSDT (`Name(KTST, One)`) through the ACPI Table Protocol established by `AcpiTableDxe`. It checks whether the firmware publishes the ACPI 2.0 Configuration Table. This is **not** an ACPI implementation for Windows and must not be used to boot an OS.

## Build (EDK II current GCC toolchain)

Extract ZIP alongside `edk2`, then from `~/uefi-work/edk2`:

```bash
python3 ../Kirin9000C-AcpiPublishTest/prepare.py
source edksetup.sh
export GCC_AARCH64_PREFIX=aarch64-linux-gnu-
build -a AARCH64 -t GCC -b RELEASE -p MdeModulePkg/KirinPublishTest.dsc -m AcpiPublishTest/AcpiPublishTest.inf
find Build -type f -name AcpiPublishTest.efi -exec file {} \;
```

Copy `AcpiPublishTest.efi` to UEFI Shell USB root (do not replace BOOTAA64.EFI).

## UEFI Shell (EXPERIMENTAL)

1. Boot to Shell, find USB filesystem `fsN:`.
2. `load fsN:\AcpiTableDxe.efi` (skip if already loaded in this boot session).
3. `fsN:\AcpiPublishTest.efi`.
4. `fsN:\Kirin9000C-EfiProbe.efi` to cross-check ConfigurationTable.
5. Take a picture, then reboot immediately to restore the unmodified boot environment.

Do **not** chainload any operating system from the same test session; do not use `bcfg`, `dmpstore`, or overwrite firmware partitions.
