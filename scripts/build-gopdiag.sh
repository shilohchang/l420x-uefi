#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$ROOT/projects/diagnostics/KirinGopDiag"
STAGE="$EDK2/MdeModulePkg/KirinGopDiag"
DSC="$EDK2/MdeModulePkg/KirinGopDiag.dsc"
if [[ ! -d "$EDK2/BaseTools" || ! -f "$SRC/KirinGopDiag.inf" ]]; then
  echo 'Missing edk2 source or KirinGopDiag.inf' >&2
  exit 1
fi
mkdir -p "$STAGE" "$ROOT/artifacts/gop"
cp "$SRC/KirinGopDiag.c" "$SRC/KirinGopDiag.inf" "$STAGE/"
cat > "$DSC" <<'DSC_EOF'
[Defines]
  PLATFORM_NAME                  = KirinGopDiag
  PLATFORM_GUID                  = 0d624e49-bf5d-4b68-b5d9-ea775d6319ba
  PLATFORM_VERSION               = 1.0
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/KirinGopDiag
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE
  SKUID_IDENTIFIER               = DEFAULT

[LibraryClasses]
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf

[Components]
  MdeModulePkg/KirinGopDiag/KirinGopDiag.inf
DSC_EOF
cd "$EDK2"
export WORKSPACE="$EDK2"
export PACKAGES_PATH="$EDK2"
export EDK_TOOLS_PATH="$EDK2/BaseTools"
# EDK2 bootstrap config; assumes BaseTools is available.
source edksetup.sh
if ! command -v build >/dev/null 2>&1; then
  echo 'EDK2 build command unavailable. Ensure BaseTools and python environment are installed.' >&2
  exit 1
fi
build -p MdeModulePkg/KirinGopDiag.dsc -a AARCH64 -t GCC -b RELEASE -n "$(nproc)"
EFI="$EDK2/Build/KirinGopDiag/RELEASE_GCC5/AARCH64/KirinGopDiag.efi"
if [[ ! -f "$EFI" ]]; then
  EFI="$(find "$EDK2/Build/KirinGopDiag" -type f -path '*/AARCH64/KirinGopDiag.efi' -print -quit)"
fi
if [[ -z "$EFI" || ! -f "$EFI" ]]; then
  echo 'Build succeeded but EFI artifact not found.' >&2
  exit 1
fi
cp "$EFI" "$ROOT/artifacts/gop/KirinGopDiag-V2-AARCH64.efi"
file "$ROOT/artifacts/gop/KirinGopDiag-V2-AARCH64.efi"
sha256sum "$ROOT/artifacts/gop/KirinGopDiag-V2-AARCH64.efi"
echo "Saved to $ROOT/artifacts/gop/KirinGopDiag-V2-AARCH64.efi"
