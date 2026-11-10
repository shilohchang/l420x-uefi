# Kirin9000C HardwareProbe v1
Read-only AArch64 UEFI application. Reads MPIDR_EL1, CNTFRQ_EL0 and CNTVCT_EL0 on the current UEFI boot processor only. Measures counter difference across a firmware Stall(100000) call. It does not access GIC MMIO, write system registers, alter NVRAM or disk, install ACPI tables, or boot an OS.

## Compile on WSL Ubuntu with current edk2 installed

```bash
cd ~/uefi-work
unzip -o /mnt/c/Users/ICY/Downloads/Kirin9000C-HardwareProbe.zip
cd edk2
python3 ../Kirin9000C-HardwareProbe/prepare.py
source edksetup.sh
export GCC_AARCH64_PREFIX=aarch64-linux-gnu-
build -a AARCH64 -t GCC -b RELEASE -p MdeModulePkg/KirinHardwareProbe.dsc -m HardwareProbe/HardwareProbe.inf
find Build -type f -name HardwareProbe.efi -exec file {} \;
```

Copy HardwareProbe.efi onto the existing EFI Shell test USB, run it from Shell with the correct fsN: drive and photograph/output its results. The UEFI application executes only on the boot processor; it cannot directly report other cores' MPIDR registers.
