#!/usr/bin/env python3
import argparse
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageEnhance, ImageFilter, ImageOps

RESAMPLE_BILINEAR = getattr(getattr(Image, "Resampling", Image), "BILINEAR")


def stretch_like_board(img):
    hist = img.histogram()
    total = img.width * img.height
    low_target = total // 50
    high_target = total - low_target
    cumulative = 0
    lo = 0
    hi = 255
    for i, count in enumerate(hist):
        cumulative += count
        if cumulative >= low_target:
            lo = i
            break
    cumulative = 0
    for i, count in enumerate(hist):
        cumulative += count
        if cumulative >= high_target:
            hi = i
            break
    if hi - lo < 32:
        lo = max(0, lo - 24)
        hi = min(255, hi + 24)
    if hi <= lo:
        return img.copy()
    lut = []
    for i in range(256):
        v = (i - lo) * 255 // (hi - lo)
        v = max(0, min(255, v))
        if v < 42:
            v = 0
        elif v > 213:
            v = 255
        lut.append(v)
    return img.point(lut)


def center_crops(img):
    w, h = img.size
    crops = [("full", img)]
    for pct in (88, 76, 64, 52):
        cw = w * pct // 100
        ch = h * pct // 100
        left = (w - cw) // 2
        top = (h - ch) // 2
        crops.append((f"center{pct}", img.crop((left, top, left + cw, top + ch))))
    return crops


def variants(path):
    base = Image.open(path).convert("L")
    for crop_name, cropped in center_crops(base):
        for rot in (0, 90, 180, 270):
            img = cropped.rotate(rot, expand=True) if rot else cropped
            scaled = img
            if max(scaled.size) > 1200:
                scale = 1200 / max(scaled.size)
                scaled = scaled.resize(
                    (max(1, int(scaled.width * scale)), max(1, int(scaled.height * scale))),
                    RESAMPLE_BILINEAR,
                )
            attempts = [
                ("gray", scaled),
                ("board_stretch", stretch_like_board(scaled)),
                ("autocontrast", ImageOps.autocontrast(scaled, cutoff=1)),
                ("sharp", ImageEnhance.Contrast(scaled.filter(ImageFilter.SHARPEN)).enhance(1.8)),
            ]
            for name, candidate in attempts:
                yield f"{crop_name}:rot{rot}:{name}", candidate


def decode(decoder, img):
    with tempfile.NamedTemporaryFile(suffix=".pgm", delete=False) as tmp:
        tmp_path = Path(tmp.name)
    try:
        img.save(tmp_path)
        result = subprocess.run(
            [str(decoder), str(tmp_path)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        payloads = [line.strip() for line in result.stdout.splitlines() if line.strip()]
        return payloads
    finally:
        tmp_path.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--decoder", required=True, type=Path)
    parser.add_argument("images", nargs="+", type=Path)
    args = parser.parse_args()

    ok = 0
    for path in args.images:
        hit = None
        for variant_name, img in variants(path):
            payloads = decode(args.decoder, img)
            if payloads:
                hit = (variant_name, payloads)
                break
        if hit:
            ok += 1
            print(f"OK {path.name} {hit[0]} {' | '.join(hit[1])}")
        else:
            print(f"MISS {path.name}")
    print(f"SUMMARY {ok}/{len(args.images)} decoded")
    return 0 if ok == len(args.images) else 1


if __name__ == "__main__":
    raise SystemExit(main())
