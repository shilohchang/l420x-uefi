#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SOURCE="$ROOT/projects/bootloaders/KirinGopCompatDxe"
[[ -f "$SOURCE/KirinGopCompatDxe.c" && -f "$EDK2/edksetup.sh" ]] || { echo 'Missing source or edk2'; exit 1; }
mkdir -p "$EDK2/MdeModulePkg/KirinGopCompatDxe" "$ROOT/artifacts/gop"
cp "$SOURCE/KirinGopCompatDxe.c" "$SOURCE/KirinGopCompatDxe.inf" "$EDK2/MdeModulePkg/KirinGopCompatDxe/"
cat > "$EDK2/MdeModulePkg/KirinGopCompatV2.dsc" <<'DSC'
[Defines]
  PLATFORM_NAME = KirinGopCompatV2
  PLATFORM_GUID = f4d17835-0a24-4b91-9a3f-38b8526b18d8
  PLATFORM_VERSION = 0.2
  DSC_SPECIFICATION = 0x0001001C
  OUTPUT_DIRECTORY = Build/KirinGopCompatV2
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
  MdeModulePkg/KirinGopCompatDxe/KirinGopCompatDxe.inf
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
build -p MdeModulePkg/KirinGopCompatV2.dsc -a AARCH64 -t "$TOOLCHAIN" -b RELEASE
EFI="$EDK2/Build/KirinGopCompatV2/RELEASE_${TOOLCHAIN}/AARCH64/KirinGopCompatDxe.efi"
test -f "$EFI"
cp "$EFI" "$ROOT/artifacts/gop/KirinGopCompatV2.efi"
file "$ROOT/artifacts/gop/KirinGopCompatV2.efi"
sha256sum "$ROOT/artifacts/gop/KirinGopCompatV2.efi"
