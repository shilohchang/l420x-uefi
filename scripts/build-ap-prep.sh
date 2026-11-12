#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/bootloaders/KirinApPrep"
[[ -f "$EDK2/edksetup.sh" ]] || { echo "Expected EDK2 at $EDK2" >&2; exit 1; }
mkdir -p "$EDK2/KirinApPrep"
cp "$SRC/KirinApPrep.c" "$SRC/ApPrepStub.S" "$SRC/KirinApPrep.inf" "$EDK2/KirinApPrep/"
cp "$SRC/KirinApPrep.dsc" "$EDK2/KirinApPrep.dsc"
cd "$EDK2"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
set +u; source edksetup.sh; set -u
build -a AARCH64 -t GCC -b RELEASE -p KirinApPrep.dsc -m KirinApPrep/KirinApPrep.inf
OUT=$(find Build/KirinApPrep -type f -name KirinApPrep.efi | head -n1)
test -n "$OUT" || { echo "Build finished but EFI missing" >&2; exit 1; }
mkdir -p "$ROOT/artifacts/boot"
cp "$OUT" "$ROOT/artifacts/boot/KirinApPrep.efi"
file "$ROOT/artifacts/boot/KirinApPrep.efi"
sha256sum "$ROOT/artifacts/boot/KirinApPrep.efi"
