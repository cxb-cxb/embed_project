from pathlib import Path

import qrcode
from PIL import Image, ImageDraw, ImageFont


QR_PAYLOADS = [
    ("product_cola", "product:cola"),
    ("product_milk", "product:milk"),
    ("product_bread", "product:bread"),
    ("product_water", "product:mineral_water"),
    ("checkout", "checkout"),
    ("clear", "clear"),
]


def load_font(size: int):
    for name in ("arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def make_qr(payload: str, output: Path) -> None:
    qr = qrcode.QRCode(
        version=2,
        error_correction=qrcode.constants.ERROR_CORRECT_H,
        box_size=30,
        border=6,
    )
    qr.add_data(payload)
    qr.make(fit=True)
    image = qr.make_image(fill_color="black", back_color="white").convert("RGB")

    width, height = image.size
    canvas = Image.new("RGB", (width, height + 140), "white")
    canvas.paste(image, (0, 0))

    draw = ImageDraw.Draw(canvas)
    font = load_font(56)
    draw.text((36, height + 34), payload, fill="black", font=font)
    canvas.save(output)


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    output_dir = root / "outputs" / "demo_qr"
    output_dir.mkdir(parents=True, exist_ok=True)

    for name, payload in QR_PAYLOADS:
        output = output_dir / f"qr_{name}.png"
        make_qr(payload, output)
        print(f"{payload} -> {output}")


if __name__ == "__main__":
    main()
