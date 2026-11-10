#!/usr/bin/env python3
"""Register a read-only Kirin9000C EfiProbe module in a local edk2 checkout."""
from pathlib import Path
import re
import shutil
import sys

workspace = Path.cwd()
source = Path(sys.argv[1]).expanduser().resolve() if len(sys.argv)>1 else None
base = workspace / 'MdeModulePkg' / 'MdeModulePkg.dsc'
output = workspace / 'MdeModulePkg' / 'EfiProbe.dsc'
if not base.is_file():
    raise SystemExit('ERROR: Run inside edk2 repository root (MdeModulePkg/MdeModulePkg.dsc missing).')
if source is not None:
    for name in ('EfiProbe.c','EfiProbe.inf'):
        file = source / name
        if not file.is_file():
            raise SystemExit(f'ERROR: Missing source file {file}')
    dest = workspace / 'EfiProbe'
    dest.mkdir(exist_ok=True)
    for name in ('EfiProbe.c','EfiProbe.inf'):
        shutil.copy2(source/name, dest/name)
if not (workspace / 'EfiProbe/EfiProbe.inf').is_file():
    raise SystemExit('ERROR: EfiProbe/EfiProbe.inf missing. Extract ZIP and copy source first.')
lines = base.read_text(encoding='utf-8-sig').splitlines()
section = re.compile(r'^\s*\[Components(?:\.[^\]]+)?\]\s*(?:#.*)?$', re.I)
pos = next((i for i,line in enumerate(lines) if section.match(line) and line.strip().lower().startswith('[components]')), None)
if pos is None:
    raise SystemExit('ERROR: [Components] section absent. Send the DSC section to the assistant.')
lines.insert(pos+1,'  EfiProbe/EfiProbe.inf')
output.write_text('\n'.join(lines)+'\n', encoding='utf-8')
assert 'EfiProbe/EfiProbe.inf' in output.read_text()
print('OK: source:', workspace / 'EfiProbe/EfiProbe.inf')
print('OK: platform:', output)
print('Registered after line',pos+1, 'header:',repr(lines[pos]))
