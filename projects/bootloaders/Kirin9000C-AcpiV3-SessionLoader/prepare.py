#!/usr/bin/env python3
"""Generate embedded ACPI blobs from user's V1/V2 files and register an EDK2 module.
This script writes files in the local edk2 workspace only. No target firmware changes.
Run from edk2 root.
"""
from pathlib import Path
import re, shutil, struct
root=Path.cwd()
assert (root/'MdeModulePkg'/'MdeModulePkg.dsc').is_file(), 'Run from edk2 root'
source=Path(__file__).resolve().parent
base=source.parent
v1=base/'Kirin9000C-AcpiV1'/'output'
v2=base/'Kirin9000C-AcpiV2'/'output'
files=[('kDsdt',v2/'DSDT.aml',b'DSDT'),('kFadt',v2/'FADT-UNRELOCATED.dat',b'FACP'),('kMadt',v1/'MADT.dat',b'APIC'),('kGtdt',v1/'GTDT.dat',b'GTDT')]
parts=['/* Auto generated: do not edit. Experimental non-boot ACPI prototypes. */','#pragma once','#include <Uefi.h>']
for name,path,sig in files:
    if not path.exists():
        raise SystemExit(f'Missing: {path}\nPut DSDT.aml into Kirin9000C-AcpiV2/output/')
    b=path.read_bytes()
    if len(b)<36 or b[:4]!=sig or struct.unpack_from('<I',b,4)[0]!=len(b) or sum(b)%256:
        raise SystemExit(f'Invalid ACPI table: {path}')
    parts.append(f'STATIC CONST UINT8 {name}[{len(b)}] = {{')
    parts.extend('  '+', '.join(f'0x{x:02X}' for x in b[i:i+16])+',' for i in range(0,len(b),16))
    parts.append('};')
    print(f'Validated {path}: {len(b)} bytes')

dest=root/'SessionLoader'
dest.mkdir(exist_ok=True)
for filename in ('SessionLoader.c','SessionLoader.inf'):
    shutil.copy2(source/'SessionLoader'/filename,dest/filename)
(dest/'EmbeddedTables.h').write_text('\n'.join(parts)+'\n')
original=(root/'MdeModulePkg'/'MdeModulePkg.dsc').read_text()
# Append an architecture-specific component section; DSC supports repeated sections.
original+='\n[Components.AARCH64]\n  SessionLoader/SessionLoader.inf\n'
out=root/'MdeModulePkg'/'KirinSessionLoader.dsc'
out.write_text(original)
print('Created:', dest/'EmbeddedTables.h')
print('Created:', out)
