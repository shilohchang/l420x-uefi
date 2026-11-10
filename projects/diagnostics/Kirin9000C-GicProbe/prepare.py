#!/usr/bin/env python3
from pathlib import Path
import shutil
root = Path(__file__).resolve().parent
edk = Path.cwd()
base = edk/'MdeModulePkg/MdeModulePkg.dsc'
if not base.is_file():
    raise SystemExit('Run from EDK II workspace root (~/uefi-work/edk2)')
dst = edk/'GicProbe'
dst.mkdir(exist_ok=True)
for item in ('GicProbe.c','GicProbe.inf'):
    shutil.copy2(root/'GicProbe'/item, dst/item)
p = edk/'MdeModulePkg/KirinGicProbe.dsc'
txt = base.read_text()
txt += '\n[Components.AARCH64]\n  GicProbe/GicProbe.inf\n'
p.write_text(txt)
print('Source:',dst)
print('DSC:',p)
print('Registered GicProbe under [Components.AARCH64]')
