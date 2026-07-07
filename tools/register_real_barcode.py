from __future__ import annotations

import csv
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PRODUCTS_CSV = ROOT / "data" / "products.csv"
ALIASES_CSV = ROOT / "data" / "barcode_aliases.csv"


def load_product_ids() -> set[str]:
    with PRODUCTS_CSV.open("r", encoding="utf-8", newline="") as f:
        return {row["id"] for row in csv.DictReader(f)}


def load_aliases() -> list[dict[str, str]]:
    if not ALIASES_CSV.exists():
        return []
    with ALIASES_CSV.open("r", encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def save_aliases(rows: list[dict[str, str]]) -> None:
    ALIASES_CSV.parent.mkdir(parents=True, exist_ok=True)
    with ALIASES_CSV.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["product_id", "barcode"])
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    if len(sys.argv) != 3:
        print("Usage: python tools/register_real_barcode.py <product_id> <barcode>")
        print("Example: python tools/register_real_barcode.py milk 692xxxxxxxxxx")
        return 2

    product_id = sys.argv[1].strip()
    barcode = sys.argv[2].strip()
    if not barcode.isdigit() or len(barcode) not in (8, 12, 13, 14):
        print(f"Invalid barcode: {barcode}. Use digits only, normally 13 digits for EAN-13.")
        return 2

    product_ids = load_product_ids()
    if product_id not in product_ids:
        print(f"Unknown product_id: {product_id}")
        print("Available:", ", ".join(sorted(product_ids)))
        return 2

    rows = load_aliases()
    updated = False
    for row in rows:
        if row["barcode"] == barcode:
            row["product_id"] = product_id
            updated = True
            break
    if not updated:
        rows.append({"product_id": product_id, "barcode": barcode})

    rows.sort(key=lambda row: (row["product_id"], row["barcode"]))
    save_aliases(rows)
    print(f"Registered real barcode: {barcode} -> {product_id}")
    print(f"Saved: {ALIASES_CSV}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
