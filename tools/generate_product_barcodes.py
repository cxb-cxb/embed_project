from __future__ import annotations

import csv
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
PRODUCTS_CSV = ROOT / "data" / "products.csv"
OUT_DIR = ROOT / "barcodes"


LEFT_ODD = {
    "0": "0001101",
    "1": "0011001",
    "2": "0010011",
    "3": "0111101",
    "4": "0100011",
    "5": "0110001",
    "6": "0101111",
    "7": "0111011",
    "8": "0110111",
    "9": "0001011",
}
LEFT_EVEN = {
    "0": "0100111",
    "1": "0110011",
    "2": "0011011",
    "3": "0100001",
    "4": "0011101",
    "5": "0111001",
    "6": "0000101",
    "7": "0010001",
    "8": "0001001",
    "9": "0010111",
}
RIGHT = {
    "0": "1110010",
    "1": "1100110",
    "2": "1101100",
    "3": "1000010",
    "4": "1011100",
    "5": "1001110",
    "6": "1010000",
    "7": "1000100",
    "8": "1001000",
    "9": "1110100",
}
PARITY = {
    "0": "OOOOOO",
    "1": "OOEOEE",
    "2": "OOEEOE",
    "3": "OOEEEO",
    "4": "OEOOEE",
    "5": "OEEOOE",
    "6": "OEEEOO",
    "7": "OEOEOE",
    "8": "OEOEEO",
    "9": "OEEOEO",
}


def ean13_checksum(first_12: str) -> str:
    total = 0
    for i, ch in enumerate(first_12):
        d = int(ch)
        total += d * 3 if i % 2 else d
    return str((10 - total % 10) % 10)


def ean13_bits(code: str) -> str:
    if len(code) == 12:
        code += ean13_checksum(code)
    if len(code) != 13 or not code.isdigit():
        raise ValueError(f"EAN-13 needs 12 or 13 digits: {code!r}")

    bits = ["101"]
    parity = PARITY[code[0]]
    for ch, mode in zip(code[1:7], parity):
        bits.append(LEFT_ODD[ch] if mode == "O" else LEFT_EVEN[ch])
    bits.append("01010")
    for ch in code[7:]:
        bits.append(RIGHT[ch])
    bits.append("101")
    return "".join(bits)


def safe_font(size: int):
    for name in ("arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def draw_card(product: dict[str, str]) -> Path:
    base_code = product["barcode"].strip()
    ean = base_code + ean13_checksum(base_code)
    bits = ean13_bits(ean)

    module = 4
    quiet = 14
    bar_h = 260
    text_h = 110
    w = (len(bits) + quiet * 2) * module
    h = bar_h + text_h

    img = Image.new("RGB", (w, h), "white")
    draw = ImageDraw.Draw(img)
    x = quiet * module
    for bit in bits:
        if bit == "1":
            draw.rectangle([x, 20, x + module - 1, bar_h], fill="black")
        x += module

    title_font = safe_font(28)
    code_font = safe_font(24)
    title = f"{product['display_name']} / {product['id']}"
    draw.text((24, bar_h + 16), title, fill="black", font=title_font)
    draw.text((24, bar_h + 58), f"EAN-13: {ean}    catalog: {base_code}", fill="black", font=code_font)

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    path = OUT_DIR / f"{product['id']}_{ean}.png"
    img.save(path)
    return path


def main() -> None:
    with PRODUCTS_CSV.open("r", encoding="utf-8", newline="") as f:
        products = list(csv.DictReader(f))

    paths = [draw_card(product) for product in products if product.get("barcode")]
    index = OUT_DIR / "README.txt"
    index.write_text(
        "Show or print these EAN-13 barcode cards for camera tests.\n"
        "The scanner accepts the 13-digit EAN code and maps it to the 12-digit catalog barcode.\n\n"
        + "\n".join(str(path.name) for path in paths)
        + "\n",
        encoding="utf-8",
    )
    print(f"Generated {len(paths)} barcode cards in {OUT_DIR}")


if __name__ == "__main__":
    main()
