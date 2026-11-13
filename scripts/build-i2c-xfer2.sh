#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/diagnostics/KirinI2cXfer2"
[[ -f "$EDK2/edksetup.sh" ]] || { echo "Expected EDK2 at $EDK2" >&2; exit 1; }
mkdir -p "$EDK2/KirinI2cXfer2"
cp "$SRC/KirinI2cXfer2.c" "$SRC/KirinI2cXfer2.inf" "$EDK2/KirinI2cXfer2/"
cp "$SRC/KirinI2cXfer2.dsc" "$EDK2/KirinI2cXfer2.dsc"
cd "$EDK2"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
set +u; source edksetup.sh; set -u
build -a AARCH64 -t GCC -b RELEASE -p KirinI2cXfer2.dsc -m KirinI2cXfer2/KirinI2cXfer2.inf
OUT=$(find Build/KirinI2cXfer2 -type f -name KirinI2cXfer2.efi | head -n1)
test -n "$OUT" || { echo "Build finished but EFI missing" >&2; exit 1; }
mkdir -p "$ROOT/artifacts/diag"
cp "$OUT" "$ROOT/artifacts/diag/KirinI2cXfer2.efi"
file "$ROOT/artifacts/diag/KirinI2cXfer2.efi"
sha256sum "$ROOT/artifacts/diag/KirinI2cXfer2.efi"
