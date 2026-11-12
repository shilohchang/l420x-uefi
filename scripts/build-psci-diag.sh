#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/diagnostics/KirinPsciDiag"
[[ -f "$EDK2/edksetup.sh" ]] || { echo "Expected EDK2 at $EDK2" >&2; exit 1; }
mkdir -p "$EDK2/KirinPsciDiag"
cp "$SRC/KirinPsciDiag.c" "$SRC/KirinPsciDiag.inf" "$EDK2/KirinPsciDiag/"
cp "$SRC/KirinPsciDiag.dsc" "$EDK2/KirinPsciDiag.dsc"
cd "$EDK2"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
set +u; source edksetup.sh; set -u
build -a AARCH64 -t GCC -b RELEASE -p KirinPsciDiag.dsc -m KirinPsciDiag/KirinPsciDiag.inf
OUT=$(find Build/KirinPsciDiag -type f -name KirinPsciDiag.efi | head -n1)
test -n "$OUT" || { echo "Build finished but EFI missing" >&2; exit 1; }
mkdir -p "$ROOT/artifacts/diag"
cp "$OUT" "$ROOT/artifacts/diag/KirinPsciDiag.efi"
file "$ROOT/artifacts/diag/KirinPsciDiag.efi"
sha256sum "$ROOT/artifacts/diag/KirinPsciDiag.efi"
