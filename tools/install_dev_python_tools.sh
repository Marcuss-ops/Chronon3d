#!/usr/bin/env bash
# Install Python-only developer tools without modifying the system interpreter.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REQUIREMENTS="$SCRIPT_DIR/requirements-dev-python.txt"

python3 -m pip install -r "$REQUIREMENTS"
echo "DEV_PYTHON_TOOLS_PASS"
