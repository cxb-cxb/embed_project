from __future__ import annotations

import argparse
import subprocess
import time
from pathlib import Path

from PIL import Image


BASE_DIR = Path(__file__).resolve().parents[1]
ADB = (
    Path.home()
    / "AppData/Local/Microsoft/WinGet/Packages/"
    / "Google.PlatformTools_Microsoft.Winget.Source_8wekyb3d8bbwe/platform-tools/adb.exe"
)

PRODUCTS = {
    "cola": "可乐",
    "noodle": "方便面",
    "chips": "薯片",
    "biscuit": "饼干",
    "milk": "牛奶",
    "bread": "面包",
    "toothpaste": "牙膏",
    "water": "矿泉水",
    "tissue": "纸巾",
    "soap": "肥皂",
}


def read_token(stream: bytes, index: int) -> tuple[bytes, int]:
    while index < len(stream) and stream[index] in b" \t\r\n":
        index += 1
    start = index
    while index < len(stream) and stream[index] not in b" \t\r\n":
        index += 1
    return stream[start:index], index


def parse_pgm_stream(stream: bytes) -> list[Image.Image]:
    images: list[Image.Image] = []
    index = 0
    while index < len(stream):
        magic, index = read_token(stream, index)
        if not magic:
            break
        if magic != b"P5":
            raise RuntimeError(f"unexpected PGM magic: {magic!r}")
        width_token, index = read_token(stream, index)
        height_token, index = read_token(stream, index)
        maxval_token, index = read_token(stream, index)
        if not width_token or not height_token or maxval_token != b"255":
            raise RuntimeError("bad PGM header")
        if index < len(stream) and stream[index] in b" \t\r\n":
            index += 1
        width = int(width_token)
        height = int(height_token)
        size = width * height
        frame = stream[index : index + size]
        if len(frame) != size:
            break
        images.append(Image.frombytes("L", (width, height), frame))
        index += size
    return images


def run_adb(args: list[str], *, capture: bool = False) -> subprocess.CompletedProcess:
    if not ADB.exists():
        raise FileNotFoundError(f"adb.exe not found: {ADB}")
    return subprocess.run(
        [str(ADB), *args],
        cwd=BASE_DIR,
        check=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
    )


def capture(product: str, frames: int, width: int, height: int, delay: int, push: bool) -> Path:
    if product not in PRODUCTS:
        raise ValueError(f"unknown product {product!r}; choose one of: {', '.join(PRODUCTS)}")

    output_dir = BASE_DIR / "dataset" / "product_images" / product
    output_dir.mkdir(parents=True, exist_ok=True)

    for second in range(delay, 0, -1):
        print(f"{PRODUCTS[product]} ({product}) 采集倒计时 {second}s ...")
        time.sleep(1)

    remote_cmd = (
        "cd /userdata/Embed_project && "
        f"./bin/camera_pgm_stream -d /dev/video5 -W {width} -H {height} -n {frames}"
    )
    result = run_adb(["exec-out", "sh", "-c", remote_cmd], capture=True)
    images = parse_pgm_stream(result.stdout)
    if not images:
        raise RuntimeError("no frames captured")

    stamp = time.strftime("%Y%m%d_%H%M%S")
    for idx, image in enumerate(images, 1):
        image.save(output_dir / f"{product}_{stamp}_{idx:03d}.png")

    if push:
        run_adb(["shell", f"mkdir -p /userdata/Embed_project/data/product_images/{product}"])
        for path in sorted(output_dir.glob(f"{product}_{stamp}_*.png")):
            run_adb(["push", str(path), f"/userdata/Embed_project/data/product_images/{product}/"])

    print(f"saved {len(images)} images -> {output_dir}")
    return output_dir


def main() -> None:
    parser = argparse.ArgumentParser(description="Capture product images from QSM368ZP-WF camera.")
    parser.add_argument("product", choices=PRODUCTS.keys())
    parser.add_argument("-n", "--frames", type=int, default=30)
    parser.add_argument("-W", "--width", type=int, default=800)
    parser.add_argument("-H", "--height", type=int, default=600)
    parser.add_argument("--delay", type=int, default=5)
    parser.add_argument("--no-push", action="store_true")
    args = parser.parse_args()
    capture(args.product, args.frames, args.width, args.height, args.delay, not args.no_push)


if __name__ == "__main__":
    main()
