import argparse
import subprocess
import sys
import time
from pathlib import Path
from tempfile import TemporaryDirectory

import cv2
import numpy as np

PRODUCTS = {
    "mineral_water": ("Mineral Water", 200),
    "cola": ("Cola", 350),
    "milk": ("Milk", 620),
    "bread": ("Bread", 480),
    "instant_noodles": ("Instant Noodles", 550),
    "chips": ("Potato Chips", 680),
    "coffee": ("Coffee", 990),
    "tea": ("Tea Drink", 450),
    "cookies": ("Cookies", 720),
    "yogurt": ("Yogurt", 580),
}

BARCODES = {
    "690100000001": "mineral_water",
    "690100000002": "cola",
    "690100000003": "milk",
    "690100000004": "bread",
    "690100000005": "instant_noodles",
    "690100000006": "chips",
    "690100000007": "coffee",
    "690100000008": "tea",
    "690100000009": "cookies",
    "690100000010": "yogurt",
}


class RetailState:
    def __init__(self):
        self.cart = {}
        self.last_payload = ""
        self.missing_frames = 0
        self.status = "READY"
        self.checkout_ready = False

    def product_id_from_payload(self, payload):
        value = payload.strip()
        if value.lower().startswith("product:"):
            value = value.split(":", 1)[1]
        lowered = value.lower()
        if lowered in PRODUCTS:
            return lowered
        if value in BARCODES:
            return BARCODES[value]
        for product_id, (name, _) in PRODUCTS.items():
            if lowered == name.lower():
                return product_id
        return None

    def total_cents(self):
        return sum(PRODUCTS[pid][1] * qty for pid, qty in self.cart.items())

    def format_money(self, cents):
        return f"CNY {cents // 100}.{cents % 100:02d}"

    def handle_payload(self, payload):
        value = payload.strip()
        lowered = value.lower()
        if lowered in {"clear", "cart:clear"}:
            self.cart.clear()
            self.status = "CART CLEARED"
            self.checkout_ready = False
            print("[retail] Cart cleared.", flush=True)
            return self.status
        if lowered in {"checkout", "pay", "cart:checkout"}:
            total = self.total_cents()
            if total <= 0:
                self.status = "CART EMPTY"
                self.checkout_ready = False
                print("[retail] Cart is empty.", flush=True)
                return self.status
            self.status = "CHECKOUT READY"
            self.checkout_ready = True
            print(
                f"[retail] Checkout ready. Pay URL: "
                f"https://pay.example.local/qsm368?amount={total}",
                flush=True,
            )
            return self.status

        product_id = self.product_id_from_payload(value)
        if product_id is None:
            self.status = "NO PRODUCT MAP"
            print(f"[retail] QR payload not mapped to a product: {payload}", flush=True)
            return self.status

        self.cart[product_id] = self.cart.get(product_id, 0) + 1
        self.checkout_ready = False
        name, price = PRODUCTS[product_id]
        total = self.total_cents()
        self.status = f"ADDED {name}"
        print(f"\n>>> QR CODE: {payload} <<<", flush=True)
        print(f"[retail] Added: {name}  unit={self.format_money(price)}", flush=True)
        print("[retail] Cart:", flush=True)
        for pid, qty in self.cart.items():
            item_name, item_price = PRODUCTS[pid]
            print(f"  - {item_name} x{qty} = {self.format_money(item_price * qty)}", flush=True)
        print(f"[retail] Total: {self.format_money(total)}", flush=True)
        return f"{self.status} TOTAL {self.format_money(total)}"

    def observe(self, payload):
        if payload:
            self.missing_frames = 0
            if payload != self.last_payload:
                self.last_payload = payload
                return self.handle_payload(payload)
            return None

        self.missing_frames += 1
        if self.missing_frames > 3:
            self.last_payload = ""
        return None


def draw_translucent_panel(image, x, y, w, h, color=(20, 20, 20), alpha=0.72):
    overlay = image.copy()
    cv2.rectangle(overlay, (x, y), (x + w, y + h), color, -1)
    cv2.addWeighted(overlay, alpha, image, 1 - alpha, 0, image)
    cv2.rectangle(image, (x, y), (x + w, y + h), (0, 220, 0), 2)


def draw_retail_panel(preview, state):
    h, w = preview.shape[:2]
    panel_w = min(420, w - 20)
    panel_h = min(230, h - 20)
    x = w - panel_w - 10
    y = 10
    draw_translucent_panel(preview, x, y, panel_w, panel_h)

    white = (245, 245, 245)
    yellow = (0, 230, 255)
    green = (0, 255, 0)
    cyan = (255, 255, 0)
    red = (40, 80, 255)
    font = cv2.FONT_HERSHEY_SIMPLEX

    cv2.putText(preview, "SMART RETAIL", (x + 14, y + 30), font, 0.75, white, 2)
    cv2.putText(preview, f"STATUS: {state.status}", (x + 14, y + 60), font, 0.55,
                cyan if state.checkout_ready else yellow, 2)

    total = state.total_cents()
    cv2.putText(preview, f"TOTAL: {state.format_money(total)}", (x + 14, y + 90),
                font, 0.65, green, 2)

    row = y + 120
    if state.cart:
        for idx, (pid, qty) in enumerate(state.cart.items()):
            if idx >= 4:
                cv2.putText(preview, "...", (x + 14, row), font, 0.55, white, 1)
                break
            name, price = PRODUCTS[pid]
            line = f"{name} x{qty} {state.format_money(price * qty)}"
            cv2.putText(preview, line[:34], (x + 14, row), font, 0.5, white, 1)
            row += 24
    else:
        cv2.putText(preview, "Cart is empty", (x + 14, row), font, 0.55, white, 1)

    if state.checkout_ready:
        pay = f"PAY amount={total}"
        cv2.putText(preview, pay, (x + 14, y + panel_h - 18), font, 0.5, cyan, 1)
    else:
        cv2.putText(preview, "Scan checkout / clear QR", (x + 14, y + panel_h - 18),
                    font, 0.5, red, 1)


def draw_qr_result(preview, detector, frame, state):
    payload, points, _ = detector.detectAndDecode(frame)
    status = state.observe(payload)
    if payload and points is not None:
        pts = points.astype(int).reshape(-1, 2)
        cv2.polylines(preview, [pts], True, (0, 255, 0), 3)
        cv2.putText(preview, payload[:32], (pts[0][0], max(24, pts[0][1] - 10)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)
    if status:
        cv2.putText(preview, status[:48], (12, 64),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)
    draw_retail_panel(preview, state)
    return preview


def read_line(stream):
    line = bytearray()
    while True:
        ch = stream.read(1)
        if not ch:
            return None
        line.extend(ch)
        if ch == b"\n":
            return bytes(line)


def read_pgm(stream):
    magic = read_line(stream)
    if magic is None:
        return None
    if magic.strip() != b"P5":
        return None

    dims = read_line(stream)
    maxval = read_line(stream)
    if dims is None or maxval is None:
        return None
    width, height = map(int, dims.split())
    if maxval.strip() != b"255":
        return None

    size = width * height
    data = stream.read(size)
    if len(data) != size:
        return None
    return np.frombuffer(data, dtype=np.uint8).reshape((height, width))


def main():
    parser = argparse.ArgumentParser(description="Preview QSM368ZP camera over adb.")
    parser.add_argument("--adb", default=None, help="Path to adb.exe")
    parser.add_argument("--device", default="/dev/video5")
    parser.add_argument("--width", type=int, default=800)
    parser.add_argument("--height", type=int, default=600)
    parser.add_argument("--mode", choices=["pull", "exec-out"], default="pull")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    adb = Path(args.adb) if args.adb else root / "tools" / "adb" / "adb.exe"
    remote = (
        f"/userdata/Embed_project/bin/camera_pgm_stream "
        f"-d {args.device} -W {args.width} -H {args.height}"
    )

    if args.mode == "pull":
        view_by_pull(adb, remote)
        return

    cmd = [str(adb), "exec-out", remote]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    if proc.stdout is None:
        raise RuntimeError("failed to open adb stdout")

    window = "QSM368ZP camera preview - press q to quit"
    frames = 0
    start = time.time()
    detector = cv2.QRCodeDetector()
    state = RetailState()
    try:
        while True:
            frame = read_pgm(proc.stdout)
            if frame is None:
                break
            frames += 1
            fps = frames / max(time.time() - start, 0.001)
            preview = cv2.cvtColor(frame, cv2.COLOR_GRAY2BGR)
            preview = draw_qr_result(preview, detector, frame, state)
            cv2.putText(preview, f"{fps:.1f} fps", (12, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)
            cv2.imshow(window, preview)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                break
    finally:
        proc.terminate()
        cv2.destroyAllWindows()
        try:
            err = proc.stderr.read().decode("utf-8", errors="ignore") if proc.stderr else ""
        except Exception:
            err = ""
        if err.strip():
            print(err.strip(), file=sys.stderr)


def view_by_pull(adb, remote):
    window = "QSM368ZP camera preview - press q to quit"
    frames = 0
    start = time.time()
    detector = cv2.QRCodeDetector()
    state = RetailState()
    with TemporaryDirectory() as tmp:
        local = Path(tmp) / "cam.pgm"
        while True:
            subprocess.run(
                [str(adb), "shell", f"{remote} -n 1 > /tmp/cam.pgm"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            )
            subprocess.run(
                [str(adb), "pull", "/tmp/cam.pgm", str(local)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            )
            frame = cv2.imread(str(local), cv2.IMREAD_GRAYSCALE)
            if frame is None:
                print("No frame received. Check adb connection and /dev/video5.", file=sys.stderr)
                time.sleep(0.5)
                continue
            frames += 1
            fps = frames / max(time.time() - start, 0.001)
            preview = cv2.cvtColor(frame, cv2.COLOR_GRAY2BGR)
            preview = draw_qr_result(preview, detector, frame, state)
            cv2.putText(preview, f"{fps:.1f} fps", (12, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)
            cv2.imshow(window, preview)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                break
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
