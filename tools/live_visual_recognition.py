from __future__ import annotations

import argparse
import subprocess
import time
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image, ImageOps

try:
    import cv2
except ImportError:  # pragma: no cover - fallback for minimal Python envs
    cv2 = None


BASE_DIR = Path(__file__).resolve().parents[1]
DATASET_DIR = BASE_DIR / "dataset" / "product_images"
OUT_DIR = BASE_DIR / "recognition_test" / "live"
ADB = (
    Path.home()
    / "AppData/Local/Microsoft/WinGet/Packages/"
    / "Google.PlatformTools_Microsoft.Winget.Source_8wekyb3d8bbwe/platform-tools/adb.exe"
)

ZH_NAMES = {
    "cola": "可乐",
    "chips": "薯片",
    "biscuit": "饼干",
    "bread": "面包",
    "tissue": "纸巾",
    "noodle": "方便面",
    "milk": "牛奶",
    "toothpaste": "牙膏",
    "water": "矿泉水",
    "soap": "肥皂",
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


def run_adb(args: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run([str(ADB), *args], cwd=BASE_DIR, check=True, capture_output=True)


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


def classify(train: list[tuple[str, np.ndarray]], feature: np.ndarray, k: int = 7) -> tuple[str, float, list[tuple[str, float]]]:
    distances = [(label, float(np.linalg.norm(train_feature - feature))) for label, train_feature in train]
    distances.sort(key=lambda item: item[1])
    top = distances[:k]
    vote = Counter(label for label, _distance in top)
    best_label, _count = vote.most_common(1)[0]
    best_distance = min(distance for label, distance in top if label == best_label)
    confidence = max(0.0, 1.0 - best_distance)
    return best_label, confidence, top[:3]


def classify_orb(
    train: list[tuple[str, np.ndarray | None]], image: Image.Image, k: int = 7
) -> tuple[str, float, list[tuple[str, float]]] | None:
    if cv2 is None or not train:
        return None
    orb = cv2.ORB_create(nfeatures=1000)
    gray = np.asarray(ImageOps.grayscale(image), dtype=np.uint8)
    gray = cv2.equalizeHist(gray)
    _keypoints, descriptors = orb.detectAndCompute(gray, None)
    if descriptors is None:
        return UNKNOWN_LABEL, 0.0, []

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
        return UNKNOWN_LABEL, 0.0, []

    scored.sort(key=lambda item: (-item[1], item[2]))
    top = scored[:k]
    votes = Counter(label for label, _count, _distance in top)
    best_label, vote_count = votes.most_common(1)[0]
    best_good = max(count for label, count, _distance in top if label == best_label)
    min_good_matches = CLASS_MIN_ORB_GOOD_MATCHES.get(best_label, MIN_ORB_GOOD_MATCHES)
    if best_good < min_good_matches or vote_count < MIN_ORB_VOTES:
        top_text = [(label, 1.0 - min(1.0, avg_distance / 80.0)) for label, _count, avg_distance in top[:3]]
        return UNKNOWN_LABEL, 0.0, top_text
    confidence = min(0.99, max(0.0, (best_good / 80.0) * (vote_count / k)))
    top_text = [(label, 1.0 - min(1.0, avg_distance / 80.0)) for label, _count, avg_distance in top[:3]]
    return best_label, confidence, top_text


def capture_frame(local_path: Path) -> Image.Image:
    remote = "/tmp/live_product_frame.pgm"
    run_adb(["shell", "pkill -9 gst-launch-1.0 2>/dev/null || true; pkill -9 qr_scanner_display 2>/dev/null || true"])
    time.sleep(0.4)
    run_adb([
        "shell",
        "cd /userdata/Embed_project && ./bin/camera_pgm_stream -d /dev/video5 -W 800 -H 600 -n 1 > /tmp/live_product_frame.pgm",
    ])
    run_adb(["pull", remote, str(local_path)])
    return parse_first_pgm(local_path)


def main() -> None:
    parser = argparse.ArgumentParser(description="Live product recognition from QSM368ZP-WF camera.")
    parser.add_argument("-n", "--rounds", type=int, default=10)
    parser.add_argument("--interval", type=float, default=0.8)
    args = parser.parse_args()

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    train = train_features()
    train_orb = train_orb_features()
    print(f"loaded {len(train)} training images from {len(set(label for label, _ in train))} classes")
    if train_orb:
        print("using ORB keypoint matching for live classification")

    results = []
    for index in range(1, args.rounds + 1):
        frame_path = OUT_DIR / f"live_{index:03d}.pgm"
        image = capture_frame(frame_path)
        png_path = OUT_DIR / f"live_{index:03d}.png"
        image.save(png_path)
        orb_result = classify_orb(train_orb, image)
        label, confidence, top = orb_result if orb_result is not None else classify(train, image_to_feature(image))
        results.append(label)
        zh = ZH_NAMES.get(label, label)
        top_text = ", ".join(f"{name}:{distance:.3f}" for name, distance in top)
        print(f"{index:02d}: {label} / {zh} confidence={confidence:.3f} top=[{top_text}]")
        time.sleep(args.interval)

    summary = Counter(results)
    print("summary:", ", ".join(f"{label}={count}" for label, count in summary.most_common()))


if __name__ == "__main__":
    main()
