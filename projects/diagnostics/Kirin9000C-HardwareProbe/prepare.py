#!/usr/bin/env python3
from pathlib import Path
import shutil
root=Path(__file__).resolve().parent
edk=Path.cwd()
if not (edk/'MdeModulePkg/MdeModulePkg.dsc').exists():
    raise SystemExit('Run this script from the edk2 root')
dst=edk/'HardwareProbe'
dst.mkdir(exist_ok=True)
for name in ('HardwareProbe.c','HardwareProbe.inf'):
    shutil.copy2(root/name,dst/name)
dsc=edk/'MdeModulePkg/KirinHardwareProbe.dsc'
text=(edk/'MdeModulePkg/MdeModulePkg.dsc').read_text()
text += '\n[Components.AARCH64]\n  HardwareProbe/HardwareProbe.inf\n'
dsc.write_text(text)
print('Created',dsc)
print('Registered HardwareProbe/HardwareProbe.inf under [Components.AARCH64]')
