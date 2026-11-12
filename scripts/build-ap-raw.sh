#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/diagnostics/KirinApRaw"
[[ -f "$EDK2/edksetup.sh" ]] || { echo "Expected EDK2 at $EDK2" >&2; exit 1; }
mkdir -p "$EDK2/KirinApRaw"
cp "$SRC/KirinApRaw.c" "$SRC/ApStub.S" "$SRC/KirinApRaw.inf" "$EDK2/KirinApRaw/"
cp "$SRC/KirinApRaw.dsc" "$EDK2/KirinApRaw.dsc"
cd "$EDK2"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
set +u; source edksetup.sh; set -u
build -a AARCH64 -t GCC -b RELEASE -p KirinApRaw.dsc -m KirinApRaw/KirinApRaw.inf
OUT=$(find Build/KirinApRaw -type f -name KirinApRaw.efi | head -n1)
test -n "$OUT" || { echo "Build finished but EFI missing" >&2; exit 1; }
mkdir -p "$ROOT/artifacts/diag"
cp "$OUT" "$ROOT/artifacts/diag/KirinApRaw.efi"
file "$ROOT/artifacts/diag/KirinApRaw.efi"
sha256sum "$ROOT/artifacts/diag/KirinApRaw.efi"
