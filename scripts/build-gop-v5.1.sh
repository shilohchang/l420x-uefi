#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/bootloaders/KirinGopCompatV5_1"
NAME=KirinGopV51Safe
[[ -f "$EDK2/edksetup.sh" && -f "$SRC/$NAME.c" ]] || { echo 'Wrong workspace or missing source'; exit 1; }
mkdir -p "$EDK2/MdeModulePkg/KirinGopCompatV5_1" "$ROOT/artifacts/gop/v5.1"
cp "$SRC/$NAME.c" "$SRC/$NAME.inf" "$EDK2/MdeModulePkg/KirinGopCompatV5_1/"
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
  echo 'Missing cross-compiler entry'; exit 1
fi
cat > "MdeModulePkg/${NAME}.dsc" <<DSC
[Defines]
  PLATFORM_NAME = ${NAME}
  PLATFORM_GUID = 3e7b51f9-25cb-4d62-9a5f-1011d5313341
  PLATFORM_VERSION = 0.5.1
  DSC_SPECIFICATION = 0x0001001C
  OUTPUT_DIRECTORY = Build/${NAME}
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
  MdeModulePkg/KirinGopCompatV5_1/${NAME}.inf
DSC
build -p "MdeModulePkg/${NAME}.dsc" -a AARCH64 -t "$TOOLCHAIN" -b RELEASE
EFI="$EDK2/Build/${NAME}/RELEASE_${TOOLCHAIN}/AARCH64/${NAME}.efi"
test -f "$EFI"
cp "$EFI" "$ROOT/artifacts/gop/v5.1/$NAME.efi"
file "$ROOT/artifacts/gop/v5.1/$NAME.efi"
sha256sum "$ROOT/artifacts/gop/v5.1/$NAME.efi"
