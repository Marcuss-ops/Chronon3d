#!/usr/bin/env bash
# Canonical same-SHA certification entry point.
#
# This is intentionally not part of the pre-push developer gate chain: it
# executes every discovered CTest suite (twice by default) and the selected
# gate profile, so it is a CI/WBH certification command.
#
# Usage:
#   bash tools/verify_same_sha_certification.sh \
#     --build-dir build/chronon/linux-fast-dev --profile all

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 "$ROOT/tools/verify_same_sha_certification.py" "$@"
