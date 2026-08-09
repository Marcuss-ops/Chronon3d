#!/usr/bin/env bash
# Install the reproducible Ubuntu/Debian development-tool set.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REQUIREMENTS="$SCRIPT_DIR/requirements-dev-ubuntu.txt"

if ! command -v apt-get >/dev/null 2>&1; then
    echo "DEV_TOOLS_FAIL: apt-get is required (Ubuntu/Debian only)" >&2
    exit 1
fi

mapfile -t PACKAGES < <(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "$REQUIREMENTS")
echo "Installing ${#PACKAGES[@]} development packages from $REQUIREMENTS"
sudo apt-get update
sudo apt-get install -y --no-install-recommends "${PACKAGES[@]}"
echo "DEV_TOOLS_PASS"
