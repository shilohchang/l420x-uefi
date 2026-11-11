#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/bootloaders/KirinGopPinV7"
[[ -f "$EDK2/edksetup.sh" && -f "$SRC/KirinGopPinV7.c" ]] || { echo 'Wrong workspace or missing source' >&2; exit 1; }
mkdir -p "$EDK2/KirinGopPinV7" "$ROOT/artifacts/gop/v7"
cp "$SRC"/KirinGopPinV7.c "$SRC"/KirinGopPinA.inf "$SRC"/KirinGopPinB.inf "$EDK2/KirinGopPinV7/"
cat > "$EDK2/KirinGopPinV7.dsc" <<'DSC'
[Defines]
  PLATFORM_NAME                  = KirinGopPinV7
  PLATFORM_GUID                  = 5F0E6C1A-33B7-4A49-9C0D-7A1E2B3C4D5E
  PLATFORM_VERSION               = 7.0
  DSC_SPECIFICATION              = 0x0001001C
  OUTPUT_DIRECTORY               = Build/KirinGopPinV7
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT

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
  KirinGopPinV7/KirinGopPinA.inf
  KirinGopPinV7/KirinGopPinB.inf
DSC
cd "$EDK2"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
set +u; source edksetup.sh; set -u
build -a AARCH64 -t GCC -b RELEASE -p KirinGopPinV7.dsc
for v in A B; do
  OUT=$(find Build/KirinGopPinV7 -type f -path '*/AARCH64/KirinGopPin'$v'.efi' | head -n1)
  test -n "$OUT" || { echo "KirinGopPin$v.efi missing" >&2; exit 1; }
  cp "$OUT" "$ROOT/artifacts/gop/v7/KirinGopPin$v.efi"
  sha256sum "$ROOT/artifacts/gop/v7/KirinGopPin$v.efi"
done
