from __future__ import annotations

import argparse
import subprocess
import time
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageOps

try:
    import cv2
except ImportError:  # pragma: no cover - fallback for minimal Python envs
    cv2 = None


BASE_DIR = Path(__file__).resolve().parents[1]
DATASET_DIR = BASE_DIR / "dataset" / "product_images"
RUNTIME_DIR = Path("D:/qsm_embed_dataset/lvds_overlay_runtime")
REMOTE_OVERLAY_PNG = "/tmp/live_product_overlay_00000.png"
KMS_PROCESS: subprocess.Popen | None = None
ADB = (
    Path.home()
    / "AppData/Local/Microsoft/WinGet/Packages/"
    / "Google.PlatformTools_Microsoft.Winget.Source_8wekyb3d8bbwe/platform-tools/adb.exe"
)

ZH_NAMES = {
    "cola": "可乐",
    "noodle": "方便面",
    "chips": "薯片",
    "biscuit": "饼干",
    "milk": "牛奶",
    "bread": "面包",
    "toothpaste": "牙膏",
    "water": "矿泉水",
    "tissue": "纸巾",
    "soap": "香皂",
    "unknown": "未录入商品",
}

UNKNOWN_LABEL = "unknown"
MIN_ORB_GOOD_MATCHES = 80
MIN_ORB_VOTES = 3
CLASS_MIN_ORB_GOOD_MATCHES = {
    "cola": 70,
    "biscuit": 42,
    "tissue": 38,
}
SAFE_MARGIN_X = 72
SAFE_MARGIN_TOP = 34
SAFE_MARGIN_BOTTOM = 34
PHYSICAL_LVDS_SIZE = (1280, 800)


def run_adb(args: list[str], *, capture: bool = False, check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(ADB), *args],
        cwd=BASE_DIR,
        check=check,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
    )


def read_token(data: bytes, index: int) -> tuple[bytes, int]:
    while index < len(data) and data[index] in b" \t\r\n":
        index += 1
    start = index
    while index < len(data) and data[index] not in b" \t\r\n":
        index += 1
    return data[start:index], index


def parse_first_pgm(path: Path) -> Image.Image:
    data = path.read_bytes()
    index = 0
    magic, index = read_token(data, index)
    if magic != b"P5":
        raise RuntimeError(f"unexpected PGM magic: {magic!r}")
    width_token, index = read_token(data, index)
    height_token, index = read_token(data, index)
    maxval_token, index = read_token(data, index)
    if maxval_token != b"255":
        raise RuntimeError("unsupported PGM maxval")
    if index < len(data) and data[index] in b" \t\r\n":
        index += 1
    width = int(width_token)
    height = int(height_token)
    frame = data[index : index + width * height]
    if len(frame) != width * height:
        raise RuntimeError("truncated PGM frame")
    return Image.frombytes("L", (width, height), frame)


def image_to_feature(image: Image.Image) -> np.ndarray:
    image = image.convert("L")
    image = ImageOps.autocontrast(image)
    image.thumbnail((240, 240))
    canvas = Image.new("L", (240, 240), 0)
    canvas.paste(image, ((240 - image.width) // 2, (240 - image.height) // 2))
    arr = np.asarray(canvas, dtype=np.float32) / 255.0
    small = np.asarray(canvas.resize((32, 32)), dtype=np.float32) / 255.0
    hist, _ = np.histogram(arr, bins=32, range=(0.0, 1.0), density=True)
    row_profile = arr.mean(axis=1)[::6]
    col_profile = arr.mean(axis=0)[::6]
    feature = np.concatenate([small.reshape(-1), hist, row_profile, col_profile])
    norm = np.linalg.norm(feature)
    return feature / norm if norm > 0 else feature


def train_features() -> list[tuple[str, np.ndarray]]:
    train: list[tuple[str, np.ndarray]] = []
    for product_dir in sorted(DATASET_DIR.iterdir()):
        if not product_dir.is_dir():
            continue
        for image_path in sorted(product_dir.glob("*.png")):
            train.append((product_dir.name, image_to_feature(Image.open(image_path))))
    if not train:
        raise RuntimeError(f"no training images found in {DATASET_DIR}")
    return train


def train_orb_features() -> list[tuple[str, np.ndarray | None]]:
    if cv2 is None:
        return []
    orb = cv2.ORB_create(nfeatures=1000)
    train: list[tuple[str, np.ndarray | None]] = []
    for product_dir in sorted(DATASET_DIR.iterdir()):
        if not product_dir.is_dir():
            continue
        for image_path in sorted(product_dir.glob("*.png")):
            image = np.asarray(ImageOps.grayscale(Image.open(image_path)), dtype=np.uint8)
            image = cv2.equalizeHist(image)
            _keypoints, descriptors = orb.detectAndCompute(image, None)
            train.append((product_dir.name, descriptors))
    return train


def classify(train: list[tuple[str, np.ndarray]], feature: np.ndarray, k: int = 7) -> tuple[str, float]:
    distances = [(label, float(np.linalg.norm(train_feature - feature))) for label, train_feature in train]
    distances.sort(key=lambda item: item[1])
    top = distances[:k]
    vote = Counter(label for label, _distance in top)
    best_label, _count = vote.most_common(1)[0]
    best_distance = min(distance for label, distance in top if label == best_label)
    confidence = max(0.0, 1.0 - best_distance)
    return best_label, confidence


def classify_orb(train: list[tuple[str, np.ndarray | None]], image: Image.Image, k: int = 7) -> tuple[str, float] | None:
    if cv2 is None or not train:
        return None
    orb = cv2.ORB_create(nfeatures=1000)
    gray = np.asarray(ImageOps.grayscale(image), dtype=np.uint8)
    gray = cv2.equalizeHist(gray)
    _keypoints, descriptors = orb.detectAndCompute(gray, None)
    if descriptors is None:
        return UNKNOWN_LABEL, 0.0

    matcher = cv2.BFMatcher(cv2.NORM_HAMMING, crossCheck=True)
    scored: list[tuple[str, int, float]] = []
    for label, train_descriptors in train:
        if train_descriptors is None:
            continue
        matches = matcher.match(descriptors, train_descriptors)
        good = [match for match in matches if match.distance < 45]
        if good:
            avg_distance = sum(match.distance for match in good) / len(good)
            scored.append((label, len(good), avg_distance))
    if not scored:
        return UNKNOWN_LABEL, 0.0

    scored.sort(key=lambda item: (-item[1], item[2]))
    top = scored[:k]
    votes = Counter(label for label, _count, _distance in top)
    best_label, vote_count = votes.most_common(1)[0]
    best_good = max(count for label, count, _distance in top if label == best_label)
    min_good_matches = CLASS_MIN_ORB_GOOD_MATCHES.get(best_label, MIN_ORB_GOOD_MATCHES)
    if best_good < min_good_matches or vote_count < MIN_ORB_VOTES:
        return UNKNOWN_LABEL, 0.0
    confidence = min(0.99, max(0.0, (best_good / 80.0) * (vote_count / k)))
    return best_label, confidence


def load_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    for path in [
        Path("C:/Windows/Fonts/msyh.ttc"),
        Path("C:/Windows/Fonts/simhei.ttf"),
        Path("C:/Windows/Fonts/arial.ttf"),
    ]:
        if path.exists():
            return ImageFont.truetype(str(path), size)
    return ImageFont.load_default()


def capture_frame(local_path: Path) -> Image.Image:
    remote = "/tmp/live_product_overlay_frame.pgm"
    run_adb(["shell", "pkill -9 qr_scanner_display 2>/dev/null || true; pkill -9 camera_pgm_stream 2>/dev/null || true"], check=False)
    time.sleep(0.2)
    run_adb([
        "shell",
        "cd /userdata/Embed_project && ./bin/camera_pgm_stream -d /dev/video5 -W 800 -H 600 -n 1 > /tmp/live_product_overlay_frame.pgm",
    ])
    run_adb(["pull", remote, str(local_path)])
    return parse_first_pgm(local_path)


def draw_overlay(camera_image: Image.Image, label: str, confidence: float, panel_size: tuple[int, int]) -> Image.Image:
    # The LVDS controller reports 800x1280, while the physical panel is viewed
    # as 1280x800. Draw a human-facing landscape UI, then rotate it into the
    # controller's portrait coordinate system before sending it to KMS.
    panel_w, panel_h = PHYSICAL_LVDS_SIZE
    canvas = Image.new("RGB", (panel_w, panel_h), (0, 0, 0))

    draw = ImageDraw.Draw(canvas)
    font_big = load_font(40)
    font_mid = load_font(28)
    is_unknown = label == UNKNOWN_LABEL
    accent = (255, 180, 0) if is_unknown else (0, 255, 0)

    margin = 54
    info_box = (margin, margin, 330, panel_h - margin)
    camera_box = (365, margin, panel_w - margin, panel_h - margin)

    frame = ImageOps.contain(
        camera_image.convert("RGB"),
        (camera_box[2] - camera_box[0], camera_box[3] - camera_box[1]),
        Image.Resampling.BILINEAR,
    )
    frame_x = camera_box[0] + ((camera_box[2] - camera_box[0] - frame.width) // 2)
    frame_y = camera_box[1] + ((camera_box[3] - camera_box[1] - frame.height) // 2)
    canvas.paste(frame, (frame_x, frame_y))

    box = (
        frame_x + 20,
        frame_y + 20,
        frame_x + frame.width - 20,
        frame_y + frame.height - 20,
    )
    for i in range(5):
        draw.rectangle((box[0] - i, box[1] - i, box[2] + i, box[3] + i), outline=accent)

    zh = ZH_NAMES.get(label, label)
    title = "未录入商品\nunknown" if is_unknown else f"{zh}\n{label}"
    score = "STATUS: NOT IN CATALOG" if is_unknown else f"CONF: {confidence:.2f}"
    draw.rectangle(info_box, fill=(16, 16, 16), outline=accent, width=3)
    draw.text((info_box[0] + 20, info_box[1] + 24), "VISUAL", fill=(255, 255, 255), font=font_big)
    draw.text((info_box[0] + 20, info_box[1] + 78), "PRODUCT", fill=(255, 255, 255), font=font_big)
    draw.text((info_box[0] + 20, info_box[1] + 178), title, fill=(255, 255, 255), font=font_mid, spacing=10)
    draw.text((info_box[0] + 20, info_box[1] + 318), score, fill=(255, 255, 0), font=font_mid)
    draw.text((info_box[0] + 20, info_box[3] - 70), "LVDS LIVE", fill=accent, font=font_mid)
    return canvas.rotate(-90, expand=True)


def image_to_xrgb8888(image: Image.Image) -> bytes:
    arr = np.asarray(image.convert("RGB"), dtype=np.uint8)
    alpha = np.full((arr.shape[0], arr.shape[1], 1), 255, dtype=np.uint8)
    bgra = np.concatenate([arr[:, :, 2:3], arr[:, :, 1:2], arr[:, :, 0:1], alpha], axis=2)
    return bgra.tobytes()


def ensure_kms_display() -> None:
    global KMS_PROCESS
    if KMS_PROCESS is not None and KMS_PROCESS.poll() is None:
        return

    command = (
        "pkill -9 gst-launch-1.0 2>/dev/null || true; "
        "exec gst-launch-1.0 -q "
        "multifilesrc location=/tmp/live_product_overlay_%05d.png start-index=0 stop-index=0 loop=true "
        "caps='image/png,framerate=1/1' ! "
        "pngdec ! videoconvert ! "
        "video/x-raw,format=BGRx,width=800,height=1280,framerate=1/1 ! "
        "kmssink driver-name=rockchip connector-id=154 plane-id=73 "
        "force-modesetting=false fullscreen=true sync=false "
    )
    KMS_PROCESS = subprocess.Popen(
        [str(ADB), "shell", command],
        cwd=BASE_DIR,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.5)


def show_on_lvds(image: Image.Image, png_path: Path) -> None:
    image.save(png_path)
    run_adb(["push", str(png_path), REMOTE_OVERLAY_PNG])
    ensure_kms_display()


def main() -> None:
    parser = argparse.ArgumentParser(description="Live visual product recognition with LVDS overlay.")
    parser.add_argument("--width", type=int, default=800)
    parser.add_argument("--height", type=int, default=1280)
    parser.add_argument("--interval", type=float, default=0.2)
    parser.add_argument("--rounds", type=int, default=0, help="0 means run forever")
    args = parser.parse_args()

    RUNTIME_DIR.mkdir(parents=True, exist_ok=True)
    train = train_features()
    train_orb = train_orb_features()
    print(f"loaded {len(train)} training images from {len(set(label for label, _ in train))} classes")
    if train_orb:
        print("using ORB keypoint matching for live classification")
    print("starting LVDS overlay loop; press Ctrl+C to stop")

    index = 0
    while args.rounds == 0 or index < args.rounds:
        index += 1
        pgm_path = RUNTIME_DIR / "frame.pgm"
        png_path = RUNTIME_DIR / "frame_overlay.png"
        image = capture_frame(pgm_path)
        orb_result = classify_orb(train_orb, image)
        label, confidence = orb_result if orb_result is not None else classify(train, image_to_feature(image))
        overlay = draw_overlay(image, label, confidence, (args.width, args.height))
        show_on_lvds(overlay, png_path)
        print(f"{index:04d}: {label} {ZH_NAMES.get(label, label)} confidence={confidence:.3f}")
        time.sleep(args.interval)


if __name__ == "__main__":
    main()
