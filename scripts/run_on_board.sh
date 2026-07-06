#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
"$PROJECT_DIR/build/embed_project" "$PROJECT_DIR/data/products.csv"
