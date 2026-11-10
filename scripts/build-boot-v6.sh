#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
if [[ ! -f "$EDK2/edksetup.sh" ]]; then echo "Expected EDK2 at $EDK2" >&2; exit 1; fi
mkdir -p "$EDK2/KirinBootV6"
cp "$ROOT/projects/bootloaders/KirinBootV6/KirinBootV6.c" "$EDK2/KirinBootV6/"
cp "$ROOT/projects/bootloaders/KirinBootV6/KirinBootV6.inf" "$EDK2/KirinBootV6/"
cp "$ROOT/projects/bootloaders/KirinBootV6/KirinBootV6.dsc" "$EDK2/KirinBootV6.dsc"
cd "$EDK2"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
source edksetup.sh
build -a AARCH64 -t GCC -b RELEASE -p KirinBootV6.dsc -m KirinBootV6/KirinBootV6.inf
OUT=$(find Build/KirinBootV6 -type f -name KirinBootV6.efi | head -n1)
test -n "$OUT" || { echo "Build succeeded but EFI missing" >&2; exit 1; }
mkdir -p "$ROOT/artifacts/v6"
cp "$OUT" "$ROOT/artifacts/v6/KirinBootV6.efi"
file "$ROOT/artifacts/v6/KirinBootV6.efi"
sha256sum "$ROOT/artifacts/v6/KirinBootV6.efi"
