#!/usr/bin/env bash

set -euo pipefail

binary="${1:-./build/root-usb-watchdog}"
temporary_file="$(mktemp)"
trap 'rm -f "$temporary_file"' EXIT

"$binary" --help | grep -q '^Usage: root-usb-watchdog'
"$binary" --version | grep -q '^root-usb-watchdog 0\.1\.0$'

if "$binary" --unknown >/dev/null 2>&1; then
  echo "unknown argument unexpectedly succeeded" >&2
  exit 1
fi

if "$binary" --device >/dev/null 2>&1; then
  echo "missing --device value unexpectedly succeeded" >&2
  exit 1
fi

if "$binary" --check --device "$temporary_file" >/dev/null 2>&1; then
  echo "regular file unexpectedly accepted as a block device" >&2
  exit 1
fi

echo "CLI tests passed"
