#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/diagnostics/KirinI2cGpioProbe"
[[ -f "$EDK2/edksetup.sh" ]] || { echo "Expected EDK2 at $EDK2" >&2; exit 1; }
mkdir -p "$EDK2/KirinI2cGpioProbe"
cp "$SRC/KirinI2cGpioProbe.c" "$SRC/KirinI2cGpioProbe.inf" "$EDK2/KirinI2cGpioProbe/"
cp "$SRC/KirinI2cGpioProbe.dsc" "$EDK2/KirinI2cGpioProbe.dsc"
cd "$EDK2"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
set +u; source edksetup.sh; set -u
build -a AARCH64 -t GCC -b RELEASE -p KirinI2cGpioProbe.dsc -m KirinI2cGpioProbe/KirinI2cGpioProbe.inf
OUT=$(find Build/KirinI2cGpioProbe -type f -name KirinI2cGpioProbe.efi | head -n1)
test -n "$OUT" || { echo "Build finished but EFI missing" >&2; exit 1; }
mkdir -p "$ROOT/artifacts/diag"
cp "$OUT" "$ROOT/artifacts/diag/KirinI2cGpioProbe.efi"
file "$ROOT/artifacts/diag/KirinI2cGpioProbe.efi"
sha256sum "$ROOT/artifacts/diag/KirinI2cGpioProbe.efi"
