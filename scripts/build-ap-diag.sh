#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/diagnostics/KirinApDiag"
[[ -f "$EDK2/edksetup.sh" ]] || { echo "Expected EDK2 at $EDK2" >&2; exit 1; }
mkdir -p "$EDK2/KirinApDiag"
cp "$SRC/KirinApDiag.c" "$SRC/KirinApDiag.inf" "$EDK2/KirinApDiag/"
cp "$SRC/KirinApDiag.dsc" "$EDK2/KirinApDiag.dsc"
cd "$EDK2"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
set +u; source edksetup.sh; set -u
build -a AARCH64 -t GCC -b RELEASE -p KirinApDiag.dsc -m KirinApDiag/KirinApDiag.inf
OUT=$(find Build/KirinApDiag -type f -name KirinApDiag.efi | head -n1)
test -n "$OUT" || { echo "Build finished but EFI missing" >&2; exit 1; }
mkdir -p "$ROOT/artifacts/diag"
cp "$OUT" "$ROOT/artifacts/diag/KirinApDiag.efi"
file "$ROOT/artifacts/diag/KirinApDiag.efi"
sha256sum "$ROOT/artifacts/diag/KirinApDiag.efi"
