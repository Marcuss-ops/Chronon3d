#!/usr/bin/env bash
set -euo pipefail

# Generate a reviewable candidate from the actual shared library. This never
# replaces the pinned ABI baseline automatically: ABI major changes require
# an explicit compatibility review.
so="${CABI_SO:-}"
[[ -n "$so" && -f "$so" ]] || {
    echo "usage: CABI_SO=/path/to/libchronon3d_c.so $0 [OUTPUT]" >&2
    exit 2
}
command -v nm >/dev/null 2>&1 || { echo "nm not found" >&2; exit 2; }
output="${1:-/dev/stdout}"
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

nm -D --defined-only "$so" \
    | awk '{ name=$3; sub(/@.*/, "", name); if (name ~ /^chronon_/) print name }' \
    | sort -u >"$tmp"
[[ -s "$tmp" ]] || { echo "no chronon_* exports found in $so" >&2; exit 2; }

{
    echo "# Candidate generated from $so; review before adopting as an ABI baseline."
    cat "$tmp"
} >"$output"
echo "Generated $(wc -l <"$tmp" | tr -d ' ') candidate ABI symbols at $output" >&2
