#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/diagnostics/StageSsdtLoader"
[[ -f "$EDK2/edksetup.sh" ]] || { echo 'ERROR: edk2 not installed in repo' >&2; exit 1; }
command -v aarch64-linux-gnu-gcc >/dev/null || { echo 'ERROR: AArch64 GCC missing' >&2; exit 1; }
mkdir -p "$EDK2/StageSsdtLoader" "$ROOT/artifacts/v8"
cp "$SRC/StageSsdtLoader.c" "$SRC/StageSsdtLoader.inf" "$EDK2/StageSsdtLoader/"
cp "$SRC/StageSsdtLoader.dsc" "$EDK2/StageSsdtLoader.dsc"
export WORKSPACE="$EDK2" EDK_TOOLS_PATH="$EDK2/BaseTools" CONF_PATH="$EDK2/Conf"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
export GCC5_AARCH64_PREFIX="${GCC5_AARCH64_PREFIX:-aarch64-linux-gnu-}"
cd "$EDK2"
export PYTHON_COMMAND="${PYTHON_COMMAND:-python3}"
# edksetup.sh is an upstream environment script and can read unset variables.
set +u
source edksetup.sh >/dev/null
set -u
if grep -Eq '^[[:space:]]*\*_GCC_AARCH64_CC_PATH[[:space:]]*=' "$CONF_PATH/tools_def.txt"; then TC=GCC; else TC=GCC5; fi
build -a AARCH64 -t "$TC" -b RELEASE -p StageSsdtLoader.dsc -m StageSsdtLoader/StageSsdtLoader.inf
OUT="$EDK2/Build/StageSsdtLoader/RELEASE_${TC}/AARCH64/StageSsdtLoader.efi"
[[ -f "$OUT" ]] || { echo "ERROR: Missing $OUT" >&2; exit 1; }
cp "$OUT" "$ROOT/artifacts/v8/StageSsdtLoader.efi"
file "$ROOT/artifacts/v8/StageSsdtLoader.efi"
sha256sum "$ROOT/artifacts/v8/StageSsdtLoader.efi"
