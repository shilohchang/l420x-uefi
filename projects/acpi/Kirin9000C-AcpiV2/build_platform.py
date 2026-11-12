#!/usr/bin/env python3
"""Offline-only ACPI platform prototype (not suitable for OS boot).
Creates an intentionally UNRELOCATED FADT plus a processor-only DSDT ASL.
"""
from pathlib import Path
import struct, json
BASE=Path(__file__).resolve().parent
OUT=BASE/'output'; OUT.mkdir(exist_ok=True)
AFF=(0x000,0x100,0x200,0x300,0x400,0x401,0x500,0x501,0x600,0x601,0x700,0x701)

# ACPI 6.5 FADT 276 bytes, offsets per ACPI 6.5 sec 5.2.9.
f=bytearray(276)
struct.pack_into('<4sIBB6s8sI4sI', f, 0, b'FACP', len(f), 6, 0,
                 b'KRNEXP', b'KR9000C ', 2, b'PYTH', 1)
f[45]=2 # Mobile: provisional laptop PM profile
struct.pack_into('<I',f,112,1<<20) # HW_REDUCED_ACPI
struct.pack_into('<H',f,129,1) # PSCI compliant, HVC=0 => SMC (observed Linux DT)
f[131]=5 # ACPI 6.5 minor version
# X_DSDT at offset 140 intentionally zero. Not a bootable FADT.
# ACPI FACS omitted in hardware-reduced mode. GAS, reset, sleep reg omitted.
f[9]=(-sum(f))&0xff
(OUT/'FADT-UNRELOCATED.dat').write_bytes(f)

asl=['DefinitionBlock ("", "DSDT", 2, "KRNEXP", "KR9000C ", 0x00000002)',
     '{', '    Scope (\\_SB)', '    {']
for uid,aff in enumerate(AFF):
    asl.extend([f'        Device (C{uid:03d})', '        {',
                '            Name (_HID, "ACPI0007")',
                f'            Name (_UID, {uid})',
                '        }'])
asl+=['        Device (USB0)', '        {',
      '            Name (_HID, "PNP0D10")',
      '            Name (_UID, Zero)',
      '            Name (_CCA, Zero)',
      '            Method (_STA) { Return (0x0F) }',
      '            Name (_CRS, ResourceTemplate ()',
      '            {',
      '                Memory32Fixed (ReadWrite, 0xEFC00000, 0x00100000)',
      '                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 172 }',
      '            })',
      '        }']
asl+=['    }', '}']
(BASE/'Kirin9000C-Processors.asl').write_text('\n'.join(asl)+'\n')
manifest={
 'status':'OFFLINE PROTOTYPE / NOT BOOTABLE',
 'expected_fadt_length':276,
 'fadt_x_dsdt_offset':140,
 'fadt_x_dsdt_value':0,
 'fadt_facs':'omitted (hardware-reduced prototype)',
 'fadt_flags':'HW_REDUCED_ACPI bit20 set',
 'fadt_arm_boot_arch':'PSCI compliant via SMC, per live DT',
 'uid_to_affinity':{str(i):f'0x{x:03X}' for i,x in enumerate(AFF)},
 'required_after_this':'Compile DSDT.asl with iasl; validate AML; allocate DSDT ACPI-reclaim memory in dedicated EFI loader; set FADT X_DSDT pointer; recompute checksum; validate firmware and device descriptions. DO NOT BOOT.',
 'provisional':['PSCI version/function semantics not validated in Windows environment','ACPI CPU container/topology/power management unimplemented','Sleep/reset registers and platform devices not described','FADT power profile provisional','MADT/GTDT from v1 retain provisional fields','No storage, GPU, IORT, PCIe or Windows drivers']}
(OUT/'manifest-v2.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
print('Wrote FADT-UNRELOCATED.dat (276 bytes) and Kirin9000C-Processors.asl (12 CPUs).')
print('X_DSDT intentionally ZERO. DO NOT publish FADT to firmware or boot an OS with it.')
