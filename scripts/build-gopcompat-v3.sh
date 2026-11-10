#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/bootloaders/KirinGopCompatV3"
[[ -f "$EDK2/edksetup.sh" && -f "$SRC/KirinGopCompatV3.c" ]] || { echo "Missing EDK2 or V3 source"; exit 1; }
mkdir -p "$EDK2/MdeModulePkg/KirinGopCompatV3" "$ROOT/artifacts/gop"
cp "$SRC/KirinGopCompatV3.c" "$SRC/KirinGopCompatV3.inf" "$EDK2/MdeModulePkg/KirinGopCompatV3/"
cat > "$EDK2/MdeModulePkg/KirinGopCompatV3.dsc" <<'DSC'
[Defines]
  PLATFORM_NAME = KirinGopCompatV3
  PLATFORM_GUID = 5b88f28d-4f8b-46aa-87da-024c1a951af0
  PLATFORM_VERSION = 0.3
  DSC_SPECIFICATION = 0x0001001C
  OUTPUT_DIRECTORY = Build/KirinGopCompatV3
  SUPPORTED_ARCHITECTURES = AARCH64
  BUILD_TARGETS = RELEASE|DEBUG
  SKUID_IDENTIFIER = DEFAULT
[LibraryClasses]
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
  MdeModulePkg/KirinGopCompatV3/KirinGopCompatV3.inf
DSC
unset WORKSPACE EDK_TOOLS_PATH CONF_PATH PACKAGES_PATH
export WORKSPACE="$EDK2" EDK_TOOLS_PATH="$EDK2/BaseTools" CONF_PATH="$EDK2/Conf"
export GCC_AARCH64_PREFIX=aarch64-linux-gnu- GCC5_AARCH64_PREFIX=aarch64-linux-gnu-
cd "$EDK2"
source ./edksetup.sh
if grep -Eq '^[[:space:]]*\*_GCC_AARCH64_CC_PATH[[:space:]]*=' "$CONF_PATH/tools_def.txt"; then
  TOOLCHAIN=GCC
elif grep -Eq '^[[:space:]]*\*_GCC5_AARCH64_CC_PATH[[:space:]]*=' "$CONF_PATH/tools_def.txt"; then
  TOOLCHAIN=GCC5
else
  echo 'No AARCH64 GCC toolchain definition'; exit 1
fi
build -p MdeModulePkg/KirinGopCompatV3.dsc -a AARCH64 -t "$TOOLCHAIN" -b RELEASE
EFI="$EDK2/Build/KirinGopCompatV3/RELEASE_${TOOLCHAIN}/AARCH64/KirinGopCompatV3.efi"
test -f "$EFI"
cp "$EFI" "$ROOT/artifacts/gop/KirinGopCompatV3.efi"
file "$ROOT/artifacts/gop/KirinGopCompatV3.efi"
sha256sum "$ROOT/artifacts/gop/KirinGopCompatV3.efi"
