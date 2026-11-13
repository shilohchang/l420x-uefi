#!/usr/bin/env python3
# Enable exactly one vendor diagnostic HID in a generated SSDT copy.
import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True, type=Path)
parser.add_argument('--dest', required=True, type=Path)
parser.add_argument('--device', required=True, help='Exact LTXxxxx diagnostic _HID')
args = parser.parse_args()
if not re.fullmatch(r'LTX[0-9A-F]{4}', args.device):
    parser.error('Expected LTXxxxx diagnostic ID, e.g. LTX0001')
source = args.source.read_text(encoding='utf-8')
if 'L42DBG' not in source or 'DTSCAN' not in source:
    parser.error('Not an L420X generated discovery table')
# Match one contiguous Device object with its unique vendor diagnostic HID.
pattern = (r'(Device \(D[0-9A-F]{3}\)\s*\{\s*'
           r'Name \(_HID, "' + re.escape(args.device) + r'"\)'
           r'.*?Method \(_STA, 0, NotSerialized\) \{ Return \(Zero\) \})')
updated, count = re.subn(pattern,
    lambda m: m.group(0).replace('Return (Zero)', 'Return (0x0F)'),
    source, flags=re.S)
if count != 1:
    parser.error(f'Expected exactly one disabled device {args.device}, found {count}')
if args.dest.resolve() == args.source.resolve():
    parser.error('Destination must differ from source')
args.dest.parent.mkdir(parents=True, exist_ok=True)
args.dest.write_text(updated, encoding='utf-8')
print(f'Enabled {args.device} only: {args.dest}')
