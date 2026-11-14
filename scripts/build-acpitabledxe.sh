#!/usr/bin/env bash
# Builds EDK II's stock AcpiTableDxe driver (MdeModulePkg) for AArch64.
# V11-A.nsh loads it first; it provides the ACPI table protocol that
# KirinAcpiSessionLoaderAuto.efi uses to install the tables.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
[[ -f "$EDK2/edksetup.sh" ]] || { echo "Expected EDK2 at $EDK2" >&2; exit 1; }
cd "$EDK2"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
set +u; source edksetup.sh; set -u
build -a AARCH64 -t GCC -b RELEASE -p MdeModulePkg/MdeModulePkg.dsc \
      -m MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableDxe.inf
OUT=$(find Build/MdeModule -type f -name AcpiTableDxe.efi -path "*RELEASE*" | head -n1)
test -n "$OUT" || { echo "Build finished but EFI missing" >&2; exit 1; }
mkdir -p "$ROOT/artifacts/boot"
cp "$OUT" "$ROOT/artifacts/boot/AcpiTableDxe.efi"
file "$ROOT/artifacts/boot/AcpiTableDxe.efi"
sha256sum "$ROOT/artifacts/boot/AcpiTableDxe.efi"
