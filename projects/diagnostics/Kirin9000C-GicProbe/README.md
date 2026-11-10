# Kirin9000C GICR Probe v1

This utility is a standalone UEFI application. It is **not** an ACPI installer and performs no NVRAM/disk/register writes.

## Build

Unpack to `~/uefi-work/Kirin9000C-GicProbe`, then:

```bash
cd ~/uefi-work/edk2
python3 ../Kirin9000C-GicProbe/prepare.py
source edksetup.sh
export GCC_AARCH64_PREFIX=aarch64-linux-gnu-
build -a AARCH64 -t GCC -b RELEASE -p MdeModulePkg/KirinGicProbe.dsc -m GicProbe/GicProbe.inf
find Build -name GicProbe.efi -type f -exec file {} \;
```

## Device operation

Place `GicProbe.efi` on a separate FAT32 UEFI Shell USB disk. Run it by name from that filesystem. It initially ONLY inspects UEFI memory descriptors and looks up `EFI_CPU_IO2_PROTOCOL`. It refuses to read MMIO unless all 12 addresses are declared `EfiMemoryMappedIO` and the CPU I/O2 protocol exists. If all preconditions hold it prompts for 'R'. **Even then, a firmware flaw could make a read hang or fault.** The EFI application only uses `CpuIo->Mem.Read`, never its write method. It never modifies boot options.

The check is intentionally conservative and may refuse a perfectly valid platform where MMIO is omitted from the UEFI memory map. Do not remove checks without analyzing firmware mappings and access protections. Boot into a fresh UEFI Shell session; reboot afterwards.

Expected GICR frames: `0xFF040000 + frame*0x20000`, `GICR_TYPER` at `+0x8`.

If it skips access, send the full output. Do not use `devmem` from a running Linux system to work around the check.
