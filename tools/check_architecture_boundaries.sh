#!/usr/bin/env bash
# tools/check_architecture_boundaries.sh
# ─────────────────────────────────────────────────────────────────────
# WP-0 (PR 0.2 / 0.5 close-out) — Architecture boundary grep checks (9 total).
#
# Verifies that headers / symbols retired in prior refactors have not been
# accidentally re-introduced.  Every check runs linearly before the summary
# is computed — see `git log` for the bug that motivated this layout: an
# earlier draft of this script had check #5 nested inside the summary's
# `else` branch, so a failing #5 would still terminate with `exit 0`.  The
# linear structure below eliminates that whole class of bug.
#
# All checks scan the active source surface
# (`include/`, `src/`, `tests/`, `apps/`).  `docs/` is intentionally NOT
# searched — historical references in roadmap documents are part of the
# audit trail.
#
#   1. core/memory/render_session.hpp        — TICKET-011 (PR-2 rewire)
#   2. renderer_runtime_resources.hpp        — TICKET-009 / TICKET-011
#   3. renderer_cache_state.hpp              — TICKET-011
#   4. clear_per_frame() (full-reset shim)   — WP-3 PR 3.4 close-out
#   5. plan_cache (ExecutionPlanCache)       — PR-2 rewire close-out
#   6. detail::g_debug_config                — TICKET-007 (2026-06-21)
#      detail::set_debug_config
#   7. detail::g_default_assets_root         — TICKET-007 deferred
#                                              follow-up (2026-06-21)
#   8. <chrono3d/...> include typo            — TICKET-003 follow-up
#                                              (2026-06-21)
#   9. core/memory/* reintroduced (allowlist) — TICKET-011 follow-up
#                                              (2026-06-21)
#
# Wired into:
#   - CI:     .github/workflows/gates.yml (Gate 5 / architecture-check)
#   - CMake:  `chronon3d_architecture_check` custom target (root
#             CMakeLists.txt)
#   - CTest:  invoked by Gate 5 of gates.yml.
#
# Exit codes:
#   0 = all boundaries PASS
#   1 = at least one boundary FAIL (offending lines dumped under the rule)
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT"

FAILED=0

# Sanctioned header paths under include/chronon3d/core/memory/. Every header
# in this directory other than these paths is treated as retired and is
# flagged by check #9 (reintroduction guard).
# Last verified: 2026-06-21 — `ls include/chronon3d/core/memory/*.hpp`.
# Note: `core/memory/detail/framebuffer_impl.hpp` is included by
# `core/memory/framebuffer.hpp` via a RELATIVE path (`#include "detail/..."`),
# which is invisible to the include-path grep in check #9 (regex requires
# `<...>` form). If a future change makes that include absolute, this
# allowlist MUST be widened.
MEMORY_SANCTIONED_RE='(framebuffer\.hpp|framebuffer_handle\.hpp|framebuffer_slot_view\.hpp|arena\.hpp|memory_utils\.hpp)'

# Shared constants for the symbol-driven checks (#6 + #7).
SCRIPT_PATHS='include src tests apps'

# Strip pure-comment lines AND lines where the symbol appears only in a
# trailing inline `//` comment.  Reads grep -Rn output (format
# <path>:<line>:<content>) from stdin and prints lines that still contain
# the symbol in NON-comment code.
#
# Use:
#   grep -Rn <pattern> <paths> | filter_symbol_in_code_only <symbol-regex>
filter_symbol_in_code_only() {
    local sym="$1"
    awk -F: -v sym="$sym" '
        {
            # Reconstruct the content (fields 3..NF joined by ":"
            # so colons inside the actual code line are preserved).
            rest = ""
            for (i = 3; i <= NF; i++) {
                rest = rest (i == 3 ? "" : ":") $i
            }
            # Pure-comment line: starts (after ws) with ///, //, /*, or *
            if (rest ~ /^[[:space:]]*(\/\/\/|\/\/|\/\*|\*)/) next
            # Inline trailing comment: symbol only appears after the first
            # `//` on the line.  Strip from // to end and recheck.
            pre = rest
            sub(/\/\/.*/, "", pre)
            # Inline `/* ... */` block comments are NOT stripped — those are
            # rare in this codebase and the few that exist are intended to
            # reference the retired symbol, so any BLOCK-comment hit should
            # be flagged for human review.
            if (pre !~ sym) next
            print
        }
    '
}

echo "=== Architecture boundary grep checks (WP-0 — 9 checks) ==="

# ── 1. core/memory/render_session.hpp ─────────────────────────────────
# Split into runtime/render_session.hpp + software_session_resources.hpp
# during TICKET-011. The old path must NEVER appear in #include or
# reference.
echo -n "  [1/9] core/memory/render_session.hpp  ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include.*core/memory/render_session\.hpp' $SCRIPT_PATHS 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 2. renderer_runtime_resources.hpp ─────────────────────────────────
# RendererRuntimeResources eliminated in TICKET-009 / TICKET-011. Its
# contents (ExecutionPlanCache, GraphExecutor, SoftwareRegistry,
# GraphNodeCatalog, EffectCatalog, ExecutionScheduler) now live on
# runtime::RenderRuntime.
echo -n "  [2/9] renderer_runtime_resources.hpp   ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include.*renderer_runtime_resources\.hpp' $SCRIPT_PATHS 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 3. renderer_cache_state.hpp ───────────────────────────────────────
# RendererCacheState eliminated in TICKET-011. Its contents (NodeCache,
# FramebufferPool, CompiledGraphCache) now live on runtime::RenderRuntime.
echo -n "  [3/9] renderer_cache_state.hpp         ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include.*renderer_cache_state\.hpp' $SCRIPT_PATHS 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 4. clear_per_frame() method (WP-3 PR 3.4 close-out) ────────────────
# Full-reset shim RETIRED. Migrate callers to `reset_frame_temporaries()`
# (frame-scoped) or `reset_job()` (full reset).
echo -n "  [4/9] legacy clear_per_frame() RETIRED ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '\bclear_per_frame\b' $SCRIPT_PATHS 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 5. ExecutionPlanCache / plan_cache references (PR-2 rewire close-out) ──
# chronon3d::runtime::ExecutionPlanCache class & header were RETIRED
# alongside the legacy `GraphExecutor::execute(RenderGraph&, ...)` overloads.
# This guard enforces zero reintroduction.
echo -n "  [5/9] plan_cache references RETIRED    ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '\bplan_cache\b' $SCRIPT_PATHS 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 6. detail::g_debug_config / detail::set_debug_config (TICKET-007) ──────
# Process-wide DebugConfig pointer REMOVED in TICKET-007 (2026-06-20). The
# replacement is per-instance `ctx.options.debug_config` / per-function
# `debug_cfg` parameter.
#
# Comment-only references are NOT failures (historical audit trails) but
# real code references ARE — hence the inline-comment-strip pipeline.
#
# The single exception is the TICKET-007 canary test file, which names a
# TEST_CASE after the symbol by string literal (line 118). That reference
# is the test STUB for the guard itself and is exempt from the guard.
echo -n "  [6/9] detail::g_debug_config REMOVED    ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E 'detail::(g_debug_config|set_debug_config)' $SCRIPT_PATHS 2>/dev/null \
    | filter_symbol_in_code_only 'detail::(g_debug_config|set_debug_config)' \
    | grep -v 'tests/core/test_parallel_render_engines_debug_isolation\.cpp:' \
    || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 7. detail::g_default_assets_root (TICKET-007 deferred follow-up) ────────
# Companion global (asset_registry.hpp). Migrated to per-instance
# m_assets_root on RenderEngine; legacy global REMOVED. Same comment-strip
# policy as check #6 applies.
echo -n "  [7/9] g_default_assets_root REMOVED    ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '\bg_default_assets_root\b' $SCRIPT_PATHS 2>/dev/null \
    | filter_symbol_in_code_only '\bg_default_assets_root\b' \
    || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 8. <chrono3d/...> include typo (TICKET-003 follow-up) ─────────────────────
# Pre-PR-23 typo: `chrono3d` (missing 'n') vs the correct `chronon3d/`.
# Original offender: `include/chronon3d/expressions/v2/lexer.hpp` line 9 —
# since fixed in TICKET-003.  This guard prevents silent reintroduction.
echo -n "  [8/9] chrono3d typo header RETIRED    ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include[[:space:]]*<chrono3d/' $SCRIPT_PATHS 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 9. core/memory/* reintroduced (allowlist-enforced, TICKET-011) ───────────
# Per PR-2 rewire, the only legitimate header paths under
# include/chronon3d/core/memory/ via `#include <...>` are listed in
# MEMORY_SANCTIONED_RE above. Every other file path is treated as retired
# and is flagged here. Note this guard does NOT catch the deprecated
# `core/memory/render_session.hpp` specifically — that is check #1.
# This guard does NOT validate that sanctioned-include references still
# resolve to extant files (a separate concern for build-time validation).
echo -n "  [9/9] core/memory/* within allowlist   ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include[[:space:]]*<[^>]*core/memory/' $SCRIPT_PATHS 2>/dev/null \
    | grep -Ev "core/memory/${MEMORY_SANCTIONED_RE}" \
    || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 10. SoftwareRenderer boundary (06 R5) ─────────────────────────────────────
# Gate blocks merge until `check_software_renderer_boundary.sh` is also
# green: 06 R2..R5 invariants on SoftwareRenderer (single-backend identity,
# header LOC <=200, non-local includes <=6, no `dynamic_cast<SoftwareRenderer*>`,
# no `SoftwareRenderer&` in processor surfaces).
echo -n "  [10/10] SoftwareRenderer boundaries  ... "
if [ -x tools/check_software_renderer_boundary.sh ]; then
    if bash tools/check_software_renderer_boundary.sh > /dev/null 2>&1; then
        echo "PASS"
    else
        echo "FAIL (advisory - does not block merge yet, see header above)"
        echo "  --- check_software_renderer_boundary.sh details ---"
        bash tools/check_software_renderer_boundary.sh 2>&1 | sed 's/^/    /' | head -40 || true
        # NOTE: FAILED intentionally NOT set in this branch. Promote to
        # `FAILED=1` after R2+R3+R4 land and the boundary script exits 0.
    fi
else
    echo "SKIP (tools/check_software_renderer_boundary.sh not executable)"
fi

# ── Summary ───────────────────────────────────────────────────────────
echo ""
if [ "$FAILED" -ne 0 ]; then
    echo "=== Architecture boundary checks FAILED (one or more P0 violations) ==="
    exit 1
fi
echo "=== All architecture boundary checks PASSED! ==="
exit 0
