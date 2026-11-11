#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/diagnostics/KirinGopDiagV3"
[[ -f "$EDK2/edksetup.sh" ]] || { echo "Expected EDK2 at $EDK2" >&2; exit 1; }
mkdir -p "$EDK2/KirinGopDiagV3"
cp "$SRC/KirinGopDiagV3.c" "$SRC/KirinGopDiagV3.inf" "$EDK2/KirinGopDiagV3/"
cp "$SRC/KirinGopDiagV3.dsc" "$EDK2/KirinGopDiagV3.dsc"
cd "$EDK2"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
set +u; source edksetup.sh; set -u
build -a AARCH64 -t GCC -b RELEASE -p KirinGopDiagV3.dsc -m KirinGopDiagV3/KirinGopDiagV3.inf
OUT=$(find Build/KirinGopDiagV3 -type f -name KirinGopDiagV3.efi | head -n1)
test -n "$OUT" || { echo "Build finished but EFI missing" >&2; exit 1; }
mkdir -p "$ROOT/artifacts/gop/v3diag"
cp "$OUT" "$ROOT/artifacts/gop/v3diag/KirinGopDiagV3.efi"
file "$ROOT/artifacts/gop/v3diag/KirinGopDiagV3.efi"
sha256sum "$ROOT/artifacts/gop/v3diag/KirinGopDiagV3.efi"
