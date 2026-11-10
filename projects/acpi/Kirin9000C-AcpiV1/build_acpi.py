#!/usr/bin/env python3
"""Kirin9000C ACPI 6.5 MADT/GTDT *offline experimental* builder.
NO firmware/EFI writes. Output is NOT BOOT-READY.
Uses byte offsets from EDK II MdePkg/Include/IndustryStandard/Acpi65.h.
"""
import json
import struct
from pathlib import Path

OUT = Path(__file__).resolve().parent / 'output'
OUT.mkdir(exist_ok=True)
AFFINITIES = (0x000, 0x100, 0x200, 0x300, 0x400, 0x401,
              0x500, 0x501, 0x600, 0x601, 0x700, 0x701)
GICD = 0xFF000000
GICR = 0xFF040000
GICR_LENGTH = 0x180000
STRIDE = 0x20000
FREQUENCY_HZ = 1920000  # independently verified by CNTFRQ_EL0, not encoded in GTDT


def acpi_table(sig, revision, payload):
    assert len(sig) == 4
    header = bytearray(36)
    struct.pack_into('<4sIBB6s8sI4sI', header, 0,
                     sig, 36 + len(payload), revision, 0,
                     b'KRNEXP', b'KR9000C ', 1, b'PYTH', 1)
    table = header + payload
    table[9] = (-sum(table)) & 0xff
    assert sum(table) % 256 == 0
    return bytes(table)


def gicc(uid, mpidr):
    # ACPI 6.5 GICC: Type 0x0B, Length 82, see Acpi65.h.
    b = bytearray(82)
    struct.pack_into('<BBH', b, 0, 0x0B, len(b), 0)
    struct.pack_into('<I', b, 4, uid)    # CPUInterfaceNumber: experimental index
    struct.pack_into('<I', b, 8, uid)    # ACPI Processor UID
    struct.pack_into('<I', b, 12, 1)     # Enabled
    # Parking Protocol 0, Performance IRQ 0 (unknown, not advertised)
    # GICV/GICH/GICC PhysicalBase 0 (GICv3 system register interface)
    struct.pack_into('<I', b, 56, 25)   # VGIC maintenance GSIV observed in Linux; provisional
    # GICR base field 0, shared GICR region record used below
    struct.pack_into('<Q', b, 68, mpidr)
    # power efficiency class / SPE / TRBE left at 0 = unadvertised
    return bytes(b)


def gicd():
    b = bytearray(24)
    struct.pack_into('<BBHIQI', b, 0, 0x0C, 24, 0, 0, GICD, 0)
    b[20] = 3   # GICv3
    return bytes(b)


def gicr():
    return struct.pack('<BBHQI', 0x0E, 16, 0, GICR, GICR_LENGTH)


def build_madt():
    # MADT header has 4-byte Local APIC Address and 4-byte flags (0 for ARM).
    payload = struct.pack('<II', 0, 0)
    for uid, affinity in enumerate(AFFINITIES):
        payload += gicc(uid, affinity)
    payload += gicd() + gicr()
    return acpi_table(b'APIC', 6, payload)


def build_gtdt():
    # ACPI 6.5 GTDT revision 3, length 104. Timer flags 0 = level-triggered,
    # active-high; interrupt attributes are EXPERIMENTAL pending firmware validation.
    # CntControlBase/CntReadBase omitted => 0xFFFFFFFFFFFFFFFF.
    p = bytearray(68)
    struct.pack_into('<Q', p, 0, 0xffffffffffffffff)
    # Reserved at body offset 8 = 0.
    for body_offset, irq in ((12, 29), (20, 30), (28, 27), (36, 26)):
        struct.pack_into('<II', p, body_offset, irq, 0)
    struct.pack_into('<Q', p, 44, 0xffffffffffffffff)  # CntReadBase
    struct.pack_into('<I', p, 52, 0)  # PlatformTimerCount
    struct.pack_into('<I', p, 56, 0)  # PlatformTimerOffset
    struct.pack_into('<I', p, 60, 0)  # Virtual EL2 timer GSIV (not verified)
    struct.pack_into('<I', p, 64, 0)  # Virtual EL2 timer flags
    return acpi_table(b'GTDT', 3, p)


def main():
    madt, gtdt = build_madt(), build_gtdt()
    (OUT / 'MADT.dat').write_bytes(madt)
    (OUT / 'GTDT.dat').write_bytes(gtdt)
    manifest = {
        'warning': 'OFFLINE EXPERIMENTAL ONLY: NOT BOOT-READY. NO FADT/DSDT.',
        'acpi_spec': '6.5',
        'cpu_affinities': [f'0x{x:03X}' for x in AFFINITIES],
        'gicd_base': f'0x{GICD:08X}',
        'gicr_base': f'0x{GICR:08X}',
        'gicr_size': f'0x{GICR_LENGTH:X}',
        'gicr_stride': f'0x{STRIDE:X}',
        'gicr_probe_result': '12 affinities matched, LAST=1 on final, PLPIS=0',
        'generic_timer_frequency_hz': FREQUENCY_HZ,
        'gtdt_gsivs': {'secure_el1': 29, 'nonsecure_el1': 30,
                       'virtual_el1': 27, 'physical_el2': 26,
                       'virtual_el2': '0 (unverified/omitted)'},
        'provisional_fields': [
            'GICC CPUInterfaceNumber uses uid index',
            'GICC maintenance IRQ=25 inferred from Linux vgic IRQ',
            'GTDT timer flags=0 level/high, requires runtime/firmware confirmation',
            'Virtual EL2 timer entry omitted',
            'All GICC performance IRQ / GICV / GICH / GICR per-CPU base omitted',
            'No GIC ITS entries; absence in DT is not proof of nonexistence',
            'No FADT/DSDT/IORT/MCFG/SPCR; tables are not a Windows boot package'
        ],
        'files': {'MADT.dat': len(madt), 'GTDT.dat': len(gtdt)}
    }
    (OUT / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    print('Built MADT.dat:', len(madt), 'bytes; GTDT.dat:', len(gtdt), 'bytes')
    print('WARNING: OFFLINE TEST DATA. DO NOT LOAD FOR OS BOOT.')


if __name__ == '__main__':
    main()
