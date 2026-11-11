#!/usr/bin/env python3
"""Read-only parser for a raw ACPI DBG2 table from a target firmware dump."""
import argparse
import struct
from pathlib import Path


def u16(buf, pos):
    return struct.unpack_from('<H', buf, pos)[0]


def u32(buf, pos):
    return struct.unpack_from('<I', buf, pos)[0]


def u64(buf, pos):
    return struct.unpack_from('<Q', buf, pos)[0]


def parse(raw: bytes) -> list[dict]:
    if len(raw) < 44 or raw[:4] != b'DBG2':
        raise ValueError('not a DBG2 ACPI table')
    n = u32(raw, 4)
    if n < 44 or n > len(raw) or n > 1024 * 1024:
        raise ValueError('bad DBG2 size')
    if sum(raw[:n]) % 256:
        raise ValueError('bad DBG2 checksum')
    off, count = u32(raw, 36), u32(raw, 40)
    if count > 32 or (count and off < 44):
        raise ValueError('bad DBG2 entry count/offset')
    results = []
    for i in range(count):
        if off + 22 > n:
            raise ValueError('truncated DBG2 entry')
        slen = u16(raw, off + 1)
        regs = raw[off + 3]
        namelen, nameoff = u16(raw, off + 4), u16(raw, off + 6)
        ptype, subtype = u16(raw, off + 12), u16(raw, off + 14)
        gasoff, sizeoff = u16(raw, off + 18), u16(raw, off + 20)
        if slen < 22 or off + slen > n or regs > 8:
            raise ValueError('malformed DBG2 entry')
        if namelen and (nameoff < 22 or nameoff + namelen > slen):
            raise ValueError('bad namespace')
        name = raw[off + nameoff:off + nameoff + namelen].split(b'\0')[0].decode('ascii', 'replace') if namelen else ''
        values = []
        if regs:
            if gasoff < 22 or sizeoff < 22 or gasoff + regs * 12 > slen or sizeoff + regs * 4 > slen:
                raise ValueError('bad register offsets')
            for j in range(regs):
                base = off + gasoff + j * 12
                values.append({"address_space": raw[base], "bit_width": raw[base+1], "access_size": raw[base+3], "base": hex(u64(raw, base+4)), "size": hex(u32(raw, off + sizeoff + j*4))})
        results.append({"index": i, "port_type": hex(ptype), "port_subtype": hex(subtype), "namespace": name, "registers": values})
        off += slen
    return results


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('dbg2', type=Path, help='raw DBG2 table from ACPI dump')
    args = p.parse_args()
    for row in parse(args.dbg2.read_bytes()):
        print(row)
    print('DBG2 presence does not prove USB EEM transport support.')


if __name__ == '__main__':
    main()
