#!/usr/bin/env python3
"""Read-only DTB scanner to identify potential USB3 dual-role and UART nodes.

Works on exported device-tree blobs from the *L420x itself*, not WSL's host.
Does not assert KDNET compatibility; DT addresses might require bus translation.
"""
import argparse
import json
import re
import struct
from pathlib import Path

FDT_MAGIC = 0xD00DFEED
BEGIN_NODE, END_NODE, PROP, NOP, END = 1, 2, 3, 4, 9


def u32(buf: bytes, off: int) -> int:
    if off < 0 or off + 4 > len(buf):
        raise ValueError(f"truncated FDT at {off}")
    return struct.unpack_from(">I", buf, off)[0]


def align4(n: int) -> int:
    return (n + 3) & ~3


def string_value(data: bytes) -> str:
    return data.split(b"\x00", 1)[0].decode("utf-8", "replace")


def string_list(data: bytes) -> list[str]:
    return [x.decode("utf-8", "replace") for x in data.split(b"\x00") if x]


def parse_dtb(raw: bytes) -> list[dict]:
    if len(raw) < 40 or u32(raw, 0) != FDT_MAGIC:
        raise ValueError("not an FDT blob")
    total = u32(raw, 4)
    struct_off = u32(raw, 8)
    strings_off = u32(raw, 12)
    strings_size = u32(raw, 32)
    struct_size = u32(raw, 36)
    if not (40 <= total <= len(raw)):
        raise ValueError("invalid FDT total length")
    if struct_off + struct_size > total or strings_off + strings_size > total:
        raise ValueError("FDT block ranges out of bounds")
    data = memoryview(raw)
    strings = data[strings_off:strings_off + strings_size]
    cursor, last = struct_off, struct_off + struct_size
    tree: list[dict] = []
    parents: list[dict] = []
    ended = False
    while cursor < last:
        token = u32(raw, cursor)
        cursor += 4
        if token == BEGIN_NODE:
            end = raw.find(b"\x00", cursor, last)
            if end < 0:
                raise ValueError("unterminated FDT node name")
            name = raw[cursor:end].decode("utf-8", "replace")
            cursor = align4(end + 1)
            path = (parents[-1]["path"].rstrip("/") if parents else "") + "/" + name
            if path == "//":
                path = "/"
            node = {"path": path, "properties": {}, "parent": parents[-1] if parents else None}
            tree.append(node)
            parents.append(node)
        elif token == END_NODE:
            if not parents:
                raise ValueError("unbalanced FDT END_NODE")
            parents.pop()
        elif token == PROP:
            if not parents or cursor + 8 > last:
                raise ValueError("bad FDT property")
            sz = u32(raw, cursor)
            nameoff = u32(raw, cursor + 4)
            cursor += 8
            if cursor + sz > last or nameoff >= len(strings):
                raise ValueError("out-of-range FDT property")
            name_end = bytes(strings).find(b"\x00", nameoff)
            if name_end < 0:
                raise ValueError("unterminated FDT property name")
            key = bytes(strings[nameoff:name_end]).decode("ascii", "replace")
            parents[-1]["properties"][key] = bytes(data[cursor:cursor + sz])
            cursor = align4(cursor + sz)
        elif token == NOP:
            continue
        elif token == END:
            ended = True
            break
        else:
            raise ValueError(f"unknown FDT token {token}")
    if not ended or parents:
        raise ValueError("incomplete FDT tree")
    return tree


def as_cells(blob: bytes) -> list[int]:
    if len(blob) % 4:
        return []
    return list(struct.unpack(">" + "I" * (len(blob) // 4), blob))


def reg_description(node: dict) -> list[str]:
    regs = as_cells(node["properties"].get("reg", b""))
    parent = node["parent"]
    if not parent or not regs:
        return []
    p = parent["properties"]
    addr_cells = as_cells(p.get("#address-cells", b"\x00\x00\x00\x02"))
    size_cells = as_cells(p.get("#size-cells", b"\x00\x00\x00\x01"))
    ac = addr_cells[0] if addr_cells else 2
    sc = size_cells[0] if size_cells else 1
    if ac < 1 or ac > 4 or sc > 4 or ac + sc == 0:
        return ["reg=" + " ".join(hex(x) for x in regs) + " (unparsed)"]
    lines = []
    step = ac + sc
    for idx in range(0, len(regs) - step + 1, step):
        addr = 0
        size = 0
        for x in regs[idx:idx + ac]:
            addr = (addr << 32) | x
        for x in regs[idx + ac:idx + step]:
            size = (size << 32) | x
        lines.append(f"reg_raw=0x{addr:x} size=0x{size:x}")
    return lines


def inspect(nodes: list[dict]) -> dict:
    result = {"usb_candidates": [], "uart_candidates": [], "warning": (
        "DT hints only: verify controller revision, actual device-mode USB-C connection, "
        "ACPI DBG2 and Windows KD transport; no KDNET compatibility inferred.")}
    for n in nodes:
        p = n["properties"]
        comp = string_list(p.get("compatible", b""))
        hay = (n["path"] + " " + " ".join(comp)).lower()
        usb = bool(re.search(r"dwc3|synopsys|snps,|usb3|usb@|xhci|dwc2", hay)) and (
            "usb" in hay or "dwc" in hay or "xhci" in hay)
        uart = bool(re.search(r"serial@|uart@|pl011|ns16550|sbsa|hisi,uart|dw-apb-uart", hay))
        if not (usb or uart):
            continue
        status = string_value(p.get("status", b"okay\x00"))
        entry = {
            "path": n["path"],
            "compatible": comp,
            "status": status,
            "dr_mode": string_value(p.get("dr_mode", b"")),
            "maximum_speed": string_value(p.get("maximum-speed", b"")),
            "usb_role_switch": "usb-role-switch" in p,
            "reg_untranslated": reg_description(n),
        }
        if usb:
            result["usb_candidates"].append(entry)
        if uart:
            result["uart_candidates"].append(entry)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dtb", type=Path, help="DTB exported from L420x Linux (or a DTB file)")
    parser.add_argument("--json", action="store_true", help="print machine-readable findings")
    args = parser.parse_args()
    raw = args.dtb.read_bytes()
    if len(raw) > 16 * 1024 * 1024:
        parser.error("DTB >16MiB rejected")
    result = inspect(parse_dtb(raw))
    if args.json:
        print(json.dumps(result, indent=2, ensure_ascii=False))
        return
    print("L420x V7 KDNET candidate USB/UART inventory (READ ONLY)")
    for typ in ("usb_candidates", "uart_candidates"):
        print(f"\n[{typ}] {len(result[typ])}")
        for dev in result[typ]:
            print(f"  {dev['path']} [{dev['status']}]")
            print(f"    compatible: {', '.join(dev['compatible']) or '?'}")
            print(f"    dr_mode: {dev['dr_mode'] or '?'}  maximum-speed: {dev['maximum_speed'] or '?'}  role-switch: {dev['usb_role_switch']}")
            print(f"    reg (UNTRANSLATED): {', '.join(dev['reg_untranslated']) or '?'}")
    print("\nLIMITATION:", result["warning"])


if __name__ == "__main__":
    main()
