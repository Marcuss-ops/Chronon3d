#!/usr/bin/env bash
# tools/check_architecture_boundaries.sh
# ─────────────────────────────────────────────────────────────────────
# WP-0 PR 0.0 — Fixed critical bug: check [5/5] was nested INSIDE the
# final PASS/FAIL else-branch, so a real `plan_cache` /
# `ExecutionPlanCache` hit set FAILED=1 but the script still exited 0.
# Every check runs BEFORE the final decision; the script never prints
# PASS after FAIL.
#
# WP-0 PR 0.1 — Added 7 P0 "must be absent" guards + 3 explicit
# allowlists (with TODO tickets) for known historical references
# that are not yet migrated:
#   - tests/render_graph/executor/test_scheduler_determinism.cpp
#     (legacy ExecutionPlanCache usage; needs WP-2 final-stage
#     scheduler-determinism migration to retire)
#   - src/runtime/render_session.cpp (comment-only `clear_per_frame`
#     reference; needs documentation scrub in WP-3 PR 3.4 close-out)
#   - src/render_graph/cache/precomp_node_execute.cpp (TODO-WP4
#     stable-node-identity: ctor m_instance_key still derives from
#     comp_name until WP-4 lands)
#
# Configurable roots (used by selftest to run each guard against a
# synthetic mktemp tree):
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
# Filter grep-hit lines whose content is a pure C++ comment, so that
# legitimate historical references (e.g. `// arena_override removed`)
# do not fire as false-positive violations.  Preprocessor directives
# (`#include`, `#pragma once`, `#define`) are NOT comments and pass
# through — this preserves the diagnostic file:line on checks 1–3
# (which expect `#include` hits).
strip_comments() {
    grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|/\*)' || true
}

# allowlist_filter <pattern>
# stdin:  multi-line `file:line:content` hits from grep
# stdout: hits whose `file:` part does NOT match the regex
# Usage:  real_hits=$(echo "$hits_raw" | allowlist_filter 'path/to/file\.cpp')
allowlist_filter() {
    grep -vE "$1" || true
}

echo "=== Architecture boundary grep checks ==="

fail() { echo "FAIL: $1"; FAILED=1; }
pass() { echo "PASS: $1"; }

# ── 1. core/memory/render_session.hpp (TICKET-011 split) ───────────────
echo -n "  [ 1/12] core/memory/render_session.hpp ...                "
if grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include.*core/memory/render_session\.hpp' "${ROOT_DIRS[@]}" 2>/dev/null \
    | strip_comments; then
    fail "core/memory/render_session.hpp is RETIRED (TICKET-011 split)"
else
    pass "no retired core/memory/render_session.hpp include"
fi

# ── 2. renderer_runtime_resources.hpp (TICKET-009 / 011) ───────────────
echo -n "  [ 2/12] renderer_runtime_resources.hpp ...               "
if grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include.*renderer_runtime_resources\.hpp' "${ROOT_DIRS[@]}" 2>/dev/null \
    | strip_comments; then
    fail "renderer_runtime_resources.hpp is RETIRED (TICKET-009 / 011)"
else
    pass "no retired renderer_runtime_resources.hpp include"
fi

# ── 3. renderer_cache_state.hpp (TICKET-011) ───────────────────────────
echo -n "  [ 3/12] renderer_cache_state.hpp ...                     "
if grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include.*renderer_cache_state\.hpp' "${ROOT_DIRS[@]}" 2>/dev/null \
    | strip_comments; then
    fail "renderer_cache_state.hpp is RETIRED (TICKET-011)"
else
    pass "no retired renderer_cache_state.hpp include"
fi

# ── 4. clear_per_frame() method (WP-3 PR 3.4 close-out) ────────────────
# TODO-WP3-3.4-doc-scrub: allowlist src/runtime/render_session.cpp for
# the comment-only `clear_per_frame` reference; remove the allowlist
# once the documentation is updated.
echo -n "  [ 4/12] legacy full-reset shim RETIRED ...               "
hits_raw=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E 'clear_per_frame' "${ROOT_DIRS[@]}" 2>/dev/null \
    | strip_comments || true)
real_hits=$(echo "$hits_raw" | allowlist_filter 'src/runtime/render_session\.cpp')
allow_hits=$(echo "$hits_raw" | grep -E '^src/runtime/render_session\.cpp:' || true)
if [ -n "$real_hits" ]; then
    fail "clear_per_frame() is RETIRED"
    echo "$real_hits" | sed 's/^/    /'
elif [ -n "$allow_hits" ]; then
    echo "PASS (TODO-WP3-3.4-doc-scrub): clear_per_frame ALLOWLISTED at:"
    echo "      $allow_hits"
else
    pass "no clear_per_frame() call site"
fi

# ── 5. ExecutionPlanCache / plan_cache (PR-0.0 + PR-0.1 fix) ───────────
# TODO-WP2-scheduler-det: allowlist
# tests/render_graph/executor/test_scheduler_determinism.cpp — that
# file's WP-1 PR 1.4 registration still references the legacy
# ExecutionPlanCache* type.  The next scheduler-determinism migration
# must retire those references; remove the allowlist then.
echo -n "  [ 5/12] ExecutionPlanCache RETIRED ...                   "
hits_raw=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E 'plan_cache|ExecutionPlanCache' "${ROOT_DIRS[@]}" 2>/dev/null \
    | strip_comments || true)
real_hits=$(echo "$hits_raw" | allowlist_filter 'tests/render_graph/executor/test_scheduler_determinism\.cpp')
allow_hits=$(echo "$hits_raw" | grep -E '^tests/render_graph/executor/test_scheduler_determinism\.cpp:' || true)
if [ -n "$real_hits" ]; then
    fail "ExecutionPlanCache / plan_cache is RETIRED (PR-2 rewire)"
    echo "$real_hits" | sed 's/^/    /'
elif [ -n "$allow_hits" ]; then
    echo "PASS (TODO-WP2-scheduler-det): ExecutionPlanCache ALLOWLISTED at:"
    echo "      $allow_hits"
else
    pass "no ExecutionPlanCache / plan_cache reference"
fi

# ── 6. make_execution_scheduler under graph executor implementation ───
# Scope is configurable via EXECUTOR_SCOPED_DIR so the selftest can
# target a mktemp tree.
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

# ── 7. tbb::parallel_for in tile coordinator files ──────────────────────
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

# ── 8. arena_override parameter returning to GraphExecutor ────────────
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

# ── 9. PrecompNode constructs GraphExecutor locally ───────────────────
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

# ── 10. PrecompNode stores composition-name-only owner key ─────────────
# TODO-WP4-stable-node-identity: allowlisted at
# src/render_graph/cache/precomp_node_execute.cpp:38 until WP-4 lands.
echo -n "  [10/12] PrecompNode composition-name-only key ...         "
hits_raw=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '\.graph[[:space:]]*=[[:space:]]*hash_string\([^)]*m_comp_name' \
    src/render_graph/nodes/precomp_node.hpp \
    src/render_graph/nodes/precomp_node.cpp \
    src/render_graph/cache/precomp_node_execute.cpp 2>/dev/null \
    | strip_comments || true)
real_hits=$(echo "$hits_raw" | allowlist_filter 'precomp_node_execute\.cpp')
allow_hits=$(echo "$hits_raw" | grep -E ':[^:]+:[[:space:]]*\.graph[[:space:]]*=[[:space:]]*hash_string' | grep -E 'precomp_node_execute\.cpp' || true)
if [ -n "$real_hits" ]; then
    fail "PrecompNode owner key is composition-name-only (WP-0 P0 guard)"
    echo "$real_hits" | sed 's/^/    /'
elif [ -n "$allow_hits" ]; then
    echo "PASS (TODO-WP4): PrecompNode owner key ALLOWLISTED at:"
    echo "      $allow_hits"
else
    pass "PrecompNode owner key is identity-driven (WP-4 invariant)"
fi

# ── 11. RenderRuntime::backend() declared noexcept (would throw) ──────
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

# ── 12. Generated build/output dirs or *.tsbuildinfo tracked ──────────
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
