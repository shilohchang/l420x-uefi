#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/diagnostics/KirinI2cXfer5"
[[ -f "$EDK2/edksetup.sh" ]] || { echo "Expected EDK2 at $EDK2" >&2; exit 1; }
mkdir -p "$EDK2/KirinI2cXfer5"
cp "$SRC/KirinI2cXfer5.c" "$SRC/KirinI2cXfer5.inf" "$EDK2/KirinI2cXfer5/"
cp "$SRC/KirinI2cXfer5.dsc" "$EDK2/KirinI2cXfer5.dsc"
cd "$EDK2"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
set +u; source edksetup.sh; set -u
build -a AARCH64 -t GCC -b RELEASE -p KirinI2cXfer5.dsc -m KirinI2cXfer5/KirinI2cXfer5.inf
OUT=$(find Build/KirinI2cXfer5 -type f -name KirinI2cXfer5.efi | head -n1)
test -n "$OUT" || { echo "Build finished but EFI missing" >&2; exit 1; }
mkdir -p "$ROOT/artifacts/diag"
cp "$OUT" "$ROOT/artifacts/diag/KirinI2cXfer5.efi"
file "$ROOT/artifacts/diag/KirinI2cXfer5.efi"
sha256sum "$ROOT/artifacts/diag/KirinI2cXfer5.efi"
