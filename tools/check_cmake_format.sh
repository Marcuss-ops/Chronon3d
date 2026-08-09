#!/usr/bin/env bash
# Check only supplied/changed CMake files. cmake-format is a formatter, not a parser.
set -euo pipefail

GATE_NAME=check_cmake_format
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if [[ $# -gt 0 ]]; then
    mapfile -t FILES < <(printf '%s\n' "$@")
else
    mapfile -t FILES < <({
        git diff --name-only --diff-filter=ACMR HEAD^ HEAD 2>/dev/null || true
        git diff --name-only --diff-filter=ACMR
        git diff --cached --name-only --diff-filter=ACMR
    } | sort -u)
fi

CM_FILES=()
for file in "${FILES[@]}"; do
    [[ "$file" == "CMakeLists.txt" || "$file" == *.cmake ]] || continue
    [[ -f "$file" ]] || continue
    CM_FILES+=("$file")
done

if [[ ${#CM_FILES[@]} -eq 0 ]]; then
    echo "GATE_PASS: no changed CMake files to check"
    echo "[INFO] ${GATE_NAME}: targeted check skipped because the change set has no CMake files"
    exit 0
fi

if ! command -v cmake-format >/dev/null 2>&1; then
    echo "GATE_FAIL: cmake-format is required for targeted CMake checks" >&2
    echo "  Install: python3 -m pip install --user cmakelang" >&2
    exit 1
fi

if ! cmake-format --check "${CM_FILES[@]}"; then
    echo "GATE_FAIL: CMake formatting drift in: ${CM_FILES[*]}" >&2
    echo "  Fix with: cmake-format -i <file>" >&2
    exit 1
fi

echo "GATE_PASS: targeted cmake-format check passed (${#CM_FILES[@]} file(s))"
echo "[INFO] ${GATE_NAME}: historical CMake files were not reformatted"
