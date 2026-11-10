#!/usr/bin/env python3
from pathlib import Path
import shutil
root = Path(__file__).resolve().parent
edk = Path.cwd()
base = edk/'MdeModulePkg/MdeModulePkg.dsc'
if not base.is_file():
    raise SystemExit('Run from edk2 root')
dst = edk/'GicProbeV2'
dst.mkdir(exist_ok=True)
for fn in ('GicProbeV2.c','GicProbeV2.inf'):
    shutil.copy2(root/'GicProbeV2'/fn, dst/fn)
p = edk/'MdeModulePkg/KirinGicProbeV2.dsc'
txt = base.read_text() + '\n[Components.AARCH64]\n  GicProbeV2/GicProbeV2.inf\n'
p.write_text(txt)
print('Source:',dst)
print('DSC:',p)
