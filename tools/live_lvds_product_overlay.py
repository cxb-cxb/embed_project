from __future__ import annotations

import argparse
import json
import subprocess
import threading
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
CURRENT_PRODUCT_STATE = RUNTIME_DIR / "current_product.json"
VOICE_REPLY_STATE = RUNTIME_DIR / "voice_reply.json"
REMOTE_OVERLAY_PNG = "/tmp/live_product_overlay_00000.png"
REMOTE_OVERLAY_PNG_ALT = "/tmp/live_product_overlay_00001.png"
REMOTE_OVERLAY_PATTERN = "/tmp/live_product_overlay_%05d.png"
REMOTE_OVERLAY_RAW = "/tmp/live_product_overlay.fb"
REMOTE_STREAM_PGM = "/tmp/live_product_overlay_stream.pgm"
REMOTE_PREVIEW_PATTERN = "/tmp/live_product_preview_%05d.jpg"
KMS_PROCESS: subprocess.Popen | None = None
CAMERA_PROCESS: subprocess.Popen | None = None
CLASSIFIER_STOP = threading.Event()
OVERLAY_SLOT = 0
ADB = (
    Path.home()
    / "AppData/Local/Microsoft/WinGet/Packages/"
    / "Google.PlatformTools_Microsoft.Winget.Source_8wekyb3d8bbwe/platform-tools/adb.exe"
)

CAMERA_CLEANUP_CMD = (
    "for p in $(ps -ef | grep '[l]ive_product_overlay_stream' | awk '{print $2}'); do "
    "[ \"$p\" = \"$$\" ] || kill -9 \"$p\" 2>/dev/null || true; "
    "done; "
    "for p in $(ps -ef | grep '[c]amera_pgm_stream' | awk '{print $2}'); do "
    "[ \"$p\" = \"$$\" ] || kill -9 \"$p\" 2>/dev/null || true; "
    "done; "
    "pkill -9 qr_scanner_display 2>/dev/null || true; "
    "pkill -9 camera_pgm_stream 2>/dev/null || true; "
    "pkill -9 gst-launch-1.0 2>/dev/null || true; "
    "fuser -k /dev/video5 2>/dev/null || true; "
    "sleep 1"
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


def read_exact(stream, size: int) -> bytes:
    chunks: list[bytes] = []
    remaining = size
    while remaining > 0:
        chunk = stream.read(remaining)
        if not chunk:
            raise EOFError("camera stream ended")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def read_stream_token(stream) -> bytes:
    token = bytearray()
    while True:
        char = stream.read(1)
        if not char:
            if token:
                return bytes(token)
            raise EOFError("camera stream ended")
        if char in b" \t\r\n":
            if token:
                return bytes(token)
            continue
        token.extend(char)


def read_pgm_frame_from_stream(stream) -> Image.Image:
    magic = read_stream_token(stream)
    if magic != b"P5":
        raise RuntimeError(f"unexpected PGM magic from stream: {magic!r}")
    width = int(read_stream_token(stream))
    height = int(read_stream_token(stream))
    maxval = read_stream_token(stream)
    if maxval != b"255":
        raise RuntimeError(f"unsupported PGM maxval from stream: {maxval!r}")
    frame = read_exact(stream, width * height)
    return Image.frombytes("L", (width, height), frame)


def start_camera_stream(width: int, height: int) -> subprocess.Popen:
    global CAMERA_PROCESS
    if CAMERA_PROCESS is not None and CAMERA_PROCESS.poll() is None:
        return CAMERA_PROCESS

    run_adb(["shell", CAMERA_CLEANUP_CMD], check=False)
    remote_cmd = (
        "cd /userdata/Embed_project && "
        f"./bin/camera_pgm_stream -d /dev/video5 -W {width} -H {height}"
    )
    CAMERA_PROCESS = subprocess.Popen(
        [str(ADB), "exec-out", "sh", "-c", remote_cmd],
        cwd=BASE_DIR,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    if CAMERA_PROCESS.stdout is None:
        raise RuntimeError("failed to open camera stream stdout")
    time.sleep(0.3)
    return CAMERA_PROCESS


def start_remote_frame_writer(width: int, height: int) -> None:
    global CAMERA_PROCESS
    if CAMERA_PROCESS is not None and CAMERA_PROCESS.poll() is None:
        return

    run_adb(["shell", CAMERA_CLEANUP_CMD], check=False)
    frame_bytes = len(f"P5\n{width} {height}\n255\n".encode("ascii")) + width * height
    command = (
        "cd /userdata/Embed_project || exit 1; "
        f"rm -f {REMOTE_STREAM_PGM} {REMOTE_STREAM_PGM}.tmp /tmp/live_product_overlay_stream.err; "
        f"./bin/camera_pgm_stream -d /dev/video5 -W {width} -H {height} 2>/tmp/live_product_overlay_stream.err | "
        "while true; do "
        f"dd iflag=fullblock bs={frame_bytes} count=1 of={REMOTE_STREAM_PGM}.tmp 2>/dev/null || break; "
        f"bytes=$(wc -c < {REMOTE_STREAM_PGM}.tmp 2>/dev/null || echo 0); "
        f"[ \"$bytes\" -eq {frame_bytes} ] || break; "
        f"mv {REMOTE_STREAM_PGM}.tmp {REMOTE_STREAM_PGM}; "
        "done"
    )
    CAMERA_PROCESS = subprocess.Popen(
        [str(ADB), "shell", command],
        cwd=BASE_DIR,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.6)


def pull_latest_remote_frame(local_path: Path) -> Image.Image:
    for _ in range(20):
        result = run_adb(["shell", f"test -s {REMOTE_STREAM_PGM}"], capture=True, check=False)
        if result.returncode == 0:
            break
        time.sleep(0.05)
    else:
        err = run_adb(["shell", "cat /tmp/live_product_overlay_stream.err 2>/dev/null || true"], capture=True, check=False)
        detail = err.stdout.decode("utf-8", errors="ignore").strip()
        if detail:
            raise RuntimeError(f"no live frame produced: {detail}")
        raise RuntimeError("no live frame produced")
    run_adb(["pull", REMOTE_STREAM_PGM, str(local_path)], capture=True, check=True)
    return parse_first_pgm(local_path)


def start_board_preview_pipeline(width: int, height: int) -> None:
    global KMS_PROCESS
    if KMS_PROCESS is not None and KMS_PROCESS.poll() is None:
        return

    run_adb(["shell", CAMERA_CLEANUP_CMD], check=False)
    command = (
        "rm -f /tmp/live_product_preview_*.jpg /tmp/live_product_preview.err; "
        "exec gst-launch-1.0 -q "
        f"v4l2src device=/dev/video5 ! video/x-raw,width={width},height={height},format=NV12,framerate=30/1 ! "
        "tee name=t "
        "t. ! queue ! videoconvert ! videoscale add-borders=true ! "
        "video/x-raw,format=BGRx,width=800,height=1280 ! "
        "kmssink driver-name=rockchip connector-id=154 plane-id=73 "
        "force-modesetting=false fullscreen=true sync=false "
        "t. ! queue leaky=downstream max-size-buffers=1 ! videorate ! "
        "video/x-raw,framerate=1/1 ! jpegenc ! "
        f"multifilesink location={REMOTE_PREVIEW_PATTERN} max-files=8 "
        "2>/tmp/live_product_preview.err"
    )
    KMS_PROCESS = subprocess.Popen(
        [str(ADB), "shell", command],
        cwd=BASE_DIR,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(1.0)


def pull_latest_preview_frame(local_path: Path) -> Image.Image:
    latest = ""
    for _ in range(30):
        result = run_adb(
            ["shell", "ls -t /tmp/live_product_preview_*.jpg 2>/dev/null | head -1"],
            capture=True,
            check=False,
        )
        latest = result.stdout.decode("utf-8", errors="ignore").strip()
        if latest:
            break
        time.sleep(0.1)
    if not latest:
        err = run_adb(["shell", "cat /tmp/live_product_preview.err 2>/dev/null || true"], capture=True, check=False)
        detail = err.stdout.decode("utf-8", errors="ignore").strip()
        if detail:
            raise RuntimeError(f"no preview frame produced: {detail}")
        raise RuntimeError("no preview frame produced")
    run_adb(["pull", latest, str(local_path)], capture=True, check=True)
    return Image.open(local_path).convert("L")


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


def read_voice_reply_state() -> dict[str, str]:
    try:
        data = json.loads(VOICE_REPLY_STATE.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return {}
    if not isinstance(data, dict):
        return {}
    return {
        "question": str(data.get("question", "")),
        "reply": str(data.get("reply", "")),
        "updated_at": str(data.get("updated_at", "")),
    }


def wrap_text_to_width(
    draw: ImageDraw.ImageDraw,
    text: str,
    font: ImageFont.FreeTypeFont | ImageFont.ImageFont,
    max_width: int,
    max_lines: int,
) -> list[str]:
    lines: list[str] = []
    current = ""
    for char in text:
        candidate = current + char
        bbox = draw.textbbox((0, 0), candidate, font=font)
        if bbox[2] - bbox[0] <= max_width:
            current = candidate
            continue
        if current:
            lines.append(current)
        current = char
        if len(lines) >= max_lines:
            break
    if current and len(lines) < max_lines:
        lines.append(current)
    if len(lines) == max_lines and len("".join(lines)) < len(text):
        lines[-1] = lines[-1].rstrip("。,.， ") + "..."
    return lines


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
    font_small = load_font(22)
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
    voice = read_voice_reply_state()
    if voice.get("reply"):
        voice_top = info_box[1] + 385
        voice_box = (info_box[0] + 14, voice_top, info_box[2] - 14, info_box[3] - 96)
        draw.rectangle(voice_box, fill=(4, 4, 4), outline=(0, 180, 255), width=2)
        draw.text((voice_box[0] + 10, voice_box[1] + 10), "VOICE QA", fill=(0, 210, 255), font=font_small)
        question = "Q: " + voice.get("question", "")
        reply = "A: " + voice.get("reply", "")
        y = voice_box[1] + 44
        for line in wrap_text_to_width(draw, question, font_small, voice_box[2] - voice_box[0] - 20, 2):
            draw.text((voice_box[0] + 10, y), line, fill=(255, 255, 255), font=font_small)
            y += 28
        y += 4
        for line in wrap_text_to_width(draw, reply, font_small, voice_box[2] - voice_box[0] - 20, 5):
            draw.text((voice_box[0] + 10, y), line, fill=(255, 255, 0), font=font_small)
            y += 28

    draw.text((info_box[0] + 20, info_box[3] - 70), "LVDS LIVE", fill=accent, font=font_mid)
    return canvas.rotate(-90, expand=True)


def image_to_xrgb8888(image: Image.Image) -> bytes:
    arr = np.asarray(image.convert("RGB"), dtype=np.uint8)
    alpha = np.full((arr.shape[0], arr.shape[1], 1), 255, dtype=np.uint8)
    bgra = np.concatenate([arr[:, :, 2:3], arr[:, :, 1:2], arr[:, :, 0:1], alpha], axis=2)
    return bgra.tobytes()


def ensure_kms_display(*, restart: bool = False) -> None:
    global KMS_PROCESS
    if restart:
        run_adb(["shell", "pkill -9 gst-launch-1.0 2>/dev/null || true"], check=False)
        KMS_PROCESS = None
    elif KMS_PROCESS is not None and KMS_PROCESS.poll() is None:
        return

    command = (
        "exec gst-launch-1.0 -q "
        f"multifilesrc location={REMOTE_OVERLAY_PATTERN} start-index=0 stop-index=1 loop=true "
        "caps='image/png,framerate=15/1' ! "
        "pngdec ! videoconvert ! "
        "video/x-raw,format=BGRx,width=800,height=1280,framerate=15/1 ! "
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


def show_on_lvds(image: Image.Image, png_path: Path, display_mode: str) -> None:
    global OVERLAY_SLOT
    image.save(png_path)
    if KMS_PROCESS is None or KMS_PROCESS.poll() is not None:
        run_adb(["push", str(png_path), REMOTE_OVERLAY_PNG], capture=True)
        run_adb(["push", str(png_path), REMOTE_OVERLAY_PNG_ALT], capture=True)
        ensure_kms_display(restart=True)
        OVERLAY_SLOT = 1
        return

    remote = REMOTE_OVERLAY_PNG_ALT if OVERLAY_SLOT else REMOTE_OVERLAY_PNG
    run_adb(["push", str(png_path), remote], capture=True)
    OVERLAY_SLOT = 1 - OVERLAY_SLOT


def write_current_product_state(label: str, confidence: float) -> None:
    payload = {
        "label": label,
        "name_zh": ZH_NAMES.get(label, label),
        "confidence": round(float(confidence), 3),
        "updated_at": time.strftime("%Y-%m-%d %H:%M:%S"),
    }
    CURRENT_PRODUCT_STATE.write_text(json.dumps(payload, ensure_ascii=False), encoding="utf-8")


def classify_image(
    image: Image.Image,
    train: list[tuple[str, np.ndarray]],
    train_orb: list[tuple[str, np.ndarray | None]],
) -> tuple[str, float]:
    orb_result = classify_orb(train_orb, image)
    return orb_result if orb_result is not None else classify(train, image_to_feature(image))


def classifier_worker(
    state: dict,
    lock: threading.Lock,
    train: list[tuple[str, np.ndarray]],
    train_orb: list[tuple[str, np.ndarray | None]],
    period: float,
    classify_every: int,
) -> None:
    last_seen_id = 0
    classify_every = max(1, classify_every)
    while not CLASSIFIER_STOP.is_set():
        with lock:
            image = state.get("image")
            frame_id = state.get("frame_id", 0)
        if image is None or frame_id == last_seen_id:
            CLASSIFIER_STOP.wait(0.05)
            continue
        if frame_id % classify_every != 0 and last_seen_id != 0:
            last_seen_id = frame_id
            CLASSIFIER_STOP.wait(0.02)
            continue
        last_seen_id = frame_id
        try:
            label, confidence = classify_image(image.copy(), train, train_orb)
        except Exception as exc:  # keep display alive if classification trips
            print(f"classifier warning: {exc}", flush=True)
            CLASSIFIER_STOP.wait(period)
            continue
        with lock:
            state["label"] = label
            state["confidence"] = confidence
            state["classified_frame_id"] = frame_id
        write_current_product_state(label, confidence)
        CLASSIFIER_STOP.wait(period)


def main() -> None:
    parser = argparse.ArgumentParser(description="Live visual product recognition with LVDS overlay.")
    parser.add_argument("--width", type=int, default=800)
    parser.add_argument("--height", type=int, default=1280)
    parser.add_argument("--interval", type=float, default=0.2)
    parser.add_argument("--rounds", type=int, default=0, help="0 means run forever")
    parser.add_argument("--camera-width", type=int, default=800)
    parser.add_argument("--camera-height", type=int, default=600)
    parser.add_argument("--classify-every", type=int, default=5, help="classify every N streamed frames")
    parser.add_argument("--classify-period", type=float, default=1.0, help="seconds between background classifications")
    parser.add_argument("--single-shot", action="store_true", help="use old one-frame capture loop")
    parser.add_argument("--stream-mode", choices=["pull", "exec-out"], default="pull")
    parser.add_argument("--display-mode", choices=["preview", "fb", "kms"], default="preview")
    args = parser.parse_args()

    RUNTIME_DIR.mkdir(parents=True, exist_ok=True)
    train = train_features()
    train_orb = train_orb_features()
    print(f"loaded {len(train)} training images from {len(set(label for label, _ in train))} classes")
    if train_orb:
        print("using ORB keypoint matching for live classification")
    mode = "single-shot capture" if args.single_shot else f"continuous camera stream ({args.stream_mode})"
    print(f"starting LVDS overlay loop in {mode}; press Ctrl+C to stop")

    index = 0
    label = UNKNOWN_LABEL
    confidence = 0.0
    camera_proc: subprocess.Popen | None = None
    state_lock = threading.Lock()
    state = {
        "image": None,
        "frame_id": 0,
        "label": label,
        "confidence": confidence,
        "classified_frame_id": 0,
    }
    classifier_thread: threading.Thread | None = None
    if args.display_mode == "preview" and not args.single_shot:
        start_board_preview_pipeline(args.camera_width, args.camera_height)
    elif not args.single_shot and args.stream_mode == "exec-out":
        camera_proc = start_camera_stream(args.camera_width, args.camera_height)
    elif not args.single_shot:
        start_remote_frame_writer(args.camera_width, args.camera_height)
        CLASSIFIER_STOP.clear()
        classifier_thread = threading.Thread(
            target=classifier_worker,
            args=(state, state_lock, train, train_orb, max(0.1, args.classify_period), max(1, args.classify_every)),
            daemon=True,
        )
        classifier_thread.start()

    try:
        while args.rounds == 0 or index < args.rounds:
            index += 1
            pgm_path = RUNTIME_DIR / "frame.pgm"
            png_path = RUNTIME_DIR / "frame_overlay.png"
            if args.single_shot:
                image = capture_frame(pgm_path)
                label, confidence = classify_image(image, train, train_orb)
                write_current_product_state(label, confidence)
            elif args.display_mode == "preview":
                if KMS_PROCESS is None or KMS_PROCESS.poll() is not None:
                    start_board_preview_pipeline(args.camera_width, args.camera_height)
                image = pull_latest_preview_frame(RUNTIME_DIR / "preview.jpg")
                label, confidence = classify_image(image, train, train_orb)
                write_current_product_state(label, confidence)
            elif args.stream_mode == "pull":
                if CAMERA_PROCESS is None or CAMERA_PROCESS.poll() is not None:
                    start_remote_frame_writer(args.camera_width, args.camera_height)
                image = pull_latest_remote_frame(pgm_path)
                with state_lock:
                    state["image"] = image.copy()
                    state["frame_id"] = index
                    label = state.get("label", UNKNOWN_LABEL)
                    confidence = state.get("confidence", 0.0)
            else:
                if camera_proc is None or camera_proc.stdout is None or camera_proc.poll() is not None:
                    camera_proc = start_camera_stream(args.camera_width, args.camera_height)
                image = read_pgm_frame_from_stream(camera_proc.stdout)
                if index == 1 or index % max(1, args.classify_every) == 0:
                    label, confidence = classify_image(image, train, train_orb)
                    write_current_product_state(label, confidence)

            if args.display_mode != "preview":
                overlay = draw_overlay(image, label, confidence, (args.width, args.height))
                show_on_lvds(overlay, png_path, args.display_mode)
            print(f"{index:04d}: {label} {ZH_NAMES.get(label, label)} confidence={confidence:.3f}", flush=True)
            if args.display_mode == "preview":
                time.sleep(max(0.1, args.classify_period))
            elif args.single_shot and args.interval > 0:
                time.sleep(args.interval)
    finally:
        CLASSIFIER_STOP.set()
        if classifier_thread is not None:
            classifier_thread.join(timeout=1.0)
        if args.display_mode == "preview":
            run_adb(["shell", CAMERA_CLEANUP_CMD], check=False)
        if CAMERA_PROCESS is not None and CAMERA_PROCESS.poll() is None:
            CAMERA_PROCESS.terminate()


if __name__ == "__main__":
    main()
