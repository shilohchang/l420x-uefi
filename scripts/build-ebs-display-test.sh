#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/diagnostics/KirinEbsDisplayTest"
[[ -f "$EDK2/edksetup.sh" ]] || { echo "Expected EDK2 at $EDK2" >&2; exit 1; }
mkdir -p "$EDK2/KirinEbsDisplayTest"
cp "$SRC/KirinEbsDisplayTest.c" "$SRC/KirinEbsDisplayTest.inf" "$EDK2/KirinEbsDisplayTest/"
cp "$SRC/KirinEbsDisplayTest.dsc" "$EDK2/KirinEbsDisplayTest.dsc"
cd "$EDK2"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
set +u; source edksetup.sh; set -u
build -a AARCH64 -t GCC -b RELEASE -p KirinEbsDisplayTest.dsc -m KirinEbsDisplayTest/KirinEbsDisplayTest.inf
OUT=$(find Build/KirinEbsDisplayTest -type f -name KirinEbsDisplayTest.efi | head -n1)
test -n "$OUT" || { echo "Build finished but EFI missing" >&2; exit 1; }
mkdir -p "$ROOT/artifacts/diag"
cp "$OUT" "$ROOT/artifacts/diag/KirinEbsDisplayTest.efi"
file "$ROOT/artifacts/diag/KirinEbsDisplayTest.efi"
sha256sum "$ROOT/artifacts/diag/KirinEbsDisplayTest.efi"
