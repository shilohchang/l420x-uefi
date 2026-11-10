# Kirin9000C GICR Probe v2 (experimental)

Unlike v1, an absent GICR address in UEFI memory map is informational, not a hard block. A memory-map descriptor with a non-MMIO type blocks the experiment. CPU I/O2 protocol must exist. Two confirmations are required: `R` to read frame 0, then `A` to read the remaining frames, but only if first affinity matches the known DT value set. On mismatch or the Last flag the scan stops.

**Risk:** Read-only MMIO can still hang or crash the UEFI Shell/firmware session. No code can guarantee no hardware/firmware side effects. This app has no disk, NVRAM, ACPI or MMIO register write API calls. Only use on the dedicated test machine from a USB drive; physically power-cycle if needed. Do not run an OS in a potentially failed session; restart normally.

From WSL after copying/unzipping directory to `~/uefi-work/Kirin9000C-GicProbeV2`:

```sh
cd ~/uefi-work/edk2
python3 ../Kirin9000C-GicProbeV2/prepare.py
source edksetup.sh
export GCC_AARCH64_PREFIX=aarch64-linux-gnu-
build -a AARCH64 -t GCC -b RELEASE -p MdeModulePkg/KirinGicProbeV2.dsc -m GicProbeV2/GicProbeV2.inf
find Build -type f -name GicProbeV2.efi -exec file {} \;
```

This code has not been compiled against the user's local edk2 checkout.

In a fresh EFI Shell session, run `GicProbeV2.efi` and review results. v2 reads 12 register locations at 0xFF040008 + index*0x20000 using EFI_CPU_IO2_PROTOCOL.Mem.Read 64-bit. It never calls Mem.Write. `GICR_TYPER` is documented read-only, but the safety guarantee is only that the code contains no intentional persistent writes.
