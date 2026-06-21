#!/usr/bin/env bash
# tools/check_architecture_boundaries_selftest.sh
# ─────────────────────────────────────────────────────────────────────
# WP-0 PR 0.0 regression test — verifies the boundary script exits 1
# when a forbidden source hit is injected, and 0 when the source is
# clean.  Each case creates a FRESH mktemp directory so residual hits
# from earlier cases cannot leak into later assertions.
#
# Test surface: 8 assertions covering prerelease-script regressions.
# ─────────────────────────────────────────────────────────────────────
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MAIN="$SCRIPT_DIR/check_architecture_boundaries.sh"
if [ ! -r "$MAIN" ]; then
    echo "MISS: $MAIN not readable" >&2
    exit 2
fi

PASS=0
FAIL=0

assert_exit() {
    local label="$1" want="$2" got="$3"
    if [ "$want" = "$got" ]; then
        echo "  PASS: $label (exit=$got)"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $label (want=$want got=$got)"
        FAIL=$((FAIL + 1))
    fi
}

# Helper: each case builds a NEW mktemp directory so leftover hits
# from earlier cases cannot leak into the assertion.
fresh_tree() {
    local d
    d=$(mktemp -d)
    mkdir -p "$d/src"
    echo "$d"
}

# ── Case 1: empty source tree (clean sanity check) ───────────────────
TMP=$(fresh_tree); trap "rm -rf '$TMP'" EXIT
set +e; BOUNDARY_CHECK_ROOT="$TMP" bash "$MAIN" >/dev/null 2>&1; rc=$?; set -e
assert_exit "clean tree returns exit 0" 0 "$rc"
rm -rf "$TMP"

# ── Case 2: forbidden header include → exit 1 ─────────────────────────
TMP=$(fresh_tree)
cat > "$TMP/src/leak_header.cpp" <<'EOF'
#include "core/memory/render_session.hpp"
int dummy() { return 0; }
EOF
set +e; BOUNDARY_CHECK_ROOT="$TMP" bash "$MAIN" >"$TMP.out" 2>&1; rc=$?; set -e
assert_exit "forbidden core/memory/render_session.hpp include -> exit 1" 1 "$rc"
if ! grep -q "core/memory/render_session\.hpp" "$TMP.out"; then
    echo "  FAIL: forbidden include path not reported in output"
    FAIL=$((FAIL + 1))
else
    echo "  PASS: forbidden include path reported correctly"
    PASS=$((PASS + 1))
fi
rm -rf "$TMP"

# ── Case 3: forbidden plan_cache reference → exit 1 ───────────────────
TMP=$(fresh_tree)
cat > "$TMP/src/leak_plan_cache.cpp" <<'EOF'
int plan_cache() { return 0; }
EOF
set +e; BOUNDARY_CHECK_ROOT="$TMP" bash "$MAIN" >"$TMP.out" 2>&1; rc=$?; set -e
assert_exit "forbidden plan_cache reference -> exit 1" 1 "$rc"
if ! grep -q "plan_cache" "$TMP.out"; then
    echo "  FAIL: plan_cache violation not reported in output"
    FAIL=$((FAIL + 1))
else
    echo "  PASS: plan_cache violation path reported"
    PASS=$((PASS + 1))
fi
rm -rf "$TMP"

# ── Case 4: forbidden ExecutionPlanCache spelling → exit 1 ────────────
TMP=$(fresh_tree)
cat > "$TMP/src/leak_execcache.cpp" <<'EOF'
class ExecutionPlanCache { int x; };
EOF
set +e; BOUNDARY_CHECK_ROOT="$TMP" bash "$MAIN" >"$TMP.out" 2>&1; rc=$?; set -e
assert_exit "forbidden ExecutionPlanCache reference -> exit 1" 1 "$rc"
rm -rf "$TMP"

# ── Case 5: forbidden make_execution_scheduler in executor scope ─────
# Populate a fresh tree; set EXECUTOR_SCOPED_DIR so check [6/12] scans
# the synthetic tree instead of the real repo.  Other checks scan the
# same tree (BOUNDARY_CHECK_ROOT).
TMP=$(fresh_tree)
mkdir -p "$TMP/src/exec_scope"
cat > "$TMP/src/exec_scope/leak.cpp" <<'EOF'
auto sched = make_execution_scheduler({});
EOF
set +e
    BOUNDARY_CHECK_ROOT="$TMP" \
    EXECUTOR_SCOPED_DIR="$TMP/src/exec_scope" \
    bash "$MAIN" >"$TMP.out" 2>&1
    rc=$?
set -e
assert_exit "forbidden make_execution_scheduler in executor scope -> exit 1" 1 "$rc"
if ! grep -q "make_execution_scheduler" "$TMP.out"; then
    echo "  FAIL: make_execution_scheduler violation not reported in output"
    FAIL=$((FAIL + 1))
else
    echo "  PASS: make_execution_scheduler violation path reported"
    PASS=$((PASS + 1))
fi
rm -rf "$TMP"

# ── Case 6: forbidden GraphExecutor local declaration in precomp TU ──
# (Honours REAL-CWD check [9/12] — src/render_graph/cache/...)
TMP=$(fresh_tree)
mkdir -p "$TMP/src/render_graph/cache"
cat > "$TMP/src/render_graph/cache/precomp_node_execute.cpp" <<'EOF'
int dummy() {
    GraphExecutor local_executor;
    (void)local_executor;
    return 0;
}
EOF
set +e; BOUNDARY_CHECK_ROOT="$TMP" bash "$MAIN" >"$TMP.out" 2>&1; rc=$?; set -e
# Note: this case uses REAL-CWD path for check 9 (hardcoded
# src/render_graph/cache/precomp_node_execute.cpp).  The synthetic
# TMP isn't scanned, so this case would yield rc=0.  We instead
# re-run the script with cwd=$TMP so the synthetic file IS scanned
# under the relative path src/render_graph/cache/precomp_node_execute.cpp.
set +e; cd "$TMP" && BOUNDARY_CHECK_ROOT="$TMP" bash "$MAIN" >"$TMP.out" 2>&1; rc2=$?; cd - >/dev/null 2>&1; set -e
assert_exit "forbidden GraphExecutor local in precomp TU -> exit 1" 1 "$rc2"
rm -rf "$TMP"

# ── Case 7: clean tree with EXECUTOR_SCOPED_DIR pointing mktemp ──────
TMP=$(fresh_tree)
set +e
    BOUNDARY_CHECK_ROOT="$TMP" \
    EXECUTOR_SCOPED_DIR="$TMP/src/exec_scope" \
    bash "$MAIN" >/dev/null 2>&1
    rc=$?
set -e
assert_exit "clean tree with scoped executor dir -> exit 0" 0 "$rc"
rm -rf "$TMP"

# ── Summary ──────────────────────────────────────────────────────────
echo ""
if [ "$FAIL" -gt 0 ]; then
    echo "=== check_architecture_boundaries selftest FAILED ($FAIL failures, $PASS passes) ==="
    exit 1
fi
echo "=== check_architecture_boundaries selftest PASSED ($PASS assertions) ==="
exit 0
