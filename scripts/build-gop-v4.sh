#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/bootloaders/KirinGopCompatV4"
test -f "$EDK2/edksetup.sh"
mkdir -p "$EDK2/MdeModulePkg/KirinGopCompatV4" "$ROOT/artifacts/gop/v4"
cp "$SRC"/*.c "$SRC"/*.inf "$EDK2/MdeModulePkg/KirinGopCompatV4/"
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
  echo 'No AARCH64 GCC toolchain'; exit 1
fi
for label in Meta Native Dual; do
  name="KirinGopV4${label}"
  cat > "$EDK2/MdeModulePkg/${name}.dsc" <<DSC
[Defines]
  PLATFORM_NAME = ${name}
  PLATFORM_GUID = 3e46bef1-00cf-4897-a1e7-201f244e8a6f
  PLATFORM_VERSION = 0.4
  DSC_SPECIFICATION = 0x0001001C
  OUTPUT_DIRECTORY = Build/${name}
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
  MdeModulePkg/KirinGopCompatV4/${name}.inf
DSC
  echo "=== Building ${name} ==="
  build -p "MdeModulePkg/${name}.dsc" -a AARCH64 -t "$TOOLCHAIN" -b RELEASE
  EFI="$EDK2/Build/${name}/RELEASE_${TOOLCHAIN}/AARCH64/${name}.efi"
  test -f "$EFI" || { echo "Missing $EFI"; exit 1; }
  cp "$EFI" "$ROOT/artifacts/gop/v4/${name}.efi"
done
file "$ROOT/artifacts/gop/v4/"*.efi
sha256sum "$ROOT/artifacts/gop/v4/"*.efi
