#!/usr/bin/env python3
"""Create local-only DSC for edk2 AcpiTableDxe build; does not modify firmware."""
from pathlib import Path
import re
root=Path.cwd()
orig=root/'MdeModulePkg/MdeModulePkg.dsc'
out=root/'MdeModulePkg/KirinAcpiTest.dsc'
inf=root/'MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableDxe.inf'
if not orig.is_file() or not inf.is_file():
    raise SystemExit('Run this script at the EDK II repository root')
lines=orig.read_text(encoding='utf-8-sig').splitlines()
component_header=re.compile(r'^\s*\[Components(?:\.[^\]]+)?\]\s*(?:#.*)?$',re.I)
idx=next((i for i,line in enumerate(lines) if component_header.match(line) and 'common' not in line.lower()),None)
if idx is None:
    raise SystemExit('No suitable [Components] section in MdeModulePkg.dsc')
module='  MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableDxe.inf'
if not any(x.strip()==module.strip() for x in lines):
    lines.insert(idx+1,module)
out.write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('Created:',out)
print('Registered module:',module.strip())
print('Verify:', 'grep -n -F "AcpiTableDxe.inf" MdeModulePkg/KirinAcpiTest.dsc')
