#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/diagnostics/KirinI2cXfer3"
[[ -f "$EDK2/edksetup.sh" ]] || { echo "Expected EDK2 at $EDK2" >&2; exit 1; }
mkdir -p "$EDK2/KirinI2cXfer3"
cp "$SRC/KirinI2cXfer3.c" "$SRC/KirinI2cXfer3.inf" "$EDK2/KirinI2cXfer3/"
cp "$SRC/KirinI2cXfer3.dsc" "$EDK2/KirinI2cXfer3.dsc"
cd "$EDK2"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
set +u; source edksetup.sh; set -u
build -a AARCH64 -t GCC -b RELEASE -p KirinI2cXfer3.dsc -m KirinI2cXfer3/KirinI2cXfer3.inf
OUT=$(find Build/KirinI2cXfer3 -type f -name KirinI2cXfer3.efi | head -n1)
test -n "$OUT" || { echo "Build finished but EFI missing" >&2; exit 1; }
mkdir -p "$ROOT/artifacts/diag"
cp "$OUT" "$ROOT/artifacts/diag/KirinI2cXfer3.efi"
file "$ROOT/artifacts/diag/KirinI2cXfer3.efi"
sha256sum "$ROOT/artifacts/diag/KirinI2cXfer3.efi"
