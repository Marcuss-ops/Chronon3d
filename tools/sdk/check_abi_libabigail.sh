#!/usr/bin/env bash
set -euo pipefail

# Compare an installed ELF ABI against a committed libabigail baseline.
# Usage:
#   ABI_SO=... ABI_BASELINE=... bash tools/sdk/check_abi_libabigail.sh
# Generate a baseline explicitly with:
#   ABI_UPDATE=1 ABI_SO=... ABI_BASELINE=... bash tools/sdk/check_abi_libabigail.sh

log() { printf '[check_abi_libabigail] %s\n' "$*" >&2; }
fail() { log "FAIL: $*"; exit 1; }
die() { log "INTERNAL ERROR: $*"; exit 2; }

so="${ABI_SO:-}"
baseline="${ABI_BASELINE:-tools/sdk/chronon3d_c.abi}"
[[ -n "$so" && -f "$so" ]] || die "ABI_SO must point to an ELF shared library"
command -v abidiff >/dev/null 2>&1 || die "abidiff not found; install libabigail"

if [[ "${ABI_UPDATE:-0}" == 1 ]]; then
    mkdir -p "$(dirname "$baseline")"
    abidw --out-file "$baseline" "$so" || die "abidw failed"
    log "baseline written: $baseline"
    exit 0
fi

[[ -s "$baseline" ]] || die "baseline missing or empty: $baseline (run ABI_UPDATE=1 once for an intentional baseline)"
abidiff --no-show-locs --no-unreferenced-symbols "$baseline" "$so" > abi-diff.txt || rc=$?
rc="${rc:-0}"
if [[ "$rc" -ne 0 ]]; then
    cat abi-diff.txt >&2
    fail "ABI changed; update the baseline only with an explicit ABI/version review"
fi
rm -f abi-diff.txt
echo "GATE_PASS: libabigail ABI compatible with $baseline"
