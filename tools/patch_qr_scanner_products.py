from pathlib import Path
import struct


BASE_DIR = Path(__file__).resolve().parents[1]
SRC_BIN = BASE_DIR / "board_backup" / "qr_scanner_display.original"
OUT_BIN = BASE_DIR / "patched" / "qr_scanner_display"


REPLACEMENTS = [
    ("mineral_water", "toothpaste"),
    ("Mineral Water", "Toothpaste"),
    ("instant_noodles", "noodle"),
    ("Instant Noodles", "Noodle"),
    ("cookies", "biscuit"),
    ("Cookies", "Biscuit"),
    ("coffee", "water"),
    ("Coffee", "Water"),
    ("Tea Drink", "Soap"),
    ("yogurt", "tissue"),
    ("Yogurt", "Tissue"),
]

DESIRED_PRICES = {
    "toothpaste": 800,
    "cola": 350,
    "milk": 450,
    "bread": 500,
    "noodle": 400,
    "chips": 600,
    "water": 200,
    "soap": 300,
    "biscuit": 550,
    "tissue": 400,
}


def parse_elf_load_segments(data):
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise RuntimeError("expected little-endian ELF64")
    e_phoff = struct.unpack_from("<Q", data, 32)[0]
    e_phentsize = struct.unpack_from("<H", data, 54)[0]
    e_phnum = struct.unpack_from("<H", data, 56)[0]
    segments = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_flags = struct.unpack_from("<II", data, off)
        if p_type != 1:
            continue
        p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz, _align = struct.unpack_from("<QQQQQQ", data, off + 8)
        segments.append((p_offset, p_vaddr, p_filesz, p_memsz, p_flags))
    return segments


def file_offset_to_vaddr(offset, segments):
    for p_offset, p_vaddr, p_filesz, _p_memsz, _flags in segments:
        if p_offset <= offset < p_offset + p_filesz:
            return p_vaddr + (offset - p_offset)
    raise RuntimeError(f"offset 0x{offset:x} is not in a LOAD segment")


def vaddr_to_file_offset(vaddr, segments):
    for p_offset, p_vaddr, p_filesz, p_memsz, _flags in segments:
        if p_vaddr <= vaddr < p_vaddr + p_memsz:
            delta = vaddr - p_vaddr
            if delta >= p_filesz:
                raise RuntimeError(f"vaddr 0x{vaddr:x} is in bss, not file-backed")
            return p_offset + delta
    raise RuntimeError(f"vaddr 0x{vaddr:x} is not in a LOAD segment")


def patch_c_string(buf, old, new):
    old_b = old.encode("ascii") + b"\x00"
    new_b = new.encode("ascii") + b"\x00"
    if len(new_b) > len(old_b):
        raise RuntimeError(f"replacement too long: {old!r} -> {new!r}")
    idx = buf.find(old_b)
    if idx < 0:
        raise RuntimeError(f"string not found: {old!r}")
    buf[idx:idx + len(old_b)] = new_b + (b"\x00" * (len(old_b) - len(new_b)))
    return idx


def find_product_structs(data, segments, id_vaddrs):
    # struct retail_product = const char* id, const char* name, const char* barcode, int price, padding
    structs = {}
    for original_id, vaddr in id_vaddrs.items():
        needle = struct.pack("<Q", vaddr)
        start = 0
        while True:
            idx = data.find(needle, start)
            if idx < 0:
                break
            try:
                # Validate that the struct's name and barcode pointers are sane file-backed pointers.
                name_ptr = struct.unpack_from("<Q", data, idx + 8)[0]
                barcode_ptr = struct.unpack_from("<Q", data, idx + 16)[0]
                vaddr_to_file_offset(name_ptr, segments)
                barcode_off = vaddr_to_file_offset(barcode_ptr, segments)
                barcode = data[barcode_off:barcode_off + 32].split(b"\x00", 1)[0]
                if barcode.isdigit():
                    structs[original_id] = idx
                    break
            except Exception:
                pass
            start = idx + 1
    return structs


def main():
    data = bytearray(SRC_BIN.read_bytes())
    segments = parse_elf_load_segments(data)

    original_id_offsets = {}
    for old, new in REPLACEMENTS:
        off = patch_c_string(data, old, new)
        if old in ["mineral_water", "instant_noodles", "cookies", "coffee", "tea", "yogurt"]:
            original_id_offsets[old] = off

    # Unchanged IDs still need price updates.
    for original_id in ["cola", "milk", "bread", "chips", "tea"]:
        needle = original_id.encode("ascii") + b"\x00"
        off = data.find(needle)
        if off < 0:
            raise RuntimeError(f"string not found: {original_id}")
        original_id_offsets[original_id] = off

    id_vaddrs = {key: file_offset_to_vaddr(off, segments) for key, off in original_id_offsets.items()}
    structs = find_product_structs(data, segments, id_vaddrs)
    if len(structs) != len(original_id_offsets):
        missing = sorted(set(original_id_offsets) - set(structs))
        raise RuntimeError(f"product structs not found for: {missing}")

    id_after_patch = {
        "mineral_water": "toothpaste",
        "cola": "cola",
        "milk": "milk",
        "bread": "bread",
        "instant_noodles": "noodle",
        "chips": "chips",
        "coffee": "water",
        "tea": "soap",
        "cookies": "biscuit",
        "yogurt": "tissue",
    }
    soap_name_off = data.find(b"Soap\x00")
    if soap_name_off < 0:
        raise RuntimeError("patched Soap string not found")
    soap_name_vaddr = file_offset_to_vaddr(soap_name_off, segments)
    for original_id, struct_off in structs.items():
        new_id = id_after_patch[original_id]
        price = DESIRED_PRICES[new_id]
        if original_id == "tea":
            struct.pack_into("<Q", data, struct_off, soap_name_vaddr)
        struct.pack_into("<i", data, struct_off + 24, price)

    OUT_BIN.write_bytes(data)
    print(f"patched {OUT_BIN}")
    for original_id, struct_off in sorted(structs.items(), key=lambda kv: kv[1]):
        new_id = id_after_patch[original_id]
        print(f"{new_id}: price_cents={DESIRED_PRICES[new_id]} struct=0x{struct_off:x}")


if __name__ == "__main__":
    main()



