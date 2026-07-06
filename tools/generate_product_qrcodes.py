from pathlib import Path
import json

import qrcode
from PIL import Image, ImageDraw, ImageFont


BASE_DIR = Path(__file__).resolve().parents[1]
OUT_DIR = BASE_DIR / "qrcodes"
ORDER = [
    "cola",
    "noodle",
    "chips",
    "biscuit",
    "milk",
    "bread",
    "toothpaste",
    "water",
    "tissue",
    "soap",
]


def load_font(size):
    for font_path in [
        Path("C:/Windows/Fonts/msyh.ttc"),
        Path("C:/Windows/Fonts/simhei.ttf"),
        Path("C:/Windows/Fonts/arial.ttf"),
    ]:
        if font_path.exists():
            return ImageFont.truetype(str(font_path), size)
    return ImageFont.load_default()


def centered_text(draw, image_width, y, text, font):
    bbox = draw.textbbox((0, 0), text, font=font)
    x = (image_width - (bbox[2] - bbox[0])) // 2
    draw.text((x, y), text, fill="black", font=font)


def make_card(index, code, item, fonts):
    qr = qrcode.QRCode(
        version=2,
        error_correction=qrcode.constants.ERROR_CORRECT_H,
        box_size=12,
        border=4,
    )
    qr.add_data(item["qr"])
    qr.make(fit=True)
    qr_image = qr.make_image(fill_color="black", back_color="white").convert("RGB")

    card = Image.new("RGB", (520, 660), "white")
    draw = ImageDraw.Draw(card)
    card.paste(qr_image.resize((420, 420), Image.Resampling.NEAREST), (50, 30))

    centered_text(
        draw,
        520,
        485,
        f"{index:02d}. {item['name_zh']} / {item['name']}",
        fonts["big"],
    )
    centered_text(draw, 520, 533, item["qr"], fonts["mid"])
    centered_text(draw, 520, 581, f"Price: {item['price']:.2f} yuan", fonts["small"])
    return card


def main():
    OUT_DIR.mkdir(exist_ok=True)
    catalog = json.loads((BASE_DIR / "catalog.json").read_text(encoding="utf-8"))
    fonts = {
        "big": load_font(34),
        "mid": load_font(24),
        "small": load_font(18),
    }

    cards = []
    for index, code in enumerate(ORDER, 1):
        item = catalog[code]
        card = make_card(index, code, item, fonts)
        card.save(OUT_DIR / f"{index:02d}_{code}_{item['name_zh']}.png")
        cards.append(card)

    sheet = Image.new("RGB", (1040, 3300), "white")
    for index, card in enumerate(cards):
        sheet.paste(card, ((index % 2) * 520, (index // 2) * 660))
    sheet.save(OUT_DIR / "all_10_products_qrcode_sheet.png")

    for path in sorted(OUT_DIR.glob("*.png")):
        print(path)


if __name__ == "__main__":
    main()
