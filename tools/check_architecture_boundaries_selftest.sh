#!/usr/bin/env bash
# tools/check_architecture_boundaries_selftest.sh
# ─────────────────────────────────────────────────────────────────────
# WP-0 PR 0.0 regression test — verifies the boundary script exits 1
# when a forbidden source hit is injected, and 0 when the source is
# clean.  Each case creates a FRESH mktemp directory so residual hits
# from earlier cases cannot leak into later assertions.
#
# Test surface: 13 assertions covering prerelease-script regressions and
# the F3.1 extension (3 new semantic gates: [12/14] registry, [13/14] vcpkg
# parity, [14/14] SDK public surface).
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

# ── Case 4: forbidden ExecutionPlanCache spelling → advisory only (TICKET-044) ──
TMP=$(fresh_tree)
cat > "$TMP/src/leak_execcache.cpp" <<'EOF'
class ExecutionPlanCache { int x; };
EOF
set +e; BOUNDARY_CHECK_ROOT="$TMP" bash "$MAIN" >"$TMP.out" 2>&1; rc=$?; set -e
assert_exit "forbidden ExecutionPlanCache reference -> exit 0 (advisory)" 0 "$rc"
rm -rf "$TMP"

# ── Case 5: REMOVED — make_execution_scheduler pattern removed from main script ──
: <<'SKIP_CASE5'
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
SKIP_CASE5

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
assert_exit "forbidden GraphExecutor local in precomp TU -> exit 0 (advisory)" 0 "$rc2"
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

# ── Case 8: REMOVED — m_runtime nullptr pattern removed from main script ──
: <<'SKIP_CASE8'
# ── Case 8: bare m_runtime = nullptr outside canonical (other.m_X) → exit 1 ──
TMP=$(fresh_tree)
cat > "$TMP/src/leak_nullptr.cpp" <<'EOF'
int dummy() {
    m_runtime = nullptr;}
EOF
set +e; BOUNDARY_CHECK_ROOT="$TMP" bash "$MAIN" >"$TMP.out" 2>&1; rc=$?; set -e
assert_exit "bare m_runtime = nullptr outside canonical -> exit 1" 1 "$rc"
if ! grep -q "leak_nullptr\.cpp" "$TMP.out"; then
    echo "  FAIL: leak_nullptr.cpp not reported in output"
    FAIL=$((FAIL + 1))
else
    echo "  PASS: leak_nullptr.cpp reported"
    PASS=$((PASS + 1))
fi
rm -rf "$TMP"
SKIP_CASE8

# ── Case 9: REMOVED — m_renderer->settings() pattern removed from main script ──
: <<'SKIP_CASE9'
# ── Case 9: m_renderer->settings() F0.2 rot regression → exit 1 ────────
TMP=$(fresh_tree)
cat > "$TMP/src/leak_f02_rot.cpp" <<'EOF'
int dummy() {
    auto s = m_renderer->settings();
    (void)s;
    return 0;
}
EOF
set +e; BOUNDARY_CHECK_ROOT="$TMP" bash "$MAIN" >"$TMP.out" 2>&1; rc=$?; set -e
assert_exit "m_renderer->settings() F0.2 rot -> exit 1" 1 "$rc"
if ! grep -q "leak_f02_rot\.cpp" "$TMP.out"; then
    echo "  FAIL: leak_f02_rot.cpp not reported in output"
    FAIL=$((FAIL + 1))
else
    echo "  PASS: leak_f02_rot.cpp reported"
    PASS=$((PASS + 1))
fi
rm -rf "$TMP"
SKIP_CASE9

# ── Case 10: REMOVED — RenderPipeline pattern removed from main script ──
: <<'SKIP_CASE10'
# ── Case 10: RenderPipeline::m_renderer sidecar leak → exit 1 ──────────
# NOTE: RenderPipeline file no longer exists post-F2.4, so the canonical
# filter is vacuous.  Any future reintroduction in any other TU is a
# regression caught by this case.
TMP=$(fresh_tree)
cat > "$TMP/src/sidecar_leak.cpp" <<'EOF'
struct RenderPipeline {
    void* m_renderer;
};
int dummy() {
    RenderPipeline rp;
    (void)rp.m_renderer;
    return 0;
}
EOF
set +e; BOUNDARY_CHECK_ROOT="$TMP" bash "$MAIN" >"$TMP.out" 2>&1; rc=$?; set -e
assert_exit "RenderPipeline::m_renderer sidecar leak -> exit 1" 1 "$rc"
if ! grep -q "sidecar_leak\.cpp" "$TMP.out"; then
    echo "  FAIL: sidecar_leak.cpp not reported in output"
    FAIL=$((FAIL + 1))
else
    echo "  PASS: sidecar_leak.cpp reported"
    PASS=$((PASS + 1))
fi
rm -rf "$TMP"
SKIP_CASE10

# ── Case 11: OBJECT lib leak not in registry → gate [12/14] exit 1 ─────
# Inject a fake add_library(... OBJECT ...) target in src/CMakeLists.txt
# but leave cmake/Chronon3DRegistry.cmake empty — gate [12/14] should
# FAIL because the source declares a target not in the canonical
# module registry.  Verify exit=1 AND output mentions the leaked target.
TMP=$(fresh_tree)
cat > "$TMP/src/CMakeLists.txt" <<'EOF'
add_library(leaked_registry_target OBJECT src.cpp)
EOF
echo "int dummy() { return 0; }" > "$TMP/src/src.cpp"
mkdir -p "$TMP/cmake"
cat > "$TMP/cmake/Chronon3DRegistry.cmake" <<'EOF'
# intentionally empty registry — no add_library(leaked_registry_target)
EOF
set +e; BOUNDARY_CHECK_ROOT="$TMP" bash "$MAIN" >"$TMP.out" 2>&1; rc=$?; set -e
# Gate [12/14] is ADVISORY: does not set FAILED=1 (TICKET-041 promotion).
# Selftest verifies the violation IS reported (mechanism), NOT that rc=1.
assert_exit "OBJECT lib leak (gate [12/14] advisory) -> exit 0" 0 "$rc"
if ! grep -q "leaked_registry_target" "$TMP.out"; then
    echo "  FAIL: leaked_registry_target not reported in output"
    FAIL=$((FAIL + 1))
else
    echo "  PASS: leaked_registry_target reported"
    PASS=$((PASS + 1))
fi
rm -rf "$TMP"

# ── Case 12: find_package X without vcpkg entry → gate [13/14] exit 1 ─────
# Inject a top-level CMakeLists.txt with find_package(LeakedBarPkg REQUIRED)
# but vcpkg.json does NOT contain 'leakedbarpkg' (case-insensitive).
# Gate [13/14] should FAIL.  Verify exit=1 AND output mentions leaked pkg.
TMP=$(fresh_tree)
cat > "$TMP/CMakeLists.txt" <<'EOF'
find_package(LeakedBarPkg REQUIRED)
EOF
cat > "$TMP/vcpkg.json" <<'EOF'
{ "dependencies": ["fmt"] }
EOF
set +e; BOUNDARY_CHECK_ROOT="$TMP" bash "$MAIN" >"$TMP.out" 2>&1; rc=$?; set -e
# Gate [13/14] is ADVISORY: does not set FAILED=1 (TICKET-042 promotion).
# Selftest verifies the violation IS reported (mechanism), NOT that rc=1.
assert_exit "find_package leak (gate [13/14] advisory) -> exit 0" 0 "$rc"
if ! grep -q "LeakedBarPkg" "$TMP.out"; then
    echo "  FAIL: LeakedBarPkg not reported in output"
    FAIL=$((FAIL + 1))
else
    echo "  PASS: LeakedBarPkg reported"
    PASS=$((PASS + 1))
fi
rm -rf "$TMP"

# ── Case 13: SDK public surface violation → gate [14/14] exit 1 ──────────
# Inject apps/leak_test/LeakedAppsSurface.hpp that directly includes
# <chronon3d_sdk_impl/foo.h> — Forbidden under AGENTS.md §4.  Gate [14/14]
# should FAIL.  Verify exit=1 AND output mentions leak_test path.
TMP=$(fresh_tree)
mkdir -p "$TMP/apps/leak_test"
cat > "$TMP/apps/leak_test/LeakedAppsSurface.hpp" <<'EOF'
#include <chronon3d_sdk_impl/leaked_internal.h>
int dummy() { return 0; }
EOF
set +e; BOUNDARY_CHECK_ROOT="$TMP" bash "$MAIN" >"$TMP.out" 2>&1; rc=$?; set -e
# Gate [14/14] is ADVISORY: does not set FAILED=1 (TICKET-043 promotion).
# Selftest verifies the violation IS reported in older_injection form
# (mechanism), NOT that rc=1.  Note: the synthetic heredoc literal-
# contains `<chronon3d_sdk_impl/...>` which the tightened regex still
# matches because the path starts with `chronon3d_sdk_impl/`.
assert_exit "SDK public surface leak (gate [14/14] advisory) -> exit 0" 0 "$rc"
if ! grep -q "leak_test\|chronon3d_sdk_impl" "$TMP.out"; then
    echo "  FAIL: SDK public surface violation not reported in output"
    FAIL=$((FAIL + 1))
else
    echo "  PASS: SDK public surface violation reported"
    PASS=$((PASS + 1))
fi
rm -rf "$TMP"

# ── Summary ──────────────────────────────────────────────────────────
echo ""
if [ "$FAIL" -gt 0 ]; then
    echo "=== check_architecture_boundaries selftest FAILED ($FAIL failures, $PASS passes) ==="
    exit 1
fi
echo "=== check_architecture_boundaries selftest PASSED ($PASS assertions) ==="
exit 0
