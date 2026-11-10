# Kirin9000C ACPI v3 — session-only loader prototype

**Do not boot an OS after loading these experimental ACPI tables. Reboot to clear session state.**

The device's UEFI Shell previously loaded `AcpiTableDxe.efi` successfully. This project builds an application that installs DSDT, FADT, MADT, GTDT using `EFI_ACPI_TABLE_PROTOCOL`, after checking signature/length/checksum and automatically proceeding after the user explicitly launches the .efi program. It does not alter firmware flash, NVRAM, target disks or MMIO registers. It DOES publish ACPI configuration in volatile UEFI memory, and could cause firmware bugs/hangs. Reboot after tests.

**Important correction:** EDK II AcpiTableDxe `InstallAcpiTable()` copies DSDT, and updates the FADT's `Dsdt`/`XDsdt` pointers and FADT checksum. You should install **DSDT first**, then FADT. The input FADT has `X_DSDT=0` intentionally; the driver's owned copy is patched. Do not pass that input template directly to an OS. The experiment assumes the same edk2 AcpiTableDxe binary previously built by the user.

## WSL preparation

All three project directories should be siblings under `~/uefi-work`:

```
~/uefi-work/Kirin9000C-AcpiV1/output/MADT.dat
~/uefi-work/Kirin9000C-AcpiV1/output/GTDT.dat
~/uefi-work/Kirin9000C-AcpiV2/output/FADT-UNRELOCATED.dat
~/uefi-work/Kirin9000C-AcpiV2/output/DSDT.aml
~/uefi-work/Kirin9000C-AcpiV3-AutoLoader/prepare.py
```

Run:

```sh
cd ~/uefi-work
unzip -o /mnt/c/Users/ICY/Downloads/Kirin9000C-AcpiV3-AutoLoader.zip
cd ~/uefi-work/edk2
python3 ../Kirin9000C-AcpiV3-AutoLoader/prepare.py
source edksetup.sh
export GCC_AARCH64_PREFIX=aarch64-linux-gnu-
build -a AARCH64 -t GCC -b RELEASE -p MdeModulePkg/KirinSessionLoader.dsc -m SessionLoader/SessionLoader.inf
find Build -type f -name KirinAcpiSessionLoader.efi -exec file {} \;
```

## EFI Shell (session-only experimental verification)

In a new clean UEFI Shell boot:

```
FS8:\> load AcpiTableDxe.efi
FS8:\> KirinAcpiSessionLoader.efi
```

The program installs the tables immediately after launch with no extra keypress. Run `Kirin9000C-EfiProbe.efi`, capture screenshots, **reboot**, and do not boot Linux or Windows in the same EFI Shell session.

If any installation call fails, loader attempts to uninstall already-added tables, but that rollback may not be perfect; always reboot.

## Known limitations

- MADT GICC maintenance GSIV and CPU interface numbers are provisional.
- GTDT flags are provisional, EL2 timer availability to an OS unverified.
- Processor-only DSDT lacks UFS, PCIe, IORT, SMMU, GPU, power management, USB, and Windows OS drivers.
- FADT has only provisional fields and missing sleep/reset resources; success does not make it bootable.
- EDK II may modify pointers inside copied FADT; no automatic pointer validation in v3: a future independent ACPI inspector should inspect RSDP/XSDT/FADT/DSDT.
- This release is source-level drafted; compilation has NOT been tested against your particular edk2 checkout.

## Auto-launch behavior

The user explicitly running `KirinAcpiSessionLoader.efi` authorizes installation; there is **no keyboard confirmation**. The loader still validates all four embedded table checksums first, requires the ACPI table protocol, and refuses a second installation if an ACPI root configuration table is already present. Do not boot any operating system after experimental table installation. Reboot after inspection.
