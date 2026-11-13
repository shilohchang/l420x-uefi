# Kirin9000C Device Discovery (experimental)

This is a **read-only DTB inventory tool** plus disabled, vendor-ID ACPI diagnostic SSDT drafts. It is **not** a DTB-to-functional-Windows-driver conversion. The first-level discovery result is a list of DTB nodes, not a list of usable independent Windows devices.

## Project layout

- `tools/discover.py`: inventory DTB, generate classification CSV/JSON and diagnostic ASL.
- `tools/set-stage.py`: activate **exactly one** diagnostic ACPI `_HID` in a new ASL copy.
- `asl/SSDT-UFS-PROBE.asl`: disabled, experimental HUFS resources (not safe to auto-enable).
- `../../diagnostics/StageSsdtLoader/`: separate UEFI application and platform DSC.
- `../../../scripts/discover-acpi-v8.sh`: inventory + optional IASL validation.
- `../../../scripts/build-ssdt-loader.sh`: EDK2 AARCH64 build consistent with existing scripts.
- `../../../artifacts/v8/`: generated CSV, AML, captured DTB and EFI binaries; untracked.

## Use

```bash
cd ~/l420x-uefi
bash scripts/discover-acpi-v8.sh
bash scripts/build-ssdt-loader.sh
# Generated IDs and details in artifacts/v8/acpi-discovery/output/devices.csv
python3 projects/acpi/Kirin9000C-DeviceDiscovery/tools/set-stage.py \
  --source artifacts/v8/acpi-discovery/output/asl/SSDT-DISCOVERY-I2C_SPI.asl \
  --dest artifacts/v8/acpi-discovery/stage/EXPERIMENT.asl --device LTX0001
# Replace LTX0001 with an ID actually present in your generated source.
(cd artifacts/v8/acpi-discovery/stage && iasl -tc EXPERIMENT.asl)
```

Copy `artifacts/v8/StageSsdtLoader.efi` to `EFI/BOOT/tools/` and staged `EXPERIMENT.aml` to `EFI/BOOT/SSDT/` on the **same USB**. Only after the known-working ACPI base tables are installed, run `StageSsdtLoader.efi` once per cold boot. Do not install a second copy of an existing namespace. This file loader reads from the filesystem the EFI program was started from. Rollback: cold boot and omit the experimental table.

## Safety

- Generated diagnostic `_HID` values (`LTXxxxx`) are made-up diagnostic IDs and do **not** bind Microsoft device drivers. Newly enabled nodes deliberately have no `_CRS`.
- Disabled entries and child buses are not tested as functional Windows devices.
- Root DTB `reg` fields are not guaranteed to be CPU physical addresses; `ranges`, GPIO interrupt mapping, clocks, reset, power, DMA/IORT all need validation.
- Do **not** enable UFS/PCIe/I2C controller drivers based on this discovery alone; do not overwrite bootable DSDT/FADT/MADT/GTDT.
- The ELF/EFI module and SSDTs in this integration are **not** asserted to have been run on device.
- `L420X-ACPI-Discovery.zip` is just the original external input; do not commit it or an extracted parallel project tree.
