#!/usr/bin/env python3
from pathlib import Path
import re, shutil
r = Path.cwd()
s = Path(__file__).resolve().parent / 'AcpiPublishTest'
d = r / 'AcpiPublishTest'
base = r / 'MdeModulePkg/MdeModulePkg.dsc'
out = r / 'MdeModulePkg/KirinPublishTest.dsc'
if not base.is_file():
    raise SystemExit('Run from edk2 root')
d.mkdir(exist_ok=True)
for n in ('AcpiPublishTest.c','AcpiPublishTest.inf'):
    shutil.copy2(s/n, d/n)
lines=base.read_text(encoding='utf-8-sig').splitlines()
header=re.compile(r'^\s*\[Components(?:\.[^\]]+)?\]\s*(?:#.*)?$',re.I)
indices=[i for i,line in enumerate(lines) if header.match(line)]
if not indices: raise SystemExit('Cannot locate [Components] section')
lines.insert(indices[0]+1, '  AcpiPublishTest/AcpiPublishTest.inf')
out.write_text('\n'.join(lines)+'\n')
print('Created',out)
print('Added AcpiPublishTest/AcpiPublishTest.inf')
