#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2="$ROOT/edk2"
SRC="$EDK2/SessionLoader/SessionLoader.c"
DSC="$EDK2/MdeModulePkg/KirinSessionLoader.dsc"
DEST="$ROOT/artifacts/boot/KirinAcpiSessionLoaderAuto.efi"
[[ -f "$SRC" && -f "$DSC" ]] || { echo "ERROR: Missing SessionLoader source or DSC"; exit 1; }
mkdir -p "$ROOT/projects/bootloaders/KirinAcpiSessionLoaderAuto" "$(dirname "$DEST")"
# Save a reproducible source snapshot in the tracked project tree.
cp -a "$EDK2/SessionLoader/." "$ROOT/projects/bootloaders/KirinAcpiSessionLoaderAuto/"
cp "$DSC" "$ROOT/projects/bootloaders/KirinAcpiSessionLoaderAuto/KirinSessionLoader.dsc"
cp "$SRC" "$ROOT/projects/bootloaders/KirinAcpiSessionLoaderAuto/SessionLoader-original.c.txt"
python3 - "$SRC" <<'PY'
from pathlib import Path
import re, sys
p=Path(sys.argv[1]); text=p.read_text()
# Remove *output messages only*, not checks, error handling, or ACPI installation.
patterns=(
    r'DO NOT BOOT WINDOWS OR LINUX',
    r'REBOOT AFTER TEST',
    r'SESSION EXPERIMENT ONLY',
    r'This does NOT establish OS compatibility',
)
# Limit matching to *single-line* Print statements, never span into control flow.
lines=text.splitlines(keepends=True)
removed=[]
out=[]
for line in lines:
    stripped=line.strip()
    if (stripped.startswith('Print ') or stripped.startswith('Print(')) and stripped.endswith(';') and any(re.search(pat,stripped,re.I) for pat in patterns):
        removed.append(stripped)
        out.append('\n')
    else:
        out.append(line)
new=''.join(out)
# Verify no interactive key wait in selected implementation.
if 'ReadKeyStroke (' in new or 'WaitForEvent (' in new:
    raise SystemExit('ERROR: Selected SessionLoader still contains interactive key handling; refusing automated build')
if new != text:
    p.write_text(new)
print('Removed warning Print statements:',len(removed))
for stmt in removed: print(' -',stmt[:130])
PY
cp -a "$EDK2/SessionLoader/." "$ROOT/projects/bootloaders/KirinAcpiSessionLoaderAuto/"
export WORKSPACE="$EDK2" EDK_TOOLS_PATH="$EDK2/BaseTools" CONF_PATH="$EDK2/Conf"
unset PACKAGES_PATH || true
export GCC_AARCH64_PREFIX=aarch64-linux-gnu-
export GCC5_AARCH64_PREFIX=aarch64-linux-gnu-
cd "$EDK2"
source edksetup.sh >/dev/null
if grep -Eq '^[[:space:]]*\*_GCC_AARCH64_CC_PATH[[:space:]]*=' "$CONF_PATH/tools_def.txt"; then TC=GCC; else TC=GCC5; fi
build -p MdeModulePkg/KirinSessionLoader.dsc -a AARCH64 -t "$TC" -b RELEASE
mapfile -t candidates < <(find "$EDK2/Build" -path "*/RELEASE_${TC}/AARCH64/KirinAcpiSessionLoader.efi" -type f)
if (( ${#candidates[@]} != 1 )); then
 echo "ERROR: Expected exactly one final EFI output; found ${#candidates[@]}"; printf '%s\n' "${candidates[@]}"; exit 1
fi
cp "${candidates[0]}" "$DEST"
file "$DEST"
sha256sum "$DEST"
echo "OUTPUT=$DEST"
