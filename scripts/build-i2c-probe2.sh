#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/diagnostics/KirinI2cProbe2"
[[ -f "$EDK2/edksetup.sh" ]] || { echo "Expected EDK2 at $EDK2" >&2; exit 1; }
mkdir -p "$EDK2/KirinI2cProbe2"
cp "$SRC/KirinI2cProbe2.c" "$SRC/KirinI2cProbe2.inf" "$EDK2/KirinI2cProbe2/"
cp "$SRC/KirinI2cProbe2.dsc" "$EDK2/KirinI2cProbe2.dsc"
cd "$EDK2"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
set +u; source edksetup.sh; set -u
build -a AARCH64 -t GCC -b RELEASE -p KirinI2cProbe2.dsc -m KirinI2cProbe2/KirinI2cProbe2.inf
OUT=$(find Build/KirinI2cProbe2 -type f -name KirinI2cProbe2.efi | head -n1)
test -n "$OUT" || { echo "Build finished but EFI missing" >&2; exit 1; }
mkdir -p "$ROOT/artifacts/diag"
cp "$OUT" "$ROOT/artifacts/diag/KirinI2cProbe2.efi"
file "$ROOT/artifacts/diag/KirinI2cProbe2.efi"
sha256sum "$ROOT/artifacts/diag/KirinI2cProbe2.efi"
