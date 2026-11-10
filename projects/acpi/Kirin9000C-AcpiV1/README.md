# Kirin9000C ACPI v1 — OFFLINE EXPERIMENTAL TABLES

This package is **not bootable** and **must not be injected into a Windows/UOS boot session**. It is a hardware-description prototype with incomplete platform ACPI coverage. No FADT, DSDT, MCFG, IORT, SPCR, PCIe, UFS, or Windows drivers are included.

## Grounding

- 12 Linux CPU DT Affinity values (0x000, 0x100, 0x200, 0x300, 0x400, 0x401, 0x500, 0x501, 0x600, 0x601, 0x700, 0x701), each cross-checked against GICR_TYPER on the test machine.
- GICD 0xFF000000, GICR range 0xFF040000+0x180000, stride 0x20000, with Last on redistributor 11.
- CNTFRQ_EL0 1,920,000 Hz. **Frequency is not encoded in GTDT.**
- Timer interrupts from live DT: 29 secure EL1, 30 non-secure EL1, 27 virtual EL1, 26 EL2 physical. Linux currently uses INTID 26 for `arch_timer` (does not establish Windows choice).
- Linux `/proc/interrupts` shows vgic maintenance PPI 25; GICC v1 has an experimental maintenance interrupt value 25.

## Run (Windows, WSL Ubuntu, Linux with Python3)

```sh
cd Kirin9000C-AcpiV1
python3 build_acpi.py
python3 validate_acpi.py
sha256sum output/*.dat
```

Outputs:

- `output/MADT.dat`: standard ACPI 6.5 MADT rev 6, 12 GICC (82 bytes each), one GICD (24 bytes), one GICR (16 bytes).
- `output/GTDT.dat`: ACPI 6.5 GTDT rev 3, 104 bytes.
- `output/manifest.json`: facts vs provisional fields.

**Important unresolved fields:** GTDT interrupt mode/polarity/always-on; EL2/VHE behavior; correctness of maintenance IRQ; processor power efficiency / performance interrupt; per-CPU UID binding with future DSDT; firmware PSCI and secure/NS EL state; ACPI FADT, memory tables and chipset driver stack.

`GTDT` timer flags are zero as a **provisional level/high encoding**. `CNTControlBase` and `CNTReadBase` use all-ones to denote unspecified counter control/read MMIO registers. Hardware counter is accessed through ARM architecture registers. No platform timer blocks specified. Do not assume this is the final Windows-compatible configuration.

`MADT` ACPI GICC GICRBaseAddress fields are zero because the table uses a shared GICR discovery range record. GICC MPIDR fields contain affinity values rather than MPIDR non-affinity bits (`0x81000000` from boot processor).

## EDK II reference

Structures correspond to `MdePkg/Include/IndustryStandard/Acpi65.h`:
- `EFI_ACPI_6_5_GIC_STRUCTURE` (82 bytes)
- `EFI_ACPI_6_5_GIC_DISTRIBUTOR_STRUCTURE` (24 bytes)
- `EFI_ACPI_6_5_GICR_STRUCTURE` (16 bytes)
- `EFI_ACPI_6_5_GENERIC_TIMER_DESCRIPTION_TABLE` (104 bytes)

ACPI specification: https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html

## Protection of original machine

Only local files are generated. Scripts do **not** access disks at the block-device layer, firmware, NVRAM, GIC MMIO or UEFI services. Do not add these incomplete tables to an OS boot session. A later controlled EFI publishing test can be created separately after FADT/DSDT and resource requirements are settled.
