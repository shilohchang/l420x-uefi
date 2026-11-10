#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
[[ -f "$EDK2/edksetup.sh" ]] || { echo "Missing edk2 directory"; exit 1; }
mkdir -p "$EDK2/MdeModulePkg/KirinPixelProbe" "$EDK2/MdeModulePkg/KirinGopCompatDxe" "$ROOT/artifacts/gop"
cp "$ROOT"/projects/diagnostics/KirinPixelProbe/* "$EDK2/MdeModulePkg/KirinPixelProbe/"
cp "$ROOT"/projects/bootloaders/KirinGopCompatDxe/* "$EDK2/MdeModulePkg/KirinGopCompatDxe/"
cat > "$EDK2/MdeModulePkg/KirinGopCompat.dsc" <<'DSC'
[Defines]
  PLATFORM_NAME = KirinGopCompat
  PLATFORM_GUID = a3b30217-4d64-4313-a6b4-87c067a1ce4c
  PLATFORM_VERSION = 0.1
  DSC_SPECIFICATION = 0x0001001C
  OUTPUT_DIRECTORY = Build/KirinGopCompat
  SUPPORTED_ARCHITECTURES = AARCH64
  BUILD_TARGETS = RELEASE|DEBUG
  SKUID_IDENTIFIER = DEFAULT
[LibraryClasses]
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf
  StackCheckLib|MdePkg/Library/StackCheckLibNull/StackCheckLibNull.inf
[LibraryClasses.AARCH64]
  NULL|MdePkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf
[Components]
  MdeModulePkg/KirinPixelProbe/KirinPixelProbe.inf
  MdeModulePkg/KirinGopCompatDxe/KirinGopCompatDxe.inf
DSC
unset WORKSPACE EDK_TOOLS_PATH CONF_PATH PACKAGES_PATH
export WORKSPACE="$EDK2" EDK_TOOLS_PATH="$EDK2/BaseTools" CONF_PATH="$EDK2/Conf"
export GCC_AARCH64_PREFIX=aarch64-linux-gnu- GCC5_AARCH64_PREFIX=aarch64-linux-gnu-
cd "$EDK2"
# shellcheck disable=SC1091
source ./edksetup.sh
if grep -Eq '^\s*\*_GCC_AARCH64_CC_PATH\s*=' "$CONF_PATH/tools_def.txt"; then
  TOOLCHAIN=GCC
elif grep -Eq '^\s*\*_GCC5_AARCH64_CC_PATH\s*=' "$CONF_PATH/tools_def.txt"; then
  TOOLCHAIN=GCC5
else
  echo 'No AARCH64 GCC toolchain definition'; exit 1
fi
build -p MdeModulePkg/KirinGopCompat.dsc -a AARCH64 -t "$TOOLCHAIN" -b RELEASE
OUT="$EDK2/Build/KirinGopCompat/RELEASE_${TOOLCHAIN}/AARCH64"
for NAME in KirinPixelProbe KirinGopCompatDxe; do
  test -f "$OUT/$NAME.efi"
  cp "$OUT/$NAME.efi" "$ROOT/artifacts/gop/$NAME.efi"
done
printf 'Outputs:\n%s\n' "$ROOT/artifacts/gop/"*.efi
