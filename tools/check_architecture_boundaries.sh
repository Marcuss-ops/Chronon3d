#!/usr/bin/env bash
# tools/check_architecture_boundaries.sh
# ─────────────────────────────────────────────────────────────────────
# WP-0 PR 0.0 — Fixed critical bug: check [5/5] was nested INSIDE the
# final PASS/FAIL else-branch, so a real `plan_cache` /
# `ExecutionPlanCache` hit set FAILED=1 but the script still exited 0.
# Every check runs BEFORE the final decision; the script never prints
# PASS after FAIL.
#
# WP-0 PR 0.1 — Added 7 P0 "must be absent" guards.  All historical
# reference comments have been scrubbed (WP-3 PR 3.4 + WP-5 + WP-8
# close-outs); no residual allowlists remain.
#
# WP-2 PR 2.5 — Test file
# tests/render_graph/executor/test_scheduler_determinism.cpp was
# migrated off `ExecutionPlanCache*` onto the CompiledFrameGraph path
# (FrameGraphCompiler + the 4-arg GraphExecutor::execute).  The
# TODO-WP2-scheduler-det allowlist is RETIRED.  Comment-only
# historical references in headers + tests are filtered by
# `strip_comments` on checks 1–11 (check 12 scans `git ls-files`,
# not source content, so it does not need stripping).  Only CODE
# reintroductions of `plan_cache` / `ExecutionPlanCache` fire
# check [5/12].  See `strip_comments()` regex at top of script.
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
#
# IMPORTANT — pipefail-safe usage pattern.
# With `set -o pipefail` (script top) + the trailing `|| true` below,
# the pipeline `... | strip_comments` ALWAYS exits 0 once the upstream
# grep finds anything (because `grep -v` returns 1 when the filtered
# output is empty — i.e. NO lines survived the comment filter — and
# `|| true` forces exit 0).  An `if pipeline; then` conditional
# therefore fires the THEN branch even when the post-filter output is
# empty — a real false positive.  EVERY check below that pipes through
# `strip_comments` MUST therefore capture the filtered stream into a
# variable and test with `[ -n "$hits" ]` (semantic: string length
# after filter), NOT `if pipeline; then`.  This is critical for
# checks 1–11 — see WP-2 PR 2.5 closure.
strip_comments() {
    grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|/\*)' || true
}

echo "=== Architecture boundary grep checks ==="

fail() { echo "FAIL: $1"; FAILED=1; }
pass() { echo "PASS: $1"; }

# ── 1. core/memory/render_session.hpp (TICKET-011 split) ───────────────
# See strip_comments docstring (top of script) for the pipefail-safe
# `[ -n "$hits" ]` pattern required for any check using strip_comments.
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

# ── 2. renderer_runtime_resources.hpp (TICKET-009 / 011) ───────────────
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

# ── 3. renderer_cache_state.hpp (TICKET-011) ───────────────────────────
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

# ── 4. clear_per_frame() method (WP-3 PR 3.4 close-out) ────────────────
# Historical comment in `src/runtime/render_session.cpp` was scrubbed in
# WP-3 PR 3.4 close-out (TODO-WP3-3.4-doc-scrub retired).  Any reintroduction
# of `clear_per_frame` in the codebase fires this check.
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

# ── 5. ExecutionPlanCache / plan_cache (PR-0.0 + PR-0.1 + WP-2 PR 2.5 fix) ─
# WP-2 PR 2.5 closed: the test file
# tests/render_graph/executor/test_scheduler_determinism.cpp was migrated
# off ExecutionPlanCache* (retired per PR-2 rewire / CHANGELOG.md R6) onto
# the CompiledFrameGraph path (FrameGraphCompiler + the 4-arg
# GraphExecutor::execute()).  The TODO-WP2-scheduler-det allowlist is gone.
# See strip_comments docstring (top of script) for the pipefail-safe
# `[ -n "$hits" ]` pattern required here.
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
# WP-5 close-out dropped the cached `m_instance_key` member field; the
# owner key is now derived per-call via `instance_key(ctx)` from the
# executing node's `current_identity`.  Historical contrast references
# to the legacy one-key-per-composition pattern were scrubbed from
# `precomp_node_execute.cpp` (TODO-WP4-stable-node-identity retired).
# Any reintroduction of `.graph = hash_string(m_comp_name)` fires this check.
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
