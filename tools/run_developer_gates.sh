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
#   Full     = 13 gate (6 veloci + 7 lenti/build/meta) — CI e CHRONON3D_PRE_PUSH_FULL=1.
#   Nessun build su ogni push. Nessun selftest del gate su ogni push.
#
# Scheduling: build gates (ninja) serializzati tra loro (RAM ~1.6 GiB/TU, swap
# se paralleli), tutti gli altri gate in parallelo. Report in manifest order.
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

sanitize() { echo "${1//\//_}"; }

WALL_START="$(date +%s)"
echo "run_developer_gates.sh: starting developer gate chain (${GATE_COUNT} gates, mode=${MODE}, ref=${COMPARISON_REF})..."
if [[ "$MODE" == "slim" ]]; then
    echo "  slim = 6 gate veloci (arch + 5 grep). Full: CHRONON3D_PRE_PUSH_FULL=1 bash tools/run_developer_gates.sh --full"
fi

WORK="$(mktemp -d -t chronon-gates.XXXXXX)"
trap 'rm -rf "${WORK}"' EXIT

# ── Build lane: serialised ───────────────────────────────────────────────
has_build=0
for g in "${DEVELOPER_GATES[@]}"; do is_build_gate "$g" && { has_build=1; break; }; done

BUILD_PID=""
if (( has_build )); then
    (
        for gate in "${DEVELOPER_GATES[@]}"; do
            if is_build_gate "${gate}"; then
                safe="$(sanitize "$gate")"
                log="${WORK}/gate-${safe}.log"
                rcfile="${WORK}/gate-${safe}.rc"
                elapsedfile="${WORK}/gate-${safe}.elapsed"
                echo "  [build] ${gate} — running..."
                start="$(date +%s)"
                rc=0
                bash "${SCRIPT_DIR}/execute_gate.sh" "$gate" "$TARGET_REMOTE" "$TARGET_BRANCH" >"${log}" 2>&1 || rc=$?
                end="$(date +%s)"
                echo "$rc" > "${rcfile}"
                echo $((end - start)) > "${elapsedfile}"
                # also treat GATE_FAIL in output as failure even if rc==0 (WARN mode)
                # Self-tests deliberately run negative fixtures through gates,
                # so their captured output can contain GATE_FAIL on success.
                if (( rc != 0 )) || { [[ "$gate" != *selftest.sh ]] && grep -q "GATE_FAIL" "${log}" 2>/dev/null; }; then
                    # normalize rc to 1 if it was 0 but GATE_FAIL present
                    if (( rc == 0 )); then echo "1" > "${rcfile}"; fi
                fi
            fi
        done
    ) &
    BUILD_PID=$!
else
    echo "  [build] — none in ${MODE} mode (skipped)"
fi

# ── Check lane: every other gate, concurrently ───────────────────────────
CHECK_PIDS=()
for gate in "${DEVELOPER_GATES[@]}"; do
    if ! is_build_gate "${gate}"; then
        safe="$(sanitize "$gate")"
        log="${WORK}/gate-${safe}.log"
        rcfile="${WORK}/gate-${safe}.rc"
        elapsedfile="${WORK}/gate-${safe}.elapsed"
        echo "  [check] ${gate} — running..."
        (
            start="$(date +%s)"
            rc=0
            bash "${SCRIPT_DIR}/execute_gate.sh" "$gate" "$TARGET_REMOTE" "$TARGET_BRANCH" >"${log}" 2>&1 || rc=$?
            end="$(date +%s)"
            echo "$rc" > "${rcfile}"
            echo $((end - start)) > "${elapsedfile}"
            # Self-tests deliberately run negative fixtures through gates,
            # so their captured output can contain GATE_FAIL on success.
            if (( rc != 0 )) || { [[ "$gate" != *selftest.sh ]] && grep -q "GATE_FAIL" "${log}" 2>/dev/null; }; then
                if (( rc == 0 )); then echo "1" > "${rcfile}"; fi
            fi
        ) &
        CHECK_PIDS+=($!)
    fi
done

# wait
for pid in "${CHECK_PIDS[@]}"; do wait "${pid}" || true; done
if [[ -n "${BUILD_PID}" ]]; then wait "${BUILD_PID}" || true; fi

# ── Report in manifest order ─────────────────────────────────────────────
echo ""
FAILED_COUNT=0
idx=0
for gate in "${DEVELOPER_GATES[@]}"; do
    idx=$((idx + 1))
    safe="$(sanitize "$gate")"
    log="${WORK}/gate-${safe}.log"
    rcfile="${WORK}/gate-${safe}.rc"
    elapsedfile="${WORK}/gate-${safe}.elapsed"
    rc="$(cat "${rcfile}" 2>/dev/null || echo 2)"
    elapsed="$(cat "${elapsedfile}" 2>/dev/null || echo "?")"
    if [[ "$rc" != "0" ]]; then
        FAILED_COUNT=$((FAILED_COUNT + 1))
        if [[ "$elapsed" != "?" ]]; then
            printf '─── [%2d/%d] %-42s GATE_FAIL (%ss)\n' "$idx" "$GATE_COUNT" "$gate" "$elapsed"
        else
            printf '─── [%2d/%d] %-42s GATE_FAIL\n' "$idx" "$GATE_COUNT" "$gate"
        fi
        if [[ -f "${log}" ]]; then
            tail -n 30 "${log}" | sed 's/^/     │ /'
        else
            echo "     (no log)"
        fi
    else
        summary="$(grep -v '^[[:space:]]*$' "${log}" 2>/dev/null | grep -E "PASS|GATE_PASS|HYGIENE_PASS|DEDUP_PASS|SUBJECT_PASS|GATE_WARN" | tail -1 || tail -1 "${log}" 2>/dev/null | cut -c1-120)"
        if [[ -z "$summary" ]]; then summary="(no output)"; fi
        if [[ "$elapsed" != "?" ]]; then
            printf '─── [%2d/%d] %-42s PASS (%ss) — %s\n' "$idx" "$GATE_COUNT" "$gate" "$elapsed" "$summary"
        else
            printf '─── [%2d/%d] %-42s PASS — %s\n' "$idx" "$GATE_COUNT" "$gate" "$summary"
        fi
    fi
done

WALL_END="$(date +%s)"
WALL_SEC=$((WALL_END - WALL_START))

if (( FAILED_COUNT > 0 )); then
    echo "" >&2
    for gate in "${DEVELOPER_GATES[@]}"; do
        safe="$(sanitize "$gate")"
        rcfile="${WORK}/gate-${safe}.rc"
        elapsedfile="${WORK}/gate-${safe}.elapsed"
        rc="$(cat "${rcfile}" 2>/dev/null || echo "?")"
        elapsed="$(cat "${elapsedfile}" 2>/dev/null || echo "?")"
        if [[ "$rc" != "0" ]]; then
            echo "GATE_FAIL: ${gate} (exit ${rc}, ${elapsed}s)" >&2
        fi
    done
    echo "run_developer_gates.sh: GATE_FAIL — ${GATE_COUNT} gates, ${WALL_SEC}s wall, mode=${MODE} — push aborted" >&2
    LAST="/tmp/chronon-last-gate-fail"
    rm -rf "${LAST}" 2>/dev/null; cp -r "${WORK}" "${LAST}" 2>/dev/null || true
    echo "  last fail logs: ${LAST}" >&2
    echo "  hint: CHRONON3D_PRE_PUSH_FULL=1 bash tools/run_developer_gates.sh --full (full cert)" >&2
    exit 1
fi

echo ""
echo "run_developer_gates.sh: ${GATE_COUNT}/${GATE_COUNT} developer gates PASSED (mode=${MODE}, wall=${WALL_SEC}s)"
exit 0
