#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/bootloaders/KirinEbsGuard"
[[ -f "$EDK2/edksetup.sh" && -f "$SRC/KirinEbsGuard.c" ]] || { echo 'Wrong workspace or missing source' >&2; exit 1; }
mkdir -p "$EDK2/KirinEbsGuard" "$ROOT/artifacts/boot"
cp "$SRC"/KirinEbsGuard.c "$SRC"/KirinEbsGuard.inf "$EDK2/KirinEbsGuard/"
cat > "$EDK2/KirinEbsGuard.dsc" <<'DSC'
[Defines]
  PLATFORM_NAME                  = KirinEbsGuard
  PLATFORM_GUID                  = A83D1F60-7C25-4E9B-B04A-6D9E2F1C3A57
  PLATFORM_VERSION               = 7.0
  DSC_SPECIFICATION              = 0x0001001C
  OUTPUT_DIRECTORY               = Build/KirinEbsGuard
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
  KirinEbsGuard/KirinEbsGuard.inf
DSC
cd "$EDK2"
export GCC_AARCH64_PREFIX="${GCC_AARCH64_PREFIX:-aarch64-linux-gnu-}"
set +u; source edksetup.sh; set -u
build -a AARCH64 -t GCC -b RELEASE -p KirinEbsGuard.dsc
OUT=$(find Build/KirinEbsGuard -type f -path '*/AARCH64/KirinEbsGuard.efi' | head -n1)
test -n "$OUT" || { echo "KirinEbsGuard.efi missing" >&2; exit 1; }
cp "$OUT" "$ROOT/artifacts/boot/KirinEbsGuard.efi"
sha256sum "$ROOT/artifacts/boot/KirinEbsGuard.efi"
