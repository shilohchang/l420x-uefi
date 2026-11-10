#!/usr/bin/env python3
"""Checks limited structural properties; never claims OS compatibility."""
from pathlib import Path
import struct,re
p=Path(__file__).resolve().parent
f=(p/'output/FADT-UNRELOCATED.dat').read_bytes()
assert len(f)==276 and f[:4]==b'FACP'
assert struct.unpack_from('<I',f,4)[0]==276
assert f[8]==6 and f[131]==5 and sum(f)%256==0
assert struct.unpack_from('<I',f,112)[0] & (1<<20)
assert struct.unpack_from('<H',f,129)[0] == 1
assert struct.unpack_from('<Q',f,140)[0]==0
asl=(p/'Kirin9000C-Processors.asl').read_text()
ids=[int(x) for x in re.findall(r'Name \(_UID, (\d+)\)',asl)]
assert ids==list(range(12)),ids
assert asl.count('"ACPI0007"')==12
assert len(set(re.findall(r'Device \((C\d{3})\)',asl)))==12
print('PASS: FADT header/length/checksum/PSCI-SMC/HW_REDUCED flags and zero X_DSDT placeholder')
print('PASS: DSDT ASL contains 12 unique ACPI0007 CPUs with UIDs 0..11 matching MADT v1')
print('NOTE: ASL has NOT been compiled; run iasl -tc Kirin9000C-Processors.asl in WSL.')
print('NOT BOOT-READY: X_DSDT is zero, no platform peripherals, incomplete ACPI bundle.')
