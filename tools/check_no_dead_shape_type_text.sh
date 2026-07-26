#!/usr/bin/env bash
# check_no_dead_shape_type_text.sh
#
# ShapeType::Text was retired.  Text production and dispatch use TextRun;
# the legacy variant payload remains index-stable but is intentionally not
# addressable through the public discriminant.

set -euo pipefail

GATE_NAME=check_no_dead_shape_type_text
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT"

matches="$(rg -n --hidden \
    --glob '!build/**' \
    --glob '!build-*/**' \
    --glob '!cmake-build-*/**' \
    --glob '*.{h,hpp,hh,c,cc,cpp,cxx}' \
    -e 'ShapeType::Text\b' include src tests content apps 2>/dev/null || true)"

if [[ -n "$matches" ]]; then
    echo "GATE_FAIL: retired ShapeType::Text reference(s) detected:" >&2
    echo "$matches" | sed 's/^/  /' >&2
    exit 1
fi

echo "GATE_PASS: ShapeType::Text remains retired"
echo "[INFO] ${GATE_NAME}: active text production uses ShapeType::TextRun"
exit 0
