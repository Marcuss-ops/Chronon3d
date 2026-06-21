#!/usr/bin/env bash
# tools/check_architecture_boundaries.sh
# ─────────────────────────────────────────────────────────────────────
# Architectural invariants grep-guard.  Twelve P0 "must-be-absent"
# patterns catch the recurring regression classes the compiler alone
# can't see — stale include resurrection, retired symbol revival,
# false noexcept contracts, the borrowed-vs-constructed discipline
# (no local executor / no arena parameter returns), scheduler-
# boundary violations (factory leaks under the executor + raw TBB
# dispatchers in tile coordinators), identity-vs-composition-name
# key aliasing, and tracked build artefacts.  Every P0 check follows
# the pipefail-safe `[ -n "$hits" ]` shape documented on
# `strip_comments()` below.
#
# Wired into CI by tools/test_architectural.sh Section 6
# (`child_target_check_arch_boundary`) which `.github/workflows/gates.yml`
# invokes in its `architecture-check` job.  Any P0 violation exits the
# script non-zero; CI surfaces the file:line and the failing pattern
# verbatim in the build log.
#
# Selftest hooks (used to run each guard against a synthetic mktemp tree):
#   BOUNDARY_CHECK_ROOT  replaces the default include/src/tests/apps scan
#   EXECUTOR_SCOPED_DIR  replaces the default src/render_graph/executor scope
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT"

if [ -n "${BOUNDARY_CHECK_ROOT:-}" ]; then
    ROOT_DIRS=("${BOUNDARY_CHECK_ROOT}")
else
    ROOT_DIRS=(include src tests apps)
fi
EXECUTOR_SCOPED_DIR="${EXECUTOR_SCOPED_DIR:-src/render_graph/executor}"

FAILED=0

# strip_comments < stdin → stdout
# Filter grep-hit lines whose content is a pure C++ comment, so
# legitimate historical references (e.g. `// arena_override removed`)
# do not fire as false-positive violations.  Preprocessor directives
# (`#include`, `#pragma once`, `#define`) are NOT comments and pass
# through — this preserves the diagnostic file:line on checks 1–3
# (which expect `#include` hits).
#
# ----------------------------------------------------------------------
# IMPORTANT — pipefail-safe usage pattern (canonical "how to add a check")
# ----------------------------------------------------------------------
# With `set -o pipefail` (script top) + the trailing `|| true` below,
# the pipeline `... | strip_comments` ALWAYS exits 0 once the upstream
# grep finds anything (because `grep -v` returns 1 when the filtered
# output is empty — i.e. NO lines survived the comment filter — and
# `|| true` forces exit 0).  An `if pipeline; then` conditional
# therefore fires the THEN branch even when the post-filter output is
# empty — a real false positive.  EVERY check below that pipes through
# `strip_comments` MUST therefore capture the filtered stream into a
# variable and test with `[ -n "$hits" ]` (semantic: string length
# after filter), NOT `if pipeline; then`.
#
# Check 12 alone uses `git ls-files` (not source grep) and therefore
# omits the `strip_comments` pipe — the `[ -n "$hits" ]` check on its
# output still applies.
strip_comments() {
    grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|/\*)' || true
}

echo "=== Architecture boundary grep checks ==="

fail() { echo "FAIL: $1"; FAILED=1; }
pass() { echo "PASS: $1"; }

# ── 1. core/memory/render_session.hpp must not be re-included
echo -n "  [ 1/12] core/memory/render_session.hpp ...                "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include.*core/memory/render_session\.hpp' "${ROOT_DIRS[@]}" 2>/dev/null \
    | strip_comments || true)
if [ -n "$hits" ]; then
    fail "core/memory/render_session.hpp is RETIRED (TICKET-011 split)"
    echo "$hits" | sed 's/^/    /'
else
    pass "no retired core/memory/render_session.hpp include"
fi

# ── 2. renderer_runtime_resources.hpp must not be re-included
echo -n "  [ 2/12] renderer_runtime_resources.hpp ...               "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include.*renderer_runtime_resources\.hpp' "${ROOT_DIRS[@]}" 2>/dev/null \
    | strip_comments || true)
if [ -n "$hits" ]; then
    fail "renderer_runtime_resources.hpp is RETIRED (TICKET-009 / 011)"
    echo "$hits" | sed 's/^/    /'
else
    pass "no retired renderer_runtime_resources.hpp include"
fi

# ── 3. renderer_cache_state.hpp must not be re-included
echo -n "  [ 3/12] renderer_cache_state.hpp ...                     "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include.*renderer_cache_state\.hpp' "${ROOT_DIRS[@]}" 2>/dev/null \
    | strip_comments || true)
if [ -n "$hits" ]; then
    fail "renderer_cache_state.hpp is RETIRED (TICKET-011)"
    echo "$hits" | sed 's/^/    /'
else
    pass "no retired renderer_cache_state.hpp include"
fi

# ── 4. clear_per_frame() method retired
echo -n "  [ 4/12] legacy full-reset shim RETIRED ...               "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E 'clear_per_frame' "${ROOT_DIRS[@]}" 2>/dev/null \
    | strip_comments || true)
if [ -n "$hits" ]; then
    fail "clear_per_frame() is RETIRED"
    echo "$hits" | sed 's/^/    /'
else
    pass "no clear_per_frame() call site"
fi

# ── 5. ExecutionPlanCache / plan_cache retired
echo -n "  [ 5/12] ExecutionPlanCache RETIRED ...                   "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E 'plan_cache|ExecutionPlanCache' "${ROOT_DIRS[@]}" 2>/dev/null \
    | strip_comments || true)
if [ -n "$hits" ]; then
    fail "ExecutionPlanCache / plan_cache is RETIRED (PR-2 rewire)"
    echo "$hits" | sed 's/^/    /'
else
    pass "no ExecutionPlanCache / plan_cache reference (code only)"
fi

# ── 6. make_execution_scheduler under graph executor implementation
#      (scope configurable via EXECUTOR_SCOPED_DIR for selftest runs)
echo -n "  [ 6/12] graph executor MAKE scheduler ...                "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E 'make_execution_scheduler' "$EXECUTOR_SCOPED_DIR" 2>/dev/null \
    | strip_comments || true)
if [ -n "$hits" ]; then
    fail "make_execution_scheduler MUST NOT appear under $EXECUTOR_SCOPED_DIR (WP-0 P0 guard)"
    echo "$hits" | sed 's/^/    /'
else
    pass "no make_execution_scheduler under graph executor"
fi

# ── 7. tbb::parallel_for in tile coordinator files
echo -n "  [ 7/12] tile coordinator raw tbb::parallel_for ...        "
TILE_COORD_FILES=(
    src/render_graph/pipeline/tile_execution_coordinator.cpp
    src/render_graph/pipeline/scene_tile_execution.cpp
    src/render_graph/pipeline/tile_execution_coordinator.hpp
    src/render_graph/pipeline/scene_tile_execution.hpp
    src/render_graph/pipeline/tile_execution_policy.hpp
    src/render_graph/pipeline/tile_execution_policy.cpp
)
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E 'tbb::parallel_for' "${TILE_COORD_FILES[@]}" 2>/dev/null \
    | strip_comments || true)
if [ -n "$hits" ]; then
    fail "raw tbb::parallel_for MUST NOT appear in tile coordinator (WP-0 P0 guard)"
    echo "$hits" | sed 's/^/    /'
else
    pass "tile coordinator routes through ExecutionScheduler"
fi

# ── 8. arena_override parameter returning to GraphExecutor
echo -n "  [ 8/12] arena_override returned to executor ...           "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E 'arena_override' include src tests 2>/dev/null \
    | strip_comments || true)
if [ -n "$hits" ]; then
    fail "arena_override is RETIRED (WP-0 P0 guard)"
    echo "$hits" | sed 's/^/    /'
else
    pass "no arena_override parameter"
fi

# ── 9. PrecompNode constructs GraphExecutor locally
echo -n "  [ 9/12] PrecompNode local GraphExecutor ...               "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E 'GraphExecutor[[:space:]]+[a-zA-Z_][a-zA-Z0-9_]*[[:space:]]*;' \
    src/render_graph/cache/precomp_node_execute.cpp 2>/dev/null \
    | strip_comments || true)
if [ -n "$hits" ]; then
    fail "PrecompNode MUST borrow session->services().executor (WP-0 P0 guard)"
    echo "$hits" | sed 's/^/    /'
else
    pass "PrecompNode borrows session executor"
fi

# ── 10. PrecompNode stores composition-name-only owner key
echo -n "  [10/12] PrecompNode composition-name-only key ...         "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '\.graph[[:space:]]*=[[:space:]]*hash_string\([^)]*m_comp_name' \
    src/render_graph/nodes/precomp_node.hpp \
    src/render_graph/nodes/precomp_node.cpp \
    src/render_graph/cache/precomp_node_execute.cpp 2>/dev/null \
    | strip_comments || true)
if [ -n "$hits" ]; then
    fail "PrecompNode owner key is composition-name-only (WP-0 P0 guard)"
    echo "$hits" | sed 's/^/    /'
else
    pass "PrecompNode owner key is identity-driven (WP-4 invariant)"
fi

# ── 11. RenderRuntime::backend() declared noexcept (would throw)
echo -n "  [11/12] RenderRuntime::backend() noexcept ...             "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' \
    -E 'backend\(\)[[:space:]]+noexcept' \
    include/chronon3d/runtime src/runtime 2>/dev/null \
    | strip_comments || true)
if [ -n "$hits" ]; then
    fail "RenderRuntime::backend() noexcept is a false contract (WP-0 P0 guard)"
    echo "$hits" | sed 's/^/    /'
else
    pass "RenderRuntime::backend() does not lie about noexcept"
fi

# ── 12. Generated build/output dirs or *.tsbuildinfo tracked
#       (git ls-files — see strip_comments() docstring for special case)
echo -n "  [12/12] generated build/output/tsbuildinfo tracked ...   "
if command -v git >/dev/null 2>&1; then
    hits=$(git ls-files 2>/dev/null \
        | grep -E '(^|/)build(/|$)|(^|/)output(/|$)|(^|/)out(/|$)|\.tsbuildinfo$' \
        | grep -vE '^docs/' || true)
else
    hits=""
fi
if [ -n "$hits" ]; then
    fail "generated build/output/tsbuildinfo is tracked by git (WP-0 P0 guard)"
    echo "$hits" | head -20 | sed 's/^/    /'
else
    pass "no generated build/output/tsbuildinfo tracked"
fi

# ── Final summary ─────────────────────────────────────────────────────
echo ""
if [ "$FAILED" -ne 0 ]; then
    echo "=== Architecture boundary checks FAILED (one or more P0 violations) ==="
    exit 1
fi
echo "=== All 12 architecture boundary checks PASSED ==="
exit 0
