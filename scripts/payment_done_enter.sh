#!/bin/sh
set -eu

PAYMENT_DONE_FILE="${PAYMENT_DONE_FILE:-/tmp/qsm_payment_done}"

echo "Payment QR is showing: press Enter after the customer has paid."
read -r _line || true
: > "$PAYMENT_DONE_FILE"
echo "Payment complete signal sent: $PAYMENT_DONE_FILE"
