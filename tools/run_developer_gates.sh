#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# run_developer_gates.sh — canonical developer gate chain.
#
# Called by:
#   - tools/wrap_push.sh (post-fetch, post-auto-FF, post-check_main_clean)
#   - .githooks/pre-push     (git push pre-push hook)
#
# Single source of truth: the gate list lives in tools/gates/manifest.sh.
#
# 2026-09-26 slim (taglia senza pietà):
#   pre-push = 6 gate veloci (<90s wall, dominato da check_architecture.py).
#   Full     = 13 gate (6 veloci + 7 lenti/build/meta) — CI e `CHRONON3D_PRE_PUSH_FULL=1`.
#   Nessun build su ogni push. Nessun selftest del gate su ogni push.
#
# Scheduling: build gates (ninja) serializzati tra loro (RAM ~1.6 GiB/TU, swap
# se paralleli), tutti gli altri gate in parallelo.  Report in manifest order.
# Env/flag:
#   CHRONON3D_PRE_PUSH_FULL=1  → full (13)
#   --full / --slim             → forza full/slim (CLI ha precedenza su env)
#
# Usage: bash tools/run_developer_gates.sh [--full|--slim] [remote] [branch]
# ═══════════════════════════════════════════════════════════════════════════

set -uo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
SCRIPT_DIR="${REPO_ROOT}/tools"

FULL=0
# env default: slim. --full flag overrides env.
if [[ "${CHRONON3D_PRE_PUSH_FULL:-0}" == "1" ]]; then FULL=1; fi
if [[ "${1:-}" == "--full" ]]; then FULL=1; shift
elif [[ "${1:-}" == "--slim" ]]; then FULL=0; shift
fi

TARGET_REMOTE="${1:-origin}"
TARGET_BRANCH="${2:-$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo main)}"
COMPARISON_REF="${TARGET_REMOTE}/${TARGET_BRANCH}"

# Load the canonical gate manifest.
# shellcheck source=gates/manifest.sh
source "${SCRIPT_DIR}/gates/manifest.sh"

if (( FULL )) && declare -p FULL_DEVELOPER_GATES >/dev/null 2>&1; then
    DEVELOPER_GATES=("${FULL_DEVELOPER_GATES[@]}")
    MODE="full"
else
    MODE="slim"
fi

GATE_COUNT=${#DEVELOPER_GATES[@]}
if (( GATE_COUNT == 0 )); then
    echo "run_developer_gates.sh: GATE_FAIL — empty gate set (manifest misconfigured)" >&2
    exit 2
fi

# Gates that drive a build: always serialised (see header).
BUILD_GATES=(
    check_cli_tests.sh
    verify_registry_render_graph_cache.sh
)

is_build_gate() {
    local candidate
    for candidate in "${BUILD_GATES[@]}"; do
        [[ "$1" == "$candidate" ]] && return 0
    done
    return 1
}

WALL_START="$(date +%s)"
echo "run_developer_gates.sh: starting developer gate chain (${GATE_COUNT} gates, mode=${MODE}, ref=${COMPARISON_REF})..."
if [[ "$MODE" == "slim" ]]; then
    echo "  slim = 6 gate veloci (arch + 5 grep). Full: CHRONON3D_PRE_PUSH_FULL=1 bash tools/run_developer_gates.sh --full"
fi

WORK="$(mktemp -d -t chronon-gates.XXXXXX)"
trap 'rm -rf "${WORK}"' EXIT
FAILED_GATES="${WORK}/failed-gates"
: > "${FAILED_GATES}"

# per-gate timing
declare -A START_TS
declare -A ELAPSED

run_gate() {
    local gate="$1" log="$2"
    local rc=0
    START_TS["$gate"]="$(date +%s)"
    bash "${SCRIPT_DIR}/execute_gate.sh" "$gate" "$TARGET_REMOTE" "$TARGET_BRANCH" >"${log}" 2>&1 || rc=$?
    ELAPSED["$gate"]=$(( $(date +%s) - START_TS["$gate"] ))
    if (( rc != 0 )); then
        printf '%s\t%s\t%s\n' "${rc}" "${gate}" "${ELAPSED[$gate]}" >> "${FAILED_GATES}"
    else
        # some gates print GATE_FAIL but exit 0 in WARN mode — treat as fail
        if grep -q "GATE_FAIL" "${log}" 2>/dev/null; then
            printf '%s\t%s\t%s\n' "1" "${gate}" "${ELAPSED[$gate]}" >> "${FAILED_GATES}"
        fi
    fi
    return 0
}

# ── Build lane: serialised ───────────────────────────────────────────────
BUILD_PIDS=()
BUILD_LOGS=()
has_build=0
for g in "${DEVELOPER_GATES[@]}"; do is_build_gate "$g" && { has_build=1; break; }; done

BUILD_LANE_PID=""
if (( has_build )); then
    (
        for gate in "${DEVELOPER_GATES[@]}"; do
            if is_build_gate "${gate}"; then
                log="${WORK}/gate-${gate//\//_}.log"
                echo "  [build] ${gate} — running..."
                run_gate "${gate}" "${log}"
            fi
        done
    ) &
    BUILD_LANE_PID=$!
else
    echo "  [build] — none in ${MODE} mode (skipped)"
fi

# ── Check lane: every other gate, concurrently ───────────────────────────
CHECK_PIDS=()
for gate in "${DEVELOPER_GATES[@]}"; do
    if ! is_build_gate "${gate}"; then
        log="${WORK}/gate-${gate//\//_}.log"
        # launch concurrently
        (
            run_gate "${gate}" "${log}"
        ) &
        CHECK_PIDS+=($!)
        echo "  [check] ${gate} — running..."
    fi
done

# wait for check lane
CHECK_FAILED=0
for pid in "${CHECK_PIDS[@]}"; do
    wait "${pid}" || CHECK_FAILED=1
done
if [[ -n "${BUILD_LANE_PID}" ]]; then
    wait "${BUILD_LANE_PID}" || CHECK_FAILED=1
fi

# ── Report in manifest order ─────────────────────────────────────────────
echo ""
idx=0
for gate in "${DEVELOPER_GATES[@]}"; do
    idx=$((idx + 1))
    log="${WORK}/gate-${gate//\//_}.log"
    elapsed="${ELAPSED[$gate]:-?}"
    # re-read elapsed from FAILED_GATES if it failed (since child process array not visible in parent — ELAPSED in subshell is lost)
    # So recompute via file mtime fallback: parse FAILED_GATES third column or stat log
    if [[ "$elapsed" == "?" || -z "$elapsed" ]]; then
        # extract from FAILED_GATES if present
        elapsed="$(awk -F'\t' -v g="$gate" '$2==g{print $3}' "${FAILED_GATES}" 2>/dev/null | head -1)"
        if [[ -z "$elapsed" ]]; then
            # fallback: 0 if missing
            elapsed="?"
        fi
    fi
    # For passed gates, ELAPSED is also in subshell — we need to recompute from log mtime if still ?
    if [[ "$elapsed" == "?" ]]; then
        if [[ -f "$log" ]]; then
            # approximate as file age not accurate; just show "?"
            elapsed="?"
        fi
    fi
    if awk -F'\t' -v gate="${gate}" '$2 == gate { found=1 } END { exit !found }' "${FAILED_GATES}"; then
        printf '─── [%2d/%d] %-42s GATE_FAIL' "$idx" "$GATE_COUNT" "$gate"
        if [[ "$elapsed" != "?" ]]; then printf ' (%ss)' "$elapsed"; fi
        echo ""
        if [[ -f "${log}" ]]; then
            # last 30 lines, keep diagnosable
            tail -n 30 "${log}" | sed 's/^/     │ /'
        else
            echo "     (no log)"
        fi
    else
        summary="$(grep -v '^[[:space:]]*$' "${log}" 2>/dev/null | grep -E "PASS|GATE_PASS|HYGIENE_PASS|DEDUP_PASS|SUBJECT_PASS|GATE_WARN" | tail -1 || tail -1 "${log}" 2>/dev/null | cut -c1-120)"
        if [[ -z "$summary" ]]; then summary="(no output)"; fi
        printf '─── [%2d/%d] %-42s PASS' "$idx" "$GATE_COUNT" "$gate"
        if [[ "$elapsed" != "?" ]]; then printf ' (%ss)' "$elapsed"; fi
        echo " — ${summary}"
    fi
done

WALL_END="$(date +%s)"
WALL_SEC=$((WALL_END - WALL_START))

if [[ -s "${FAILED_GATES}" || "${CHECK_FAILED}" -ne 0 ]]; then
    echo "" >&2
    while IFS=$'\t' read -r rc gate elapsed; do
        echo "GATE_FAIL: ${gate} (exit ${rc}, ${elapsed}s)" >&2
    done < "${FAILED_GATES}"
    echo "run_developer_gates.sh: GATE_FAIL — ${#DEVELOPER_GATES[@]} gates, ${WALL_SEC}s wall, mode=${MODE} — push aborted" >&2
    # keep logs for inspection
    LAST="/tmp/chronon-last-gate-fail"
    rm -rf "${LAST}" 2>/dev/null; cp -r "${WORK}" "${LAST}" 2>/dev/null || true
    echo "  last fail logs: ${LAST}" >&2
    echo "  hint: CHRONON3D_PRE_PUSH_FULL=1 bash tools/run_developer_gates.sh --full (full cert)" >&2
    exit 1
fi

echo ""
echo "run_developer_gates.sh: ${GATE_COUNT}/${GATE_COUNT} developer gates PASSED (mode=${MODE}, wall=${WALL_SEC}s)"
exit 0
