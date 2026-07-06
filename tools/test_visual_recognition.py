from __future__ import annotations

from collections import Counter, defaultdict
from pathlib import Path

import numpy as np
from PIL import Image, ImageOps


BASE_DIR = Path(__file__).resolve().parents[1]
DATASET_DIR = BASE_DIR / "dataset" / "product_images"
OUT_DIR = BASE_DIR / "recognition_test"


def load_image(path: Path) -> np.ndarray:
    image = Image.open(path).convert("L")
    image = ImageOps.autocontrast(image)
    image.thumbnail((240, 240))
    canvas = Image.new("L", (240, 240), 0)
    canvas.paste(image, ((240 - image.width) // 2, (240 - image.height) // 2))
    return np.asarray(canvas, dtype=np.float32) / 255.0


def extract_features(path: Path) -> np.ndarray:
    image = load_image(path)
    small = np.asarray(Image.fromarray((image * 255).astype(np.uint8)).resize((32, 32)), dtype=np.float32) / 255.0
    hist, _ = np.histogram(image, bins=32, range=(0.0, 1.0), density=True)
    row_profile = image.mean(axis=1)[::6]
    col_profile = image.mean(axis=0)[::6]
    feature = np.concatenate([small.reshape(-1), hist, row_profile, col_profile])
    norm = np.linalg.norm(feature)
    if norm > 0:
        feature = feature / norm
    return feature


def collect_samples() -> dict[str, list[Path]]:
    samples: dict[str, list[Path]] = {}
    for product_dir in sorted(DATASET_DIR.iterdir()):
        if not product_dir.is_dir():
            continue
        images = sorted(product_dir.glob("*.png"))
        if images:
            samples[product_dir.name] = images
    return samples


def classify(train: list[tuple[str, np.ndarray]], feature: np.ndarray) -> tuple[str, float]:
    distances = [(label, float(np.linalg.norm(train_feature - feature))) for label, train_feature in train]
    distances.sort(key=lambda item: item[1])
    top = distances[:5]
    vote = Counter(label for label, _distance in top)
    best_label, _ = vote.most_common(1)[0]
    best_distance = min(distance for label, distance in top if label == best_label)
    confidence = max(0.0, 1.0 - best_distance)
    return best_label, confidence


def main() -> None:
    OUT_DIR.mkdir(exist_ok=True)
    samples = collect_samples()
    if len(samples) < 2:
        raise SystemExit("need at least two product classes")

    train: list[tuple[str, np.ndarray]] = []
    tests: list[tuple[str, Path, np.ndarray]] = []
    counts: dict[str, tuple[int, int]] = {}
    for label, paths in samples.items():
        for index, path in enumerate(paths):
            feature = extract_features(path)
            if index % 3 == 0:
                tests.append((label, path, feature))
            else:
                train.append((label, feature))
        counts[label] = (len(paths) - sum(1 for i in range(len(paths)) if i % 3 == 0), sum(1 for i in range(len(paths)) if i % 3 == 0))

    correct = 0
    confusion: dict[str, Counter[str]] = defaultdict(Counter)
    rows = ["actual,predicted,confidence,file"]
    mistakes: list[Path] = []

    for actual, path, feature in tests:
        predicted, confidence = classify(train, feature)
        if predicted == actual:
            correct += 1
        else:
            mistakes.append(path)
        confusion[actual][predicted] += 1
        rows.append(f"{actual},{predicted},{confidence:.4f},{path.name}")

    accuracy = correct / len(tests) if tests else 0.0
    (OUT_DIR / "predictions.csv").write_text("\n".join(rows) + "\n", encoding="utf-8")

    report = []
    report.append(f"classes: {', '.join(samples)}")
    report.append(f"accuracy: {correct}/{len(tests)} = {accuracy:.2%}")
    report.append("")
    report.append("samples:")
    for label, (train_count, test_count) in counts.items():
        report.append(f"  {label}: train={train_count}, test={test_count}")
    report.append("")
    report.append("confusion:")
    for actual in sorted(confusion):
        parts = ", ".join(f"{pred}={count}" for pred, count in sorted(confusion[actual].items()))
        report.append(f"  {actual}: {parts}")
    report.append("")
    report.append("mistakes:")
    if mistakes:
        for path in mistakes:
            report.append(f"  {path.parent.name}/{path.name}")
    else:
        report.append("  none")

    (OUT_DIR / "report.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    print("\n".join(report))


if __name__ == "__main__":
    main()
