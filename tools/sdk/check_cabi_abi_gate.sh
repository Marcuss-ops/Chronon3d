#!/usr/bin/env bash
# tools/sdk/check_cabi_abi_gate.sh
#
# Frozen C ABI v2 product gate.  Verifies that the installed shared library
# still satisfies the ABI 2 contract by comparing its exported symbols
# (nm -D --defined-only) against a committed baseline, plus two invariants:
#
#   1. No ABI2 symbol removed.  Every symbol listed in the baseline must still
#      be exported.  A missing symbol -> FAIL.
#   2. No signature-level type change.  Every baseline symbol must keep its
#      function type (`T` in nm output).  A change to data/weak/… (`D`/`W`/…)
#      is treated as a signature-incompatibility proxy -> FAIL.
#   3. SOVERSION == ABI major.  The ELF SONAME must be
#      libchronon3d_c.so.<abi_major>.  A breaking ABI change must bump BOTH
#      SOVERSION (cmake/Chronon3DSdkTargets.cmake) AND chronon_abi_version()
#      (src/c_api/chronon3d_c_api.cpp) AND rename the baseline file to
#      cabi_symbol_baseline_v<N>.txt, all in one commit.
#
# Additive symbols (new chronon_* names not in the baseline) are OK and are
# reported as informational output.  Full parameter-level signature diffing
# (abi-dumper / abi-compliance-checker) is a documented future enhancement;
# this gate is the drift-free first line of ABI defense.
#
# Env inputs:
#   CABI_SO           — direct path to libchronon3d_c.so (overrides lookup)
#   SDK_PREFIX        — install prefix root (used to locate libchronon3d_c.so)
#   CABI_ABI_BASELINE — override path to the baseline file
#   CABI_ABI_MAJOR    — override the ABI major (default 2)
#
# Exit codes:
#   0 = GATE_PASS  — baseline intact, types unchanged, SONAME == ABI major
#   1 = GATE_FAIL  — removed symbol / type change / SONAME mismatch
#   2 = internal error (missing .so / nm / readelf / baseline)
#
# Invocation pattern:
#   SDK_PREFIX=/path/to/prefix bash tools/sdk/check_cabi_abi_gate.sh
#   CABI_SO=/path/to/libchronon3d_c.so bash tools/sdk/check_cabi_abi_gate.sh

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

log()  { printf '[check_cabi_abi_gate] %s\n' "$*" >&2; }
die2() { log "INTERNAL ERROR: $*"; exit 2; }

abi_major="${CABI_ABI_MAJOR:-2}"
baseline="${CABI_ABI_BASELINE:-$HERE/cabi_symbol_baseline_v2.txt}"

[[ -f "$baseline" ]] || die2 "ABI baseline not found: $baseline"

# ── Locate the shared library ──────────────────────────────────────────────
so="${CABI_SO:-}"
if [[ -z "$so" ]]; then
    : "${SDK_PREFIX:?SDK_PREFIX or CABI_SO env var required}"
    so="$(find "$SDK_PREFIX" -name 'libchronon3d_c.so' 2>/dev/null | head -1 || true)"
    [[ -z "$so" ]] && so="$(find "$SDK_PREFIX" -name 'libchronon3d_c.so.*' -type f 2>/dev/null | head -1 || true)"
fi
[[ -n "$so" && -e "$so" ]] || die2 "libchronon3d_c.so not found"

command -v nm >/dev/null 2>&1     || die2 "nm not on PATH"
command -v readelf >/dev/null 2>&1 || die2 "readelf not on PATH"

log "ABI gate starting (so=$so abi_major=$abi_major baseline=$baseline)"

# ── Current exported chronon_* symbols (name + nm type) ────────────────────
nm_out="$(nm -D --defined-only "$so" 2>/dev/null)" \
    || die2 "nm -D --defined-only failed on $so"

# Emit "name type" pairs for chronon_* symbols, stripping any @version suffix.
current="$(printf '%s\n' "$nm_out" \
    | awk '{ n=$3; sub(/@.*/, "", n); if (n ~ /^chronon_/) print n" "$2 }' \
    | sort -u)"

# ── Baseline symbols ────────────────────────────────────────────────────────
baseline_syms="$(grep -vE '^[[:space:]]*(#|$)' "$baseline" | awk '{print $1}' | sort -u || true)"
[[ -n "$baseline_syms" ]] || die2 "baseline $baseline is empty (no symbol lines)"

# ── Removed symbols + type changes (signature proxy) ───────────────────────
removed=()
type_changed=()
while IFS= read -r name; do
    [[ -n "$name" ]] || continue
    line="$(printf '%s\n' "$current" | grep -E "^${name} " || true)"
    if [[ -z "$line" ]]; then
        removed+=("$name")
        continue
    fi
    type="${line#* }"
    if [[ "$type" != "T" ]]; then
        type_changed+=("$name ($type)")
    fi
done <<< "$baseline_syms"

# ── New additive symbols (OK) ───────────────────────────────────────────────
new_syms=()
while IFS= read -r line; do
    [[ -n "$line" ]] || continue
    name="${line%% *}"
    if ! printf '%s\n' "$baseline_syms" | grep -Fxq "$name"; then
        new_syms+=("$name")
    fi
done <<< "$current"

# ── SONAME must track the ABI major ────────────────────────────────────────
soname="$(readelf -d "$so" 2>/dev/null | sed -n 's/.*(SONAME).*\[\(.*\)\].*/\1/p' || true)"
[[ -n "$soname" ]] || die2 "no SONAME found in $so (not an ELF shared object?)"
expected_soname="libchronon3d_c.so.${abi_major}"

# ── Verdict ────────────────────────────────────────────────────────────────
rc=0
if [[ ${#removed[@]} -gt 0 ]]; then
    log "ABI2 symbols REMOVED (breaking):"
    printf '    %s\n' "${removed[@]}" >&2
    rc=1
fi
if [[ ${#type_changed[@]} -gt 0 ]]; then
    log "ABI2 symbols with incompatible type (signature proxy, expected T):"
    printf '    %s\n' "${type_changed[@]}" >&2
    rc=1
fi
if [[ "$soname" != "$expected_soname" ]]; then
    log "SONAME mismatch: got $soname, expected $expected_soname (SOVERSION must equal ABI major)"
    rc=1
fi

if [[ ${#new_syms[@]} -gt 0 ]]; then
    log "new additive chronon_* symbols (OK, non-breaking):"
    printf '    %s\n' "${new_syms[@]}" >&2
fi

baseline_count="$(printf '%s\n' "$baseline_syms" | wc -l | tr -d ' ')"
if [[ "$rc" -ne 0 ]]; then
    echo "GATE_FAIL: C ABI v2 contract broken (see [check_cabi_abi_gate] diagnostics above)" >&2
    exit 1
fi

echo "GATE_PASS: all $baseline_count ABI2 symbols intact (types unchanged), SONAME=$soname"
echo "[INFO] check_cabi_abi_gate: $baseline_count baseline symbols verified (${#new_syms[@]} additive, 0 removed, 0 type-changed)"
exit 0
