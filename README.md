# L420X UEFI

Experimental UEFI firmware, ACPI tables, bootloaders
and hardware diagnostics for the Kirin 9000C platform.

## Repository structure

- `projects/acpi/` - ACPI table experiments
- `projects/bootloaders/` - Boot and session loaders
- `projects/diagnostics/` - GIC and hardware probes
- `edk2-overlay/` - EDK2 modifications and custom sources
- `scripts/` - Build and deployment scripts
- `docs/` - Technical documentation
- `artifacts/` - Selected release artifacts

## Status

Experimental research project. Firmware compatibility
and operating system boot support are not guaranteed.

## EDK2

The upstream EDK2 working tree is excluded from Git.
See `edk2-overlay/` for local modification records.
