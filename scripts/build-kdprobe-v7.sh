#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/diagnostics/KirinKdProbe"
STAGE="$EDK2/KirinKdProbe"
if [[ ! -f "$EDK2/edksetup.sh" || ! -f "$SRC/KirinKdProbe.c" ]]; then
  echo 'Expected ~/l420x-uefi/edk2 and projects/diagnostics/KirinKdProbe sources' >&2
  exit 1
fi
command -v aarch64-linux-gnu-gcc >/dev/null || { echo 'AArch64 GCC missing' >&2; exit 1; }
mkdir -p "$STAGE" "$ROOT/artifacts/v7"
cp "$SRC/KirinKdProbe.c" "$SRC/KirinKdProbe.inf" "$STAGE/"
cp "$SRC/KirinKdProbe.dsc" "$EDK2/KirinKdProbe.dsc"
cd "$EDK2"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
source edksetup.sh
build -a AARCH64 -t GCC -b RELEASE -p KirinKdProbe.dsc -m KirinKdProbe/KirinKdProbe.inf
OUT="$EDK2/Build/KirinKdProbe/RELEASE_GCC/AARCH64/KirinKdProbe.efi"
if [[ ! -f "$OUT" ]]; then
  OUT="$(find "$EDK2/Build/KirinKdProbe" -type f -name KirinKdProbe.efi -print -quit)"
fi
[[ -n "$OUT" && -f "$OUT" ]] || { echo 'Build finished without EFI output' >&2; exit 1; }
cp "$OUT" "$ROOT/artifacts/v7/KirinKdProbe.efi"
file "$ROOT/artifacts/v7/KirinKdProbe.efi"
sha256sum "$ROOT/artifacts/v7/KirinKdProbe.efi"
