#!/usr/bin/env bash
# tools/check_architecture_boundaries.sh
# ─────────────────────────────────────────────────────────────────────
# PR-0 — Temporary architecture boundary grep checks (4 total).
#
# Verifies that headers removed in prior refactors (TICKET-011 render-
# session split, render-runtime-resources extraction, cache-state
# removal) have not accidentally been re-introduced via #include or
# direct reference in source files.  Also enforces the retirement of
# legacy reset APIs from WP-3 close-out.
#
# These checks are intentionally simple grep-based assertions.  When
# the architecture stabilises further, they can be graduated into
# tools/test_architectural.sh or removed once the risk of drift is zero.
#
# Wired into:
#   - CI:       .github/workflows/gates.yml (Gate 5 / architecture-check)
#   - CMake:    chronon3d_architecture_check (root CMakeLists.txt)
#   - CTest:    registered as architecture_boundaries_ci test
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

FAILED=0

echo "=== Architecture boundary grep checks ==="

# ── 1. core/memory/render_session.hpp ─────────────────────────────────
# This header was split into runtime/render_session.hpp (common part) +
# backends/software/software_session_resources.hpp (backend part) during
# TICKET-011 (render-session split).  The old path must NEVER appear in
# an #include directive or as a direct header reference.
echo -n "  [1/3] core/memory/render_session.hpp  ... "
if grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include.*core/memory/render_session\.hpp' include src tests 2>/dev/null; then
    echo "FAIL"
    FAILED=1
else
    echo "PASS"
fi

# ── 2. renderer_runtime_resources.hpp ─────────────────────────────────
# RendererRuntimeResources was eliminated in TICKET-009 / TICKET-011.
# Its contents (ExecutionPlanCache, GraphExecutor, SoftwareRegistry,
# GraphNodeCatalog, EffectCatalog, ExecutionScheduler) now live on
# runtime::RenderRuntime.  The old header must never be included.
echo -n "  [2/3] renderer_runtime_resources.hpp   ... "
if grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include.*renderer_runtime_resources\.hpp' include src tests 2>/dev/null; then
    echo "FAIL"
    FAILED=1
else
    echo "PASS"
fi

# ── 3. renderer_cache_state.hpp ───────────────────────────────────────
# RendererCacheState was eliminated in TICKET-011.  Its contents
# (NodeCache, FramebufferPool, CompiledGraphCache) now live on
# runtime::RenderRuntime.  The old header must never be included.
echo -n "  [3/4] renderer_cache_state.hpp         ... "
if grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include.*renderer_cache_state\.hpp' include src tests 2>/dev/null; then
    echo "FAIL"
    FAILED=1
else
    echo "PASS"
fi

# ── 4. clear_per_frame() method (WP-3 PR 3.4 close-out) ────────────────
# The legacy full-reset shim on RenderSession was RETIRED in WP-3 PR 3.4
# close-out.  Callers must migrate to `reset_frame_temporaries()`
# (frame-scoped) or `reset_job()` (full reset).  This guard prevents
# silent source-code reintroduction across include/, src/, tests/, apps/.
# Note: docs/ is intentionally NOT searched (historical references in
# the WP-3 roadmap doc are allowed).
echo -n "  [4/4] legacy full-reset shim RETIRED   ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E 'clear_per_frame' include src tests apps 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "FAIL"
    echo "$hits" | sed 's/^/    /'
    FAILED=1
else
    echo "PASS"
fi

# ── Final summary ─────────────────────────────────────────────────────
echo ""
if [ "$FAILED" -ne 0 ]; then
    echo "=== Architecture boundary checks FAILED! ==="
    exit 1
else
    echo "=== All architecture boundary checks PASSED! ==="
    exit 0
fi
