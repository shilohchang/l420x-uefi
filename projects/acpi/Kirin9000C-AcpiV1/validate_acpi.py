#!/usr/bin/env python3
"""Check table checksum, format, GICC/GICD/GICR, GTDT layout/fields."""
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent / 'output'
EXPECTED = [0,0x100,0x200,0x300,0x400,0x401,0x500,0x501,0x600,0x601,0x700,0x701]


def u32(b, at): return struct.unpack_from('<I', b, at)[0]
def u64(b, at): return struct.unpack_from('<Q', b, at)[0]


def header(path, signature, revision):
    b = path.read_bytes()
    assert len(b) >= 36, 'short ACPI header'
    assert b[:4] == signature, f'{path.name}: signature mismatch'
    assert u32(b, 4) == len(b), f'{path.name}: length mismatch'
    assert b[8] == revision, f'{path.name}: revision mismatch'
    assert sum(b) % 256 == 0, f'{path.name}: checksum mismatch'
    print(f'{path.name}: signature={signature.decode()} rev={revision} length={len(b)} checksum=OK')
    return b


def madt_check():
    b = header(ROOT/'MADT.dat', b'APIC', 6)
    assert len(b) == 44 + 12 * 82 + 24 + 16, 'MADT unexpected length'
    assert u32(b, 36) == 0 and u32(b, 40) == 0  # local APIC address and flags
    off = 44
    entries = []
    while off < len(b):
        ty, ln = b[off], b[off+1]
        assert ln >= 2 and off+ln <= len(b)
        entries.append((ty, b[off:off+ln]))
        off += ln
    assert len(entries) == 14
    assert [ty for ty,_ in entries] == [11]*12 + [12,14]
    for uid, (ty, e) in enumerate(entries[:12]):
        assert len(e) == 82
        assert u32(e, 4) == uid and u32(e,8) == uid
        assert u32(e,12) & 1
        assert u32(e,56) == 25
        assert u64(e,68) == EXPECTED[uid]
        print(f'  GICC[{uid:02d}]: UID={uid} MPIDR_AFF=0x{EXPECTED[uid]:03X} OK')
    assert len(entries[12][1]) == 24 and u64(entries[12][1],8) == 0xFF000000
    assert entries[12][1][20] == 3
    assert len(entries[13][1]) == 16 and u64(entries[13][1],4) == 0xFF040000
    assert u32(entries[13][1],12) == 0x180000
    print('  GICD: 0xFF000000 v3; GICR: 0xFF040000 + 0x180000 OK')


def gtdt_check():
    b = header(ROOT/'GTDT.dat', b'GTDT', 3)
    assert len(b) == 104
    assert u64(b,36) == 0xffffffffffffffff
    assert u32(b,44) == 0
    assert [(u32(b,x),u32(b,x+4)) for x in (48,56,64,72)] == [
        (29,0),(30,0),(27,0),(26,0)
    ]
    assert u64(b,80) == 0xffffffffffffffff
    assert u32(b,88) == 0 and u32(b,92) == 0
    assert u32(b,96) == 0 and u32(b,100) == 0
    print('  GTDT timer GSIV secure=29 ns=30 virt=27 EL2=26; all flags PROVISIONAL')


if __name__ == '__main__':
    madt_check()
    gtdt_check()
    print('PASS: structural checks passed; firmware/Windows compatibility NOT established.')
