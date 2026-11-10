#!/usr/bin/env python3
"""Run from edk2 root. Makes separate DSC and copies read-only EFI app."""
from pathlib import Path
import shutil
root=Path.cwd()
assert (root/'MdeModulePkg'/'MdeModulePkg.dsc').is_file(), 'Run from edk2 root'
src=Path(__file__).resolve().parent/'AcpiInspector'
out=root/'AcpiInspector'
out.mkdir(exist_ok=True)
for file in ['AcpiInspector.c','AcpiInspector.inf']:
    shutil.copy2(src/file,out/file)
dsc=(root/'MdeModulePkg'/'MdeModulePkg.dsc').read_text()
dsc+='\n[Components.AARCH64]\n  AcpiInspector/AcpiInspector.inf\n'
p=root/'MdeModulePkg'/'KirinAcpiInspector.dsc'
p.write_text(dsc)
print('Created:',out)
print('Created:',p)
