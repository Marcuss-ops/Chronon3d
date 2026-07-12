#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# check_fix_cronograph.sh — Test 11 (Fix Speed / Cronometro del fix) gate
# ═══════════════════════════════════════════════════════════════════════════
#
# Implements the canonical fail-on-target-breach gate for Test 11 of the
# First-Principles Product Check framework. Per the Test 11 spec verbatim:
#
#   "Test 11 — Cronometro del fix: ... Misura i 5 tempi: riproduzione /
#    root cause / scrittura test / fix / certificazione. Obiettivi:
#    riproduzione <30min, root cause <2h, fix+test <1gg. FAIL se il bug
#    genera 10 ticket + 4 adapter + nessuna correzione verificata."
#
# The gate reads docs/fix_cronograph_log.jsonl (append-only fix log; each
# line is one JSON entry per fixed bug) and computes rolling mean across the
# last `WINDOW` (default 5) entries for the two phase targets. FAILs on:
#
#   1. Threshold breach — mean_repro_m > 30 OR mean_rca_m > 120 over last
#      `WINDOW` entries (per spec target envelope)
#   2. Catastrophic-fix FAIL condition — the LAST fix entry has
#      `new_tickets >= 10 AND adapters >= 4 AND verified != "yes"`
#      (per spec "FAIL se il bug genera 10 ticket + 4 adapter +
#      nessuna correzione verificata")
#
# If the log file is missing (e.g. on first install), the gate is
# permissive (zero-data) — emits a [INFO] line and exits 0 to avoid
# blocking initial adoption. As soon as ≥1 entry exists, the targets
# apply per spec.
#
# Per AGENTS.md §"Regole di lint documentale" rule #2 (INFO-level
# diagnostic style): PASS path emits `GATE_PASS:` canonical first, then
# `[INFO] ${GATE_NAME}: ...` additive line ≤200 chars.
# FAIL path unchanged: GATE_FAIL on stderr + exit 1.
#
# Wired into:
#   - tools/wrap_push.sh                Step 4.5i (canonical pre-push chain)
#   - tools/first_principles_product_check.sh  == Fix speed == section
# ═══════════════════════════════════════════════════════════════════════════
set -uo pipefail

GATE_NAME=check_fix_cronograph

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
LOG_FILE="${REPO_ROOT}/docs/fix_cronograph_log.jsonl"

# Per-Test-11-spec verbatim targets
TARGET_REPRO_MIN=30        # <30min reproduction per spec
TARGET_RCA_MIN=120         # <2h root cause analysis per spec
TARGET_FIXTEST_MIN=1440    # <1gg (24h) fix+test per spec
WINDOW=5                   # rolling-mean window for threshold check

# ── Zero-data permissive path (no log → no data → gate PASS-conditionally) ──
if [ ! -f "$LOG_FILE" ]; then
    echo "[INFO] ${GATE_NAME}: no fix log at $LOG_FILE; gate is permissive (zero-data)"
    exit 0
fi

ENTRIES=$(wc -l < "$LOG_FILE" 2>/dev/null || echo 0)
if [ "$ENTRIES" -eq 0 ]; then
    echo "[INFO] ${GATE_NAME}: empty fix log at $LOG_FILE; gate is permissive (zero-data)"
    exit 0
fi

# ── Last entry parse (per-field extractors; tolerant of missing fields) ──
LAST_ENTRY=$(tail -n 1 "$LOG_FILE")
extract_field() {
    # $1: field name (e.g. repro_m). Returns 0 / "no" / 0 on missing.
    local field="$1"
    local val
    val=$(echo "$LAST_ENTRY" | grep -oE "\"${field}\":[ ]?[0-9]+\"" 2>/dev/null | grep -oE '[0-9]+' | head -n 1)
    if [ -z "$val" ]; then
        val=$(echo "$LAST_ENTRY" | grep -oE "\"${field}\":\"[a-zA-Z_]+\"" 2>/dev/null | sed -E 's/.*"([a-zA-Z_]+)".*/\1/' | head -n 1)
    fi
    echo "${val:-0}"
}
LAST_REPRO=$(extract_field "repro_m")
LAST_RCA=$(extract_field "rca_m")
LAST_TEST=$(extract_field "test_m")
LAST_FIX=$(extract_field "fix_m")
LAST_CERT=$(extract_field "cert_m")
LAST_TICKETS=$(extract_field "new_tickets")
LAST_ADAPTERS=$(extract_field "adapters")
LAST_VERIFIED=$(extract_field "verified")

# ── Rolling mean over last $WINDOW entries (phase-level arithmetic mean) ──
WINDOW_ENTRIES=$(tail -n "$WINDOW" "$LOG_FILE" 2>/dev/null || echo "")
if [ -z "$WINDOW_ENTRIES" ]; then
    MEAN_REPRO=0
    MEAN_RCA=0
else
    MEAN_REPRO=$(echo "$WINDOW_ENTRIES" | grep -oE '"repro_m":[0-9]+' | grep -oE '[0-9]+' \
        | awk 'BEGIN{s=0;n=0} {s+=$1;n+=1} END{if(n>0) print int(s/n); else print 0}')
    MEAN_RCA=$(echo "$WINDOW_ENTRIES" | grep -oE '"rca_m":[0-9]+' | grep -oE '[0-9]+' \
        | awk 'BEGIN{s=0;n=0} {s+=$1;n+=1} END{if(n>0) print int(s/n); else print 0}')
fi

# ── FAIL condition 1: catastrophic-fix envelope (spec FAIL verbatim) ───────
if [ "$LAST_TICKETS" -ge 10 ] && [ "$LAST_ADAPTERS" -ge 4 ] && [ "$LAST_VERIFIED" != "yes" ]; then
    echo "GATE_FAIL: catastrophic-fix envelope exceeded on last entry:" >&2
    echo "  new_tickets = ${LAST_TICKETS}  (threshold < 10 per spec)" >&2
    echo "  adapters    = ${LAST_ADAPTERS}  (threshold < 4 per spec)" >&2
    echo "  verified    = ${LAST_VERIFIED}  (must be 'yes' per spec)" >&2
    echo "  spec_verbatim: 'FAIL se il bug genera 10 ticket + 4 adapter + nessuna correzione verificata'" >&2
    echo "GATE_FAIL"
    exit 1
fi

# ── FAIL condition 2: rolling-mean threshold breach (spec target envelope) ─
BREACHES=""
if [ "$MEAN_REPRO" -gt "$TARGET_REPRO_MIN" ]; then
    BREACHES="${BREACHES} mean_repro_m=${MEAN_REPRO} > ${TARGET_REPRO_MIN}"
fi
if [ "$MEAN_RCA" -gt "$TARGET_RCA_MIN" ]; then
    BREACHES="${BREACHES} mean_rca_m=${MEAN_RCA} > ${TARGET_RCA_MIN}"
fi
# Spec verbatim target #3: "fix+test <1gg" → (test_m + fix_m) < 1440 min.
# Rolling mean over last WINDOW.
WINDOW_TESTFIX=$(echo "$WINDOW_ENTRIES" | grep -oE '"test_m":[0-9]+|"fix_m":[0-9]+' | grep -oE '[0-9]+' \
    | awk 'BEGIN{s=0;n=0} {if(n%2==0){last_test=$1} else{s+=last_test+$1;n+=1}} END{if(n>0) print int(s/n); else print 0}')
if [ -n "$WINDOW_TESTFIX" ] && [ "$WINDOW_TESTFIX" -gt "$TARGET_FIXTEST_MIN" ]; then
    BREACHES="${BREACHES} mean_(test+fix)_m=${WINDOW_TESTFIX} > ${TARGET_FIXTEST_MIN}"
fi
if [ -n "$BREACHES" ]; then
    echo "GATE_FAIL: target envelope breach in last ${WINDOW} entries:${BREACHES}" >&2
    echo "  spec targets: repro_m < ${TARGET_REPRO_MIN}, rca_m < ${TARGET_RCA_MIN}, (test+fix)_m < ${TARGET_FIXTEST_MIN}" >&2
    LAST_FIXTEST=$((LAST_TEST + LAST_FIX))
    echo "  last entry:   repro=${LAST_REPRO}m, rca=${LAST_RCA}m, test+fix=${LAST_FIXTEST}m, cert=${LAST_CERT}m, verified=${LAST_VERIFIED}" >&2
    echo "GATE_FAIL"
    exit 1
fi

# ── PASS path (canonical GATE_PASS + additive [INFO] per AGENTS.md Rule #2) ─
LAST_FIXTEST=$((LAST_TEST + LAST_FIX))
echo "GATE_PASS: ${ENTRIES} fix entries; last repro=${LAST_REPRO}m rca=${LAST_RCA}m test+fix=${LAST_FIXTEST}m cert=${LAST_CERT}m verified=${LAST_VERIFIED}"
FIXTEST_SUFFIX=""
if [ -n "$WINDOW_TESTFIX" ] && [ "$WINDOW_TESTFIX" -gt 0 ]; then
    FIXTEST_SUFFIX=" ${WINDOW_TESTFIX}m"
fi
echo "[INFO] ${GATE_NAME}: rolling mean (last ${WINDOW}) repro_m=${MEAN_REPRO} rca_m=${MEAN_RCA}${FIXTEST_SUFFIX} — within target envelopes (repro<${TARGET_REPRO_MIN}m, rca<${TARGET_RCA_MIN}m, fix+test<${TARGET_FIXTEST_MIN}m); zero-data forwarding when entries absent"
exit 0
