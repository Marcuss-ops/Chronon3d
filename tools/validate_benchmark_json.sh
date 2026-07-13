#!/usr/bin/env bash
# ════════════════════════════════════════════════════════════════════════════
# tools/validate_benchmark_json.sh — F1.2 benchmark JSON schema validator
# (TICKET-BENCH-SCHEMA-V1)
# ════════════════════════════════════════════════════════════════════════════
#
# Validates a flat benchmark JSON report against `bench/benchmark_schema.json`
# (user-facing canonical).  The validator's required-field list is the
# schema's `required` array — single source of truth (Cat-3 anti-dup: no
# separate list inside the validator).  Adding a new required field to the
# schema auto-extends the validator's check.
#
# Exit codes (canonical 3-state gate envelope per AGENTS.md §honesty):
#   0 = GATE_PASS  (all required fields present + correct types)
#   1 = GATE_FAIL  (missing field OR type mismatch OR unknown field)
#   2 = GATE_BLOCKED  (file missing, unreadable, or not valid JSON)
#
# Args:
#   --report <path>                  path to flat benchmark JSON (REQUIRED)
#   --schema <path>                  path to schema (default: bench/benchmark_schema.json)
#   --allow-unknown-fields           permit fields not in the schema (default: REJECT)
#   --help|-h                        print this header
#
# Dependencies: bash, python3 (stdlib only — no jsonschema, no PyYAML).
# ════════════════════════════════════════════════════════════════════════════

set -uo pipefail

GATE_NAME="validate_benchmark_json"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Canonical REPO_ROOT pattern (mirrors tools/benchmark_host_info.sh).  The
# `$(git rev-parse ... || cd ... && pwd)` form is a bash pitfall: when
# `git rev-parse` succeeds, the `||` short-circuits but the `&& pwd` after
# the `||` chain can still be evaluated, causing the path to be doubled.
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEFAULT_SCHEMA="${REPO_ROOT}/bench/benchmark_schema.json"

REPORT_PATH=""
SCHEMA_PATH="$DEFAULT_SCHEMA"
ALLOW_UNKNOWN="false"

# ── Arg parsing ─────────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --report)                 REPORT_PATH="$2"; shift 2 ;;
        --schema)                 SCHEMA_PATH="$2"; shift 2 ;;
        --allow-unknown-fields)   ALLOW_UNKNOWN="true"; shift ;;
        --help|-h)                sed -n '2,28p' "$0"; exit 0 ;;
        *) echo "[ERROR] $(basename "$0"): unknown arg: $1" >&2; exit 2 ;;
    esac
done

# Required arg check
if [[ -z "$REPORT_PATH" ]]; then
    echo "[ERROR] $(basename "$0"): --report <path> is REQUIRED" >&2
    echo "  hint: pass the flat benchmark JSON produced by `bench/run_perf_bench.sh`" >&2
    exit 2
fi

# ── GATE_FORMAT — canonical AGENTS.md §Lint-checkability diagnostic ──────
_info()    { echo "[INFO] $(basename "$0" .sh): $*"; }
_warn()    { echo "[WARN] $(basename "$0" .sh): $*"; }
_err()     { echo "[ERROR] $(basename "$0" .sh): $*"; }
_passln() { echo "  [PASS]    $*"; }
_failln() { echo "  [FAIL]    $*"; }
_blockln() { echo "  [BLOCKED] $*"; }

# ── Stage 1: file readability ─────────────────────────────────────────────
if [[ ! -r "$REPORT_PATH" ]]; then
    _err "report file missing or unreadable: $REPORT_PATH"
    _err "  hint: pass the flat benchmark JSON produced by `bench/run_perf_bench.sh`"
    exit 2
fi
if [[ ! -r "$SCHEMA_PATH" ]]; then
    _err "schema file missing or unreadable: $SCHEMA_PATH"
    _err "  fix: verify bench/benchmark_schema.json is tracked (git ls-files)"
    exit 2
fi

# ── Stage 2: validate via python3 stdlib (no jsonschema dep) ─────────────
# The validator reads the schema's `required` array as SSoT + does per-field
# type + min/max + pattern checks against the schema's `properties` block.
# This means: adding a new required field to the schema auto-extends the
# validator's check (Cat-3 anti-dup: zero duplication between schema & script).
validation_json="$(python3 - "$SCHEMA_PATH" "$REPORT_PATH" "$ALLOW_UNKNOWN" <<'PYEOF'
import json
import re
import sys

schema_path, report_path, allow_unknown = sys.argv[1], sys.argv[2], sys.argv[3]
allow_unknown = (allow_unknown.lower() == "true")

try:
    with open(schema_path, "r", encoding="utf-8") as fh:
        schema = json.load(fh)
except (OSError, json.JSONDecodeError) as e:
    print(json.dumps({"verdict": "BLOCKED", "stage": "schema-load", "reason": str(e)}))
    sys.exit(0)  # exit 0 to let the bash wrapper emit the BLOCKED verdict

try:
    with open(report_path, "r", encoding="utf-8") as fh:
        report = json.load(fh)
except (OSError, json.JSONDecodeError) as e:
    print(json.dumps({"verdict": "BLOCKED", "stage": "report-load", "reason": str(e)}))
    sys.exit(0)

if not isinstance(report, dict):
    print(json.dumps({"verdict": "FAIL", "stage": "report-shape",
                      "reason": f"report must be a JSON object, got {type(report).__name__}"}))
    sys.exit(0)

required = schema.get("required", []) or []
properties = schema.get("properties", {}) or {}

issues = []  # list of {stage, field, reason}

# Missing required fields.
present = set(report.keys())
for f in required:
    if f not in present:
        issues.append({"stage": "missing-required", "field": f,
                       "reason": f"required field '{f}' absent from report"})

# Unknown fields (Cat-3 sealed: additionalProperties: false).
if not allow_unknown:
    allowed = set(properties.keys())
    for k in present:
        if k not in allowed:
            issues.append({"stage": "unknown-field", "field": k,
                           "reason": f"field '{k}' not declared in schema "
                                     f"(use --allow-unknown-fields to bypass)"})

# Per-field type + min/max + pattern checks (against schema.properties).
def _check_type(field_name, expected_type, value):
    if expected_type == "string":
        return isinstance(value, str)
    if expected_type == "integer":
        # bool is a subclass of int in Python — reject it explicitly.
        return isinstance(value, int) and not isinstance(value, bool)
    if expected_type == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if expected_type == "boolean":
        return isinstance(value, bool)
    if expected_type == "array":
        return isinstance(value, list)
    if expected_type == "object":
        return isinstance(value, dict)
    if expected_type == "null":
        return value is None
    return True  # unknown type: trust schema-level validation

for f, spec in properties.items():
    if f not in report:
        continue  # absence handled above
    v = report[f]
    t = spec.get("type")
    if t and not _check_type(f, t, v):
        issues.append({"stage": "type-mismatch", "field": f,
                       "reason": f"expected type '{t}', got {type(v).__name__}: {v!r}"})
        continue
    # Numeric bounds.
    if t in ("integer", "number") and isinstance(v, (int, float)) and not isinstance(v, bool):
        if "minimum" in spec and v < spec["minimum"]:
            issues.append({"stage": "value-out-of-range", "field": f,
                           "reason": f"value {v} < minimum {spec['minimum']}"})
        if "maximum" in spec and v > spec["maximum"]:
            issues.append({"stage": "value-out-of-range", "field": f,
                           "reason": f"value {v} > maximum {spec['maximum']}"})
    # String length + pattern.
    if t == "string" and isinstance(v, str):
        if "minLength" in spec and len(v) < spec["minLength"]:
            issues.append({"stage": "value-out-of-range", "field": f,
                           "reason": f"length {len(v)} < minLength {spec['minLength']}"})
        if "maxLength" in spec and len(v) > spec["maxLength"]:
            issues.append({"stage": "value-out-of-range", "field": f,
                           "reason": f"length {len(v)} > maxLength {spec['maxLength']}"})
        if "pattern" in spec and not re.match(spec["pattern"], v):
            issues.append({"stage": "pattern-mismatch", "field": f,
                           "reason": f"value {v!r} does not match pattern {spec['pattern']!r}"})

if issues:
    print(json.dumps({"verdict": "FAIL", "stage": "checks", "issues": issues,
                      "summary": {"required_total": len(required),
                                  "required_missing": sum(1 for i in issues
                                                          if i["stage"] == "missing-required"),
                                  "issues_total": len(issues)}}, sort_keys=True))
else:
    print(json.dumps({"verdict": "PASS", "stage": "checks",
                      "summary": {"required_total": len(required),
                                  "fields_checked": len(properties)}}, sort_keys=True))
PYEOF
)" || {
    _err "python3 validation helper crashed unexpectedly"
    exit 2
}

# ── Stage 3: render verdict + emit gate decision ─────────────────────────
verdict="$(echo "$validation_json" | python3 -c 'import json, sys; print(json.load(sys.stdin)["verdict"])')"

case "$verdict" in
    PASS)
        summary="$(echo "$validation_json" | python3 -c 'import json, sys; d = json.load(sys.stdin); print(d.get("summary", {}))')"
        _info "schema=$SCHEMA_PATH report=$REPORT_PATH (allow_unknown=$ALLOW_UNKNOWN)"
        _passln "all required fields present + type/bounds/pattern checks clear"
        _passln "summary: $summary"
        echo "GATE_PASS"
        exit 0
        ;;
    FAIL)
        _info "schema=$SCHEMA_PATH report=$REPORT_PATH (allow_unknown=$ALLOW_UNKNOWN)"
        # Emit one [FAIL] per issue (grep-discoverable).
        echo "$validation_json" | python3 -c "
import json, sys
d = json.load(sys.stdin)
for i in d.get('issues', []):
    print(f'  [FAIL] {i[\"stage\"]}  field={i.get(\"field\", \"?\")}  reason={i[\"reason\"]}')
print(f'  [FAIL] summary: required_total={d[\"summary\"][\"required_total\"]}  '
      f'required_missing={d[\"summary\"][\"required_missing\"]}  '
      f'issues_total={d[\"summary\"][\"issues_total\"]}')
"
        _failln "report does NOT match schema — see issues above"
        _failln "fix-forward: (a) regenerate report with the canonical fields; (b) extend schema with --allow-unknown-fields (ADR for breaking the sealed additionalProperties:false)."
        echo "GATE_FAIL"
        exit 1
        ;;
    BLOCKED)
        reason="$(echo "$validation_json" | python3 -c 'import json, sys; d = json.load(sys.stdin); print(d.get("reason", "unknown"))')"
        stage="$(echo "$validation_json" | python3 -c 'import json, sys; d = json.load(sys.stdin); print(d.get("stage", "unknown"))')"
        _err "schema=$SCHEMA_PATH report=$REPORT_PATH (allow_unknown=$ALLOW_UNKNOWN)"
        _blockln "stage=$stage reason=$reason"
        echo "GATE_BLOCKED"
        exit 2
        ;;
    *)
        _err "unrecognized verdict: $verdict"
        exit 2
        ;;
esac
