"""Synthetic, non-hardware tests for offline parsers."""
import importlib.util
import struct
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1] / 'scripts'


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


dtb = load('kd_dtb', 'inspect-kirin-dtb.py')
dbg = load('kd_dbg2', 'inspect-dbg2.py')


def fdt_blob():
    names = b'compatible\x00reg\x00dr_mode\x00status\x00'
    off = {name: names.index(name.encode()) for name in ['compatible', 'reg', 'dr_mode', 'status']}
    st = bytearray()
    def word(n): st.extend(struct.pack('>I', n))
    def padded(b): st.extend(b); st.extend(b'\0' * ((-len(b)) % 4))
    def begin(n): word(1); padded(n.encode() + b'\0')
    def end(): word(2)
    def prop(n, b): word(3); word(len(b)); word(off[n]); padded(b)
    begin('')
    begin('usb@ff120000')
    prop('compatible', b'snps,dwc3\x00')
    prop('dr_mode', b'otg\x00')
    prop('reg', struct.pack('>III', 0, 0xff120000, 0x10000))
    end()
    begin('serial@ff110000')
    prop('compatible', b'arm,pl011\x00')
    end()
    end()
    word(9)
    h = [0xD00DFEED, 40 + len(st) + len(names), 40, 40 + len(st), 0, 17, 16, 0, len(names), len(st)]
    return struct.pack('>10I', *h) + st + names


def dbg2_blob():
    # 1 register GAS, 4-byte size, 2-byte NUL namespace.
    ent = bytearray(44)
    struct.pack_into('<BH', ent, 0, 0, len(ent))
    ent[3] = 1
    struct.pack_into('<HHHHHHH', ent, 4, 2, 38, 0, 0, 0x8002, 0, 0)
    struct.pack_into('<HH', ent, 18, 22, 34)
    struct.pack_into('<BBBBQ', ent, 22, 0, 32, 0, 3, 0xff000000)
    struct.pack_into('<I', ent, 34, 0x1000)
    ent[38:40] = b'.\0'
    blob = bytearray(44 + len(ent))
    blob[:4] = b'DBG2'
    struct.pack_into('<I', blob, 4, len(blob))
    struct.pack_into('<II', blob, 36, 44, 1)
    blob[44:] = ent
    blob[9] = (-sum(blob)) & 0xff
    return bytes(blob)


class Parsers(unittest.TestCase):
    def test_dtb_usb_uart(self):
        result = dtb.inspect(dtb.parse_dtb(fdt_blob()))
        self.assertEqual(len(result['usb_candidates']), 1)
        self.assertEqual(result['usb_candidates'][0]['dr_mode'], 'otg')
        self.assertEqual(len(result['uart_candidates']), 1)
        self.assertIn('0xff120000', result['usb_candidates'][0]['reg_untranslated'][0])

    def test_dtb_invalid(self):
        with self.assertRaises(ValueError):
            dtb.parse_dtb(b'nonsense')

    def test_dbg2_valid(self):
        results = dbg.parse(dbg2_blob())
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0]['port_type'], '0x8002')
        self.assertEqual(results[0]['registers'][0]['base'], '0xff000000')

    def test_dbg2_bad_checksum(self):
        data = bytearray(dbg2_blob())
        data[10] ^= 1
        with self.assertRaisesRegex(ValueError, 'checksum'):
            dbg.parse(bytes(data))


if __name__ == '__main__':
    unittest.main()
