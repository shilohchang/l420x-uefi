#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DTB="${1:-$ROOT/artifacts/v8/acpi-discovery/source/live.dtb}"
OUT="$ROOT/artifacts/v8/acpi-discovery/output"
[[ -f "$DTB" ]] || { echo "Missing DTB: $DTB" >&2; exit 1; }
python3 "$ROOT/projects/acpi/Kirin9000C-DeviceDiscovery/tools/discover.py" "$DTB" --out "$OUT"
echo "Inventory: $OUT/devices.csv"
echo "ASL drafts: $OUT/asl/"
if command -v iasl >/dev/null; then
  mkdir -p "$ROOT/artifacts/v8/acpi-discovery/aml"
  for f in "$OUT"/asl/*.asl; do
    (cd "$ROOT/artifacts/v8/acpi-discovery/aml" && iasl -tc "$f")
  done
else
  echo 'NOTE: iasl missing; install acpica-tools before building AML.'
fi
