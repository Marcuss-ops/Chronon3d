#!/usr/bin/env bash
set -euo pipefail

# Compare a current ELF ABI against either:
#   1) a baseline ELF shared library (preferred in CI), or
#   2) a local libabigail XML baseline (kept only for explicit local workflows).
# Usage:
#   ABI_SO=... ABI_BASELINE_SO=... bash tools/sdk/check_abi_libabigail.sh
#   ABI_SO=... ABI_BASELINE=... bash tools/sdk/check_abi_libabigail.sh
# Generate a local XML baseline explicitly with:
#   ABI_UPDATE=1 ABI_SO=... ABI_BASELINE=... bash tools/sdk/check_abi_libabigail.sh

log() { printf '[check_abi_libabigail] %s\n' "$*" >&2; }
fail() { log "FAIL: $*"; exit 1; }
die() { log "INTERNAL ERROR: $*"; exit 2; }

so="${ABI_SO:-}"
baseline_so="${ABI_BASELINE_SO:-}"
baseline="${ABI_BASELINE:-tools/sdk/chronon3d_c.abi}"
[[ -n "$so" && -f "$so" ]] || die "ABI_SO must point to an ELF shared library"
command -v abidiff >/dev/null 2>&1 || die "abidiff not found; install libabigail"

if [[ "${ABI_UPDATE:-0}" == 1 ]]; then
    [[ -z "$baseline_so" ]] || die "ABI_UPDATE cannot be combined with ABI_BASELINE_SO"
    command -v abidw >/dev/null 2>&1 || die "abidw not found; install libabigail"
    mkdir -p "$(dirname "$baseline")"
    abidw --out-file "$baseline" "$so" || die "abidw failed"
    log "local baseline written: $baseline"
    exit 0
fi

if [[ -n "$baseline_so" ]]; then
    [[ -s "$baseline_so" ]] || die "ABI_BASELINE_SO must point to a non-empty ELF shared library"
    baseline_input="$baseline_so"
    baseline_label="$baseline_so"
else
    [[ -s "$baseline" ]] || die "baseline missing or empty: $baseline (set ABI_BASELINE_SO, or run ABI_UPDATE=1 for a local XML baseline)"
    baseline_input="$baseline"
    baseline_label="$baseline"
fi

abidiff --no-show-locs --no-unreferenced-symbols "$baseline_input" "$so" > abi-diff.txt || rc=$?
rc="${rc:-0}"
if [[ "$rc" -ne 0 ]]; then
    cat abi-diff.txt >&2
    fail "ABI changed; update the pinned baseline only with an explicit ABI/version review"
fi
rm -f abi-diff.txt
echo "GATE_PASS: libabigail ABI compatible with $baseline_label"
