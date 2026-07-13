#!/usr/bin/env bash
# ════════════════════════════════════════════════════════════════════════════
# tools/check_perf_regression.sh — F1.6 perf-regression gate
# (TICKET-PERF-GATE-V1)
# ════════════════════════════════════════════════════════════════════════════
#
# Compares a NEW bench.v3 JSON report against a baseline; emits
# GATE_PASS / GATE_FAIL / GATE_BLOCKED per AGENTS.md §honesty convention.
#
# User spec verbatim:
#   FAIL if median       > p50_baseline * 1.03
#        or p95          > p95_baseline * 1.05
#        or peak_rss     > baseline      * 1.05
#        or full_frame_copies            increase
#        or allocations_per_frame        increase
#        or output_hash changed (UNLESS --allow-golden-change).
#   For measures close to threshold: rerun 10-20 times + Mann-Whitney U test
#   (stdlib python3 via tools/lib_perf_regression.py — no scipy).
#
# Exit codes (canonical 3-state gate envelope):
#   0 = GATE_PASS
#   1 = GATE_FAIL
#   2 = GATE_BLOCKED  (env missing / binary missing / thresholds absent)
#
# Environment:
#   PERF_GATE=enabled                            gate is opt-in by env var
#   CHRONON3D_PERF_BASELINE=<path>               baseline JSON (default below)
#
# Args:
#   --current <path>                  path to current bench.v3 JSON (REQUIRED)
#   --baseline <path>                 path to baseline bench.v3 JSON
#   --retries <N>                     retry attempts (default 10-20 from YAML)
#   --allow-golden-change             bypass output_hash diff
#   --help|-h                         print this header
#
# Parallel precedent: tools/verify_performance_linux.sh ([INFO]/[WARN]/
# GATE_FAIL emit style); tools/check_manual_touches_per_video.sh
# (3-exit-codes envelope + threshold-driven GATE_FAIL).
# ════════════════════════════════════════════════════════════════════════════

set -uo pipefail

GATE_NAME="check_perf_regression"
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || true)"
SCRIPT_DIR="${REPO_ROOT}/tools"
LIB_HELPER="${SCRIPT_DIR}/lib_perf_regression.py"
THRESHOLDS_YAML="${REPO_ROOT}/configs/touchpoint_thresholds.yaml"

# Defaults
CURRENT_PATH=""
BASELINE_PATH="${CHRONON3D_PERF_BASELINE:-${REPO_ROOT}/bench/baselines/main-HEAD-perf.json}"
RETRIES=10
ALLOW_GOLDEN="false"

# ── Arg parsing ─────────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --current)              CURRENT_PATH="$2"; shift 2 ;;
        --baseline)             BASELINE_PATH="$2"; shift 2 ;;
        --retries)              RETRIES="$2"; shift 2 ;;
        --allow-golden-change)  ALLOW_GOLDEN="true"; shift ;;
        --help|-h) sed -n '2,33p' "$0"; exit 0 ;;
        *) echo "[ERROR] $(basename "$0"): unknown arg: $1" >&2; exit 2 ;;
    esac
done

# Required arg check
if [[ -z "$CURRENT_PATH" ]]; then
    echo "[ERROR] $(basename "$0"): --current <path> is REQUIRED" >&2
    echo "  hint: pass the current run's bench.v3 report (chronon3d_cli bench <scene> --json-file <path>)" >&2
    exit 2
fi

# ── GATE_FORMAT — canonical AGENTS.md §Lint-checkability diagnostic ──────
_info() { echo "[INFO] $(basename "$0" .sh): $*"; }
_warn() { echo "[WARN] $(basename "$0" .sh): $*"; }
_err()  { echo "[ERROR] $(basename "$0" .sh): $*"; }
_pass() { echo "  [PASS]    $*"; }
_fail() { echo "  [FAIL]    $*"; }
_blocked() { echo "  [BLOCKED] $*"; }

# ── env-block tests ───────────────────────────────────────────────────────────
if [[ -z "${CHRONON3D_CLI:-}" ]] && [[ ! -x "${CHRONON3D_CLI:-build/manual-test/chronon3d_cli}" ]]; then
    _info "binary missing: chronon3d_cli not in PATH; bench v3 entries verification IS env-blocked"
    _info "  tip: macchina-verifica DEFERRED-WBH per AGENTS.md §honest-limitation"
fi

# ── Load helpers / thresholds ─────────────────────────────────────────────
if [[ ! -r "$LIB_HELPER" ]]; then
    _err "missing lib helper: $LIB_HELPER"
    _err "  fix: verify tools/lib_perf_regression.py is tracked (git ls-files)"
    exit 2
fi

# Extract perf_regression_gate block from YAML using python3 (stdlib yaml not
# always available; we use a minimal block-detection via awk + python json).
thresholds_block="$(python3 - <<PYEOF
import json, sys, os
try:
    import yaml  # noqa: F401
    has_yaml = True
except ImportError:
    has_yaml = False
yaml_path = os.environ.get("THRESHOLDS_YAML", "$THRESHOLDS_YAML")
if not has_yaml or not os.path.isfile(yaml_path):
    # Fallback to default thresholds (the README values).
    out = {
        "median_pct": 1.03,
        "p95_pct": 1.05,
        "peak_rss_pct": 1.05,
        "close_call_band_pct": 0.20,
        "mann_whitney_alpha": 0.05,
        "retry_attempts_min": 10,
        "retry_attempts_max": 20,
    }
    print(json.dumps(out))
    sys.exit(0)
with open(yaml_path, "r", encoding="utf-8") as fh:
    doc = yaml.safe_load(fh)
block = doc.get("perf_regression_gate", {}) if isinstance(doc, dict) else {}
defaults = {
    "median_pct": 1.03,
    "p95_pct": 1.05,
    "peak_rss_pct": 1.05,
    "close_call_band_pct": 0.20,
    "mann_whitney_alpha": 0.05,
    "retry_attempts_min": 10,
    "retry_attempts_max": 20,
}
defaults.update({
    k: block.get(k, v)
    for k, v in defaults.items()
})
print(json.dumps(defaults))
PYEOF
)" || {
    _err "failed to parse thresholds YAML"
    exit 2
}

median_pct="$(python3 -c "import json,sys; print(json.loads(sys.argv[1])['median_pct'])" "$thresholds_block")"
p95_pct="$(python3    -c "import json,sys; print(json.loads(sys.argv[1])['p95_pct'])"     "$thresholds_block")"
peak_rss_pct="$(python3 -c "import json,sys; print(json.loads(sys.argv[1])['peak_rss_pct'])" "$thresholds_block")"
close_band="$(python3 -c "import json,sys; print(json.loads(sys.argv[1])['close_call_band_pct'])" "$thresholds_block")"
alpha="$(python3 -c "import json,sys; print(json.loads(sys.argv[1])['mann_whitney_alpha'])" "$thresholds_block")"
retry_min="$(python3 -c "import json,sys; print(json.loads(sys.argv[1])['retry_attempts_min'])" "$thresholds_block")"
retry_max="$(python3 -c "import json,sys; print(json.loads(sys.argv[1])['retry_attempts_max'])" "$thresholds_block")"

# Cap RETRIES between retry_min and retry_max (default = retry_min; spec says 10-20)
if [[ "$RETRIES" -lt "$retry_min" ]]; then RETRIES="$retry_min"; fi
if [[ "$RETRIES" -gt "$retry_max" ]]; then RETRIES="$retry_max"; fi

_info "thresholds: median_pct=$median_pct p95_pct=$p95_pct peak_rss_pct=$peak_rss_pct close_band=$close_band alpha=$alpha retries=$RETRIES allow_golden=$ALLOW_GOLDEN"

# ── Stage 1: validate current + baseline exist ────────────────────────────
if [[ ! -s "$CURRENT_PATH" ]]; then
    _err "current bench.v3 file missing or empty: $CURRENT_PATH"
    _err "  hint: run `chronon3d_cli bench <scene> --json-file <path>` first"
    exit 2
fi
if [[ ! -s "$BASELINE_PATH" ]]; then
    _err "baseline bench.v3 file missing or empty: $BASELINE_PATH"
    _err "  macchina-verifica DEFERRED-WBH per AGENTS.md §honest-limitation"
    exit 2
fi

# ── Stage 2: compare-to-baseline (numeric pass / close-call / fail labels)──
comparison_json="$(python3 - <<PYEOF
import json, sys
sys.path.insert(0, "$SCRIPT_DIR")
import lib_perf_regression as lib
thresholds = json.loads("$thresholds_block")
current  = lib.parse_bench("$CURRENT_PATH")
baseline = lib.parse_bench("$BASELINE_PATH")
result = lib.compare_to_baseline(current, baseline, thresholds, float("$alpha"))
print(json.dumps(result, sort_keys=True))
PYEOF
)" || {
    _err "failed during compare_to_baseline (python3 or lib helper crash)"
    exit 2
}

# Reproducible diagnostic per metric
echo "$comparison_json" | python3 -c "
import json, sys
d = json.load(sys.stdin)
for v in d.get('verdicts', []):
    metric = v.get('metric', '?')
    status = v.get('status', '?')
    b = v.get('baseline')
    c = v.get('current')
    if metric == 'output_hash':
        print(f'  [{status.upper()}] {metric}  baseline={b}  current={c}')
    elif 'ratio' in v:
        print(f'  [{status.upper()}] {metric}  baseline={b}  current={c}  ratio={v[\"ratio\"]:.4f}  threshold={v[\"threshold\"]}')
    else:
        print(f'  [{status.upper()}] {metric}  baseline={b}  current={c}  delta={v.get(\"delta\", \"?\")}')
print(f'\\n  summary: passes={d[\"summary\"][\"passes\"]}  close_calls={d[\"summary\"][\"close_calls\"]}  hard_fails={d[\"summary\"][\"hard_fails\"]}')
" || true

close_count="$(echo "$comparison_json" | python3 -c 'import json, sys; print(json.load(sys.stdin)["summary"]["close_calls"])')"
hard_count="$(echo "$comparison_json" | python3 -c 'import json, sys; print(json.load(sys.stdin)["summary"]["hard_fails"])')"
pass_count="$(echo "$comparison_json" | python3 -c 'import json, sys; print(json.load(sys.stdin)["summary"]["passes"])')"

# ── Stage 3: apply allow_golden_change bypass for hash-only failures ─────
hard_count_after_bypass="$hard_count"
if [[ "$ALLOW_GOLDEN" == "true" ]]; then
    # Re-run decide() with bypass flag — reduces hard_count by 1 only if the
    # sole hard-fail was output_hash.
    hard_count_after_bypass="$(echo "$comparison_json" | python3 - "$alpha" <<PYEOF
import json, sys
d = json.load(sys.stdin)
hard = d['summary']['hard_fails']
for v in d.get('verdicts', []):
    if v.get('metric') == 'output_hash' and v.get('status') == 'fail':
        # Allow bypass
        hard -= 1
        break
print(hard)
PYEOF
)"
fi

# ── Stage 4: hard-fail fast path ─────────────────────────────────────────
if [[ "$hard_count_after_bypass" -gt 0 ]]; then
    _fail "perf-regression gate: $hard_count_after_bypass hard-fail metric(s) (threshold breach)"
    _fail "see diagnostic table above for offending metric(s) + baseline/current/ratio"
    _fail "fix-forward paths: (a) src-side fix the regressed code path; (b) regenerate baseline (deferred-WBH); (c) widen threshold in configs/touchpoint_thresholds.yaml::perf_regression_gate (ADR)."
    echo "GATE_FAIL"
    exit 1
fi

# ── Stage 5: close-call resolver via Mann-Whitney burst-reruns ────────────
# Per user spec "Per misure vicine alla soglia: riesegui 10-20 volte e usa
# mediane con test Mann-Whitney semplice".  We'll burst-rerun *up to* RETRIES
# attempts IF a chronon3d_cli binary is present (real re-runs); otherwise we
# emit BLOCKED with the close_call metrics identified (since we can't actually
# rerun the bench on this VPS without the binary + benchmarks enabled).
if [[ "$close_count" -gt 0 ]]; then
    # We don't have a real rerun loop on this VPS unless chronon3d_cli is
    # available; surface the close-call IDs and let the gate verdict depend
    # on whether the binary is present.
    if command -v "${CHRONON3D_CLI:-build/manual-test/chronon3d_cli}" > /dev/null 2>&1; then
        _info "close-call metrics found: $close_count — Mann-Whitney burst-rerun $RETRIES times..."
        _info "  (placeholder: full burst-rerun is forward-pointed to TICKET-PERF-GATE-V1-WBH-MACHINE-VERIFY)"
        _info "  this VPS is env-blocked per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV; gate IS env-blocked."
    fi
    _warn "close-call resolver NOT executed on this VPS — DEFERRED-WBH per AGENTS.md §honest-limitation"
    _warn "  metrics: $(echo "$comparison_json" | python3 -c 'import json,sys; print(",".join(v[\"metric\"] for v in json.load(sys.stdin)[\"close_calls\"]))')"
    _info "GATE_BLOCKED: close-call metrics require WBH burst-rerun to resolve"
    echo "GATE_BLOCKED"
    exit 2
fi

# ── Stage 6: all-cleared verdict ─────────────────────────────────────────
_info "perf-regression gate cleared: $pass_count pass / 0 close-call / 0 hard-fail"
_info "  metrics evaluated: $(echo "$comparison_json" | python3 -c 'import json,sys; print(",".join(v[\"metric\"] for v in json.load(sys.stdin)[\"verdicts\"]))')"
echo "GATE_PASS"
exit 0
