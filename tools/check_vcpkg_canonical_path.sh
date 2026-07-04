#!/usr/bin/env bash
# ============================================================================
# tools/check_vcpkg_canonical_path.sh
#
# Regression check for TICKET-UNIFIED-VCPKG-TOOLCHAIN.
# Enforces the SINGLE CANONICAL VCPKG TOOLCHAIN invariant across every
# cmake/presets/*.json: the only `CMAKE_TOOLCHAIN_FILE` value any preset may
# reference is the wrapper at `cmake/Chronon3DVcpkgToolchain.cmake`, and the
# only vcpkg-related paths any preset may appear with are inherited from that
# wrapper through clear cache vars (VCPKG_INSTALLED_DIR + VCPKG_TARGET_TRIPLET).
#
# This is the quartet rule for cmake/presets/{base,release,ci,profiling,
# linux-fast-dev,development}.json AND the top-level CMakePresets.json.
#
# Implements 4 invariants:
#   I1. Every CMAKE_TOOLCHAIN_FILE in any preset JSON == canonical wrapper
#       (single value, never direct vcpkg.cmake).
#   I2. No preset JSON hardcodes vcpkg_bootstrap/... or vcpkg_installed/...
#       outside the canonical wrapper reference.
#   I3. The wrapper declares CHRONON3D_VCPKG_TOOLCHAIN_FILE cache variable
#       AND references vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake.
#   I4. No preset sets CMAKE_PREFIX_PATH (consolidated into the wrapper).
#
# JSON parsing uses python3 stdlib (consistent with other tools/ python
# helpers like check_backend_sanitization.py).  bash + awk + sed cannot
# safely traverse nested CMakePresets JSON; jq is intentionally avoided
# because it is NOT in the vcpkg_bootstrap sandbox.  python3 IS shipped
# with every CI runner and is already a dependency of the existing
# tools/check_backend_sanitization.py.
#
# Usage:
#   tools/check_vcpkg_canonical_path.sh [--wip] [--strict]
#
# Exit codes:
#   0  Invariants hold (PASS).
#   1  At least one FAIL condition detected (CI blocks push on this).
#   2  Bad flag / misuse.
#
# --wip    log FAIL/WARN but always exit 0.
# --strict default: any FAIL exits 1 (CI mode).
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

wip=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --wip)    wip=1; shift ;;
    --strict) shift ;;
    -*)       echo "Unknown flag: $1" >&2; exit 2 ;;
    *)        echo "Unknown arg: $1" >&2; exit 2 ;;
  esac
done

CANONICAL_WRAPPER="cmake/Chronon3DVcpkgToolchain.cmake"

fails=0
ok()   { echo "[ OK ] $1"; }
fail() { echo "[FAIL] $1" >&2; fails=$((fails + 1)); }
warn() { if [[ "$wip" -eq 0 ]]; then echo "[WARN] $1" >&2; fi; }

# Discover every CMakePresets JSON. Auto-glob (rather than enumerate the
# six canonical files) so a future `cmake/presets/foo.json` is covered.
preset_files=()
[[ -f CMakePresets.json ]] && preset_files+=( "CMakePresets.json" )
while IFS= read -r f; do
  [[ -n "$f" ]] && preset_files+=( "$f" )
done < <(find cmake/presets -maxdepth 1 -name '*.json' -type f 2>/dev/null | sort)

[[ ${#preset_files[@]} -eq 0 ]] && {
  fail "no CMakePresets JSON files discovered (look for cmake/presets/*.json or top-level CMakePresets.json)"
  exit 1
}

echo "Scanning ${#preset_files[@]} CMakePresets JSON file(s) for canonical vcpkg toolchain invariants..."
echo

# Collect structured records (preset_file, json_path, key, value) for every
# vcpkg-related cacheVariable / toolchainFile we care about. python3 stdlib
# is used because bash+awk is too brittle for nested CMakePresets JSON.
RECORDS_TSV="$(mktemp -t check_vcpkg_records.XXXXXX.tsv)"
trap 'rm -f "$RECORDS_TSV"' EXIT

python3 - "${preset_files[@]}" "$CANONICAL_WRAPPER" >"$RECORDS_TSV" <<'PYEOF'
import json, sys

# Keys whose values are vcpkg-related for the structural invariant.
VCPKG_KEYS = {
    'CMAKE_TOOLCHAIN_FILE',  # canonical toolchain chain
    'CMAKE_PREFIX_PATH',     # must be empty across presets (wrapper owns it)
}

def walk(node, path, pf, out):
    if isinstance(node, dict):
        cv = node.get('cacheVariables')
        if isinstance(cv, dict):
            for k, v in cv.items():
                if k in VCPKG_KEYS:
                    value = v if isinstance(v, str) else json.dumps(v)
                    out.append((pf, path, k, value))
        # CMakePresets v6+ also supports top-level `toolchainFile` per preset.
        if path == '' and 'toolchainFile' in node and isinstance(node['toolchainFile'], str):
            out.append((pf, path, 'topLevel.toolchainFile', node['toolchainFile']))
        for k, v in node.items():
            walk(v, path + ("." + k if path else k), pf, out)
    elif isinstance(node, list):
        for i, item in enumerate(node):
            walk(item, path + f"[{i}]", pf, out)

records = []
for pf in sys.argv[1:-1]:
    with open(pf) as fh:
        try:
            data = json.load(fh)
        except Exception as e:
            print(f"[ERROR] {pf}: invalid JSON ({e})", file=sys.stderr)
            sys.exit(2)
    walk(data, '', pf, records)

# TSV: file\tjson_path\tkey\tvalue  (key uniquely identifies the record kind)
for r in records:
    print('\t'.join(r))
PYEOF

# --------------------------------------------------------------------------
# I1: every CMAKE_TOOLCHAIN_FILE in any preset == the canonical wrapper path
#     (the wrappers itself, or its ${sourceDir}/<wrapper> prefix).
# --------------------------------------------------------------------------
echo "[I1] CMAKE_TOOLCHAIN_FILE references must be the canonical wrapper"
echo "     canonical = $CANONICAL_WRAPPER"
echo

# Filter on non-empty lines (a missing-key yields no rows).
mapfile -t toolchain_rows < <(
  awk -F'\t' '$3 == "CMAKE_TOOLCHAIN_FILE" { print $4 }' "$RECORDS_TSV" || true
)
# Same logic for top-level toolchainFile (rare but valid).
mapfile -t top_level_toolchain_rows < <(
  awk -F'\t' '$3 == "topLevel.toolchainFile" { print $4 }' "$RECORDS_TSV" || true
)

# De-dup distinct values.
distinct_toolchain="$(printf '%s\n' "${toolchain_rows[@]:-}" "${top_level_toolchain_rows[@]:-}" | sort -u | sed '/^$/d')"
distinct_count=$(printf '%s\n' "$distinct_toolchain" | grep -c . || echo 0)
# Strip the count-string to a clean integer.
distinct_count="${distinct_count:-0}"

suffix_marker="/${CANONICAL_WRAPPER}"

if [[ "$distinct_count" -eq 0 ]]; then
  warn "[I1] no CMAKE_TOOLCHAIN_FILE entries found in any preset"
elif [[ "$distinct_count" -eq 1 ]]; then
  single="$distinct_toolchain"
  if [[ "$single" == *"$suffix_marker" || "$single" == "$CANONICAL_WRAPPER" ]]; then
    ok "[I1] all presets share the single canonical wrapper: $single"
  else
    fail "[I1] CMAKE_TOOLCHAIN_FILE is not the canonical wrapper: $single"
  fi
else
  fail "[I1] multiple distinct CMAKE_TOOLCHAIN_FILE values across presets (must be exactly 1):"
  printf '%s\n' "$distinct_toolchain" | sed 's/^/         /'
fi
echo

# --------------------------------------------------------------------------
# I2: no preset JSON hardcodes vcpkg_bootstrap/ or vcpkg_installed/…
#     in a TOOLCHAIN-related key (CMAKE_TOOLCHAIN_FILE, topLevel.toolchainFile,
#     CMAKE_PREFIX_PATH). Other legitimate vcpkg-typed cache vars such as
#     VCPKG_INSTALLED_DIR + VCPKG_TARGET_TRIPLET + VCPKG_MANIFEST_* are
#     ALLOWED — they are the vcpkg control vars that vcpkg.cmake consumes.
# --------------------------------------------------------------------------
echo "[I2] no preset may hardcode vcpkg_bootstrap/ or vcpkg_installed/ in TOOLCHAIN-related keys"

violations=0
while IFS=$'\t' read -r pf path key value; do
  case "$key" in
    CMAKE_TOOLCHAIN_FILE|topLevel.toolchainFile|CMAKE_PREFIX_PATH) ;;  # targets to check
    *) continue ;;
  esac
  # The canonical wrapper itself legitimately contains the substring.
  [[ "$value" == *"$CANONICAL_WRAPPER"* ]] && continue
  # Anything else mentioning vcpkg_bootstrap/ or vcpkg_installed/ is a violation.
  if [[ "$value" == *vcpkg_bootstrap* || "$value" == *vcpkg_installed* ]]; then
    fail "[I2] $pf($path).$key hardcodes a vcpkg_bootstrap/ or vcpkg_installed/ path:"
    printf '         %s\n' "$value"
    violations=$((violations + 1))
  fi
done < "$RECORDS_TSV"
if [[ "$violations" -eq 0 ]]; then
  ok "[I2] no preset hardcodes a vcpkg_bootstrap/ or vcpkg_installed/ path in TOOLCHAIN keys"
fi
echo

# --------------------------------------------------------------------------
# I3: the wrapper declares CHRONON3D_VCPKG_TOOLCHAIN_FILE cache var AND
#     references vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake.
# --------------------------------------------------------------------------
echo "[I3] $CANONICAL_WRAPPER must declare CHRONON3D_VCPKG_TOOLCHAIN_FILE and reach vcpkg.cmake"
if [[ ! -f "$CANONICAL_WRAPPER" ]]; then
  fail "[I3] canonical wrapper does not exist on disk: $CANONICAL_WRAPPER"
else
  if grep -q 'CHRONON3D_VCPKG_TOOLCHAIN_FILE' "$CANONICAL_WRAPPER"; then
    ok "[I3] wrapper declares CHRONON3D_VCPKG_TOOLCHAIN_FILE"
  else
    fail "[I3] wrapper does NOT declare CHRONON3D_VCPKG_TOOLCHAIN_FILE"
  fi
  if grep -q 'vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake' "$CANONICAL_WRAPPER"; then
    ok "[I3] wrapper references vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake as canonical"
  else
    fail "[I3] wrapper does NOT reference vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake"
  fi
fi
echo

# --------------------------------------------------------------------------
# I4: every preset's CMAKE_PREFIX_PATH, if present, MUST be empty
#     (we removed all duplicates — wrapper owns all CMAKE_PREFIX_PATH).
# --------------------------------------------------------------------------
echo "[I4] no preset sets CMAKE_PREFIX_PATH (the wrapper owns it)"
prefix_rows="$(awk -F'\t' '$3 == "CMAKE_PREFIX_PATH" { print $1 "\t" $4 }' "$RECORDS_TSV" || true)"
if [[ -z "$(printf '%s' "$prefix_rows" | tr -d '[:space:]')" ]]; then
  ok "[I4] no preset sets CMAKE_PREFIX_PATH (consolidated into $CANONICAL_WRAPPER)"
else
  fail "[I4] one or more presets still sets CMAKE_PREFIX_PATH (consolidate into wrapper):"
  printf '%s\n' "$prefix_rows" | sed 's/^/         /'
fi
echo

# --------------------------------------------------------------------------
# Final verdict
# --------------------------------------------------------------------------
echo
if [[ "$fails" -ne 0 ]]; then
  echo "FAIL: $fails violation(s) of the canonical vcpkg toolchain invariants"
  if [[ "$wip" -eq 1 ]]; then echo "(wip mode: ignoring failure)"; exit 0; fi
  exit 1
fi

echo "OK: vcpkg toolchain path is unified across all CMakePresets"
echo "     wrapper: $CANONICAL_WRAPPER"
exit 0
