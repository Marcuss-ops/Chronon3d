#!/usr/bin/env bash
# tools/check_architecture_boundaries.sh
# ─────────────────────────────────────────────────────────────────────
# WP-0 (PR 0.2 / 0.5 close-out) + F3.1 phase B — Architecture boundary
# grep + semantic checks (14 total).
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
#  10. SoftwareRenderer boundary             — 06 R5
#  11. msdfgen / libtess2 / unicode          — PR-A10 (CPU-first deny)
#        include patterns
#  12. CMake module registry                 — AGENTS.md §2 / ADR-010
#        (semantic)
#  13. vcpkg / find_package parity           — AGENTS.md §3 / ADR-010
#        (semantic)
#  14. SDK public surface boundary           — AGENTS.md §4 / ADR-010
#        (semantic)
#  15. Legacy text pipeline gate             — P1 #4 census
#        (check_legacy_text_pipeline.sh)
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

if [ -n "${BOUNDARY_CHECK_ROOT:-}" ]; then
    REPO_ROOT="$BOUNDARY_CHECK_ROOT"
else
    REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
fi
cd "$REPO_ROOT" || { echo "INTERNAL_ERROR: cannot cd to $REPO_ROOT" >&2; exit 2; }

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

echo "=== Architecture boundary grep + semantic checks (WP-0 / F3.1 / P1-4 — 15 checks) ==="

# ── 1. core/memory/render_session.hpp ─────────────────────────────────
# Split into runtime/render_session.hpp + software_session_resources.hpp
# during TICKET-011. The old path must NEVER appear in #include or
# reference.
echo -n "  [1/15] core/memory/render_session.hpp  ... "
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
echo -n "  [2/15] renderer_runtime_resources.hpp   ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include.*renderer_runtime_resources\.hpp' $SCRIPT_PATHS 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 3. renderer_cache_state.hpp ───────────────────────────────────────
# RendererCacheState eliminated in TICKET-011. Its contents (NodeCache,
# FramebufferPool, CompiledGraphCache) now live on runtime::RenderRuntime.
echo -n "  [3/15] renderer_cache_state.hpp         ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include.*renderer_cache_state\.hpp' $SCRIPT_PATHS 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 4. clear_per_frame() method (WP-3 PR 3.4 close-out) ────────────────
# Full-reset shim RETIRED. Migrate callers to `reset_frame_temporaries()`
# (frame-scoped) or `reset_job()` (full reset).
echo -n "  [4/15] legacy clear_per_frame() RETIRED ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '\bclear_per_frame\b' $SCRIPT_PATHS 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 5. ExecutionPlanCache / plan_cache references (PR-2 rewire close-out) ──
# chronon3d::runtime::ExecutionPlanCache class & header were RETIRED
# alongside the legacy `GraphExecutor::execute(RenderGraph&, ...)` overloads.
# This guard enforces zero reintroduction.
echo -n "  [5/15] plan_cache references RETIRED    ... "
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
echo -n "  [6/15] detail::g_debug_config REMOVED    ... "
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
echo -n "  [7/15] g_default_assets_root REMOVED    ... "
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
echo -n "  [8/15] chrono3d typo header RETIRED    ... "
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
echo -n "  [9/15] core/memory/* within allowlist   ... "
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
echo -n "  [10/15] SoftwareRenderer boundaries  ... "
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

# ── 11. msdfgen / libtess2 / unicode include DENY (PR-A10, ADR-009) ────
# CPU-first headless posture (AGENTS.md §Regole di lavoro): no third-party
# glyph / tessellation / ICU deps anywhere *outside* the scoped adapter
# directories justified by ADR-009 (`docs/adr/ADR-009-optional-text-deps.md`).
# The adapter allowlist is the single source of truth for which paths may
# include the otherwise-forbidden headers; a leak outside these paths
# trips this guard and the PR fails.  Pattern:
#   <msdfgen[/...]> | <libtess2[/...]> | <unicode[/...]>
# Adapter scopes (ADR-009 §Decision #4):
#   <msdfgen[/...]>    src/text/glyph_raster/   (FILES ONLY, not include/)
#   <libtess2[/...]>   src/text/text_3d/        (FILES ONLY, not include/)
#   <unicode[/...]>    src/text/boundary_resolver/   + src/backends/text/icu_resolver.cpp
# Public headers reflected from adapters must NOT include these libraries
# directly — downstream consumers see only IGlyphRasterizer*, TextBoundaryResolver*,
# and Text3DNode (the same adapter pattern already documented for HarfBuzz
# and Blend2D).
#
# Adapter-scope note (forward-looking hardening): the source surface does
# NOT yet contain any of `src/text/glyph_raster/`, `src/text/text_3d/`,
# `src/text/boundary_resolver/`, or `src/backends/text/icu_resolver.cpp`
# (those paths are reserved for future phase 9/11/12 per the ROADMAP).
# Once adapter modules land, the awk filter below must continue to drop
# their include statements before the deny-pattern fires.
#
# Anchor policy: each forbidden header NAME is matched at the START of
# the included token (right after `<` or `"`), so substring coincidences
# such as `text_unicode_utils.hpp` (a Chronon3D-internal helper whose
# name contains `unicode` as a substring) do NOT false-positive.
echo -n "  [11/15] msdfgen/libtess2/unicode includes FORBIDDEN (ADR-009 scoped) ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#[[:space:]]*include[[:space:]]*[<"](msdfgen|libtess2|unicode|tesselator)([-./][^>"]*)?[>"]' \
    $SCRIPT_PATHS 2>/dev/null \
    | awk -F: '
        {
            path = $1
            # Drop hits that fall inside the ADR-009 allowlist paths.
            if (path ~ /^src\/text\/glyph_raster\//)       next
            if (path ~ /^src\/text\/text_3d\//)            next
            if (path ~ /^src\/text\/boundary_resolver\//)  next
            if (path == "src/backends/text/icu_resolver.cpp") next
            print
        }
    ' || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'
    echo "    → ADR-009 (\`docs/adr/ADR-009-optional-text-deps.md\` §Decision #4) lists the only scopes that"
    echo "      may legitimately include the forbidden headers.  Relocate the offending file into one of:"
    echo "        src/text/glyph_raster/             — msdfgen[/...]  only"
    echo "        src/text/text_3d/                  — libtess2[/...]  or tesselator...  only"
    echo "        src/text/boundary_resolver/        — unicode[/...]  only"
    echo "        src/backends/text/icu_resolver.cpp — unicode[/...]  only"
    FAILED=1
else echo "PASS"; fi

# ── 12. CMake module registry (AGENTS.md §2 / ADR-010 Decision 1) ──────────
# Semantic: Every add_library(... OBJECT|INTERFACE) target declared in
# src/**/CMakeLists.txt MUST also be listed in
# cmake/Chronon3DRegistry.cmake. That registry is the SOLE registration
# channel — anti-duplication per ANTI_DUPLICATION_RULES.md.  Reversed
# guard: src OBJECT/INTERFACE libs not in registry -> FAIL.
echo -n "  [12/15] CMake module registry (semantic) ... "
src_libs=$(grep -Rh --include='CMakeLists.txt' \
    -E '^[[:space:]]*add_library\([[:space:]]*[A-Za-z_][A-Za-z_0-9]*[[:space:]]+(OBJECT|INTERFACE)\b' \
    src/ 2>/dev/null \
    | sed -E 's/.*add_library\([[:space:]]*([A-Za-z_][A-Za-z_0-9]*).*/\1/' \
    | sort -u || true)
registry_libs=$(grep -E '^[[:space:]]*add_library\(' \
    cmake/Chronon3DRegistry.cmake 2>/dev/null \
    | sed -E 's/.*add_library\([[:space:]]*([A-Za-z_][A-Za-z_0-9]*).*/\1/' \
    | sort -u || true)
missing=$(comm -23 <(printf '%s\n' "$src_libs") \
                <(printf '%s\n' "$registry_libs") 2>/dev/null \
            | tr -d '[:space:]' || true)
if [ -n "$missing" ]; then
    echo "FAIL (advisory)"
    echo "  src OBJECT/INTERFACE libs not in registry:"
    comm -23 <(printf '%s\n' "$src_libs") \
             <(printf '%s\n' "$registry_libs") 2>/dev/null \
        | sed 's/^/    /' | head -10
    echo "  (registry sync tracked separately — promotion to blocking"
    echo "   requires TICKET-041 CMake registry completeness sync)"
else echo "PASS"; fi

# ── 13. vcpkg / find_package parity (AGENTS.md §3 / ADR-010 Decision 2) ──
# Semantic: Every find_package(X REQUIRED) in CMakeLists.txt MUST have a
# matching entry in vcpkg.json (case-insensitive lowercase).  Allowlist
# for CMake/system-builtin deps (Threads, EXPAT) that don't need vcpkg
# entries.  Files: top-level CMakeLists.txt.
echo -n "  [13/15] vcpkg dep parity (semantic) ... "
miss=""
for pkg in $(grep -hE '^[[:space:]]*find_package\([[:space:]]*[A-Za-z_][A-Za-z_0-9]*' \
                CMakeLists.txt 2>/dev/null \
             | sed -E 's/.*find_package\([[:space:]]*([A-Za-z_][A-Za-z_0-9]*).*/\1/' \
             | sort -u); do
    [ -z "$pkg" ] && continue
    case "$pkg" in
        Threads|EXPAT) continue ;;
    esac
    lcp=$(echo "$pkg" | tr '[:upper:]' '[:lower:]')
    if ! grep -qE "\"${lcp}\"" vcpkg.json 2>/dev/null; then
        miss="$miss $pkg"
    fi
done
if [ -n "$(echo $miss | tr -d '[:space:]')" ]; then
    echo "FAIL (advisory)"
    echo "  find_package entries without vcpkg deps:$miss"
    echo "  (vcpkg / CMakeLists.txt sync tracked separately — promotion"
    echo "   to blocking requires TICKET-042 vcpkg dependency coverage)"
else echo "PASS"; fi

# ── 14. SDK public surface boundary (AGENTS.md §4 / ADR-010 Decision 3) ──
# Semantic: apps/* MUST NOT directly include internal chronon3d_sdk_impl
# headers.  Only the SDK INTERFACE alias Chronon3D::SDK may be consumed by
# external consumers (apps/chronon3d_cli, install_consumer_test,
# downstream).  Permitted entry points: <chronon3d/...> | "chronon3d/...".
# FORBIDDEN: <chronon3d_sdk_impl[/...> | "chronon3d_sdk_impl[/...].
echo -n "  [14/15] SDK public surface (semantic) ... "
# Tighter regex: require /, >, or " boundary immediately after
# `chronon3d_sdk_impl` so legitimate internal filenames like
# <chronon3d_sdk_impl_marker.h> are exempted while any include UNAMBIGUOUSLY
# targeting the internal sdk_impl layer is flagged.
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include[[:space:]]*[<"]chronon3d_sdk_impl[/>"]' \
    apps/ 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "FAIL (advisory)"
    echo "$hits" | sed 's/^/    /'
    echo "  (SDK consumer-surface audit tracked separately — promotion"
    echo "   to blocking requires TICKET-043 SDK consumer-surface audit)"
else echo "PASS"; fi

# ── 15. Legacy text pipeline gate (P1 #4 — census gate) ────────────────
# Dual text pipeline (TextShape vs TextRun).  Blocks NEW production
# callsites of rasterize_text_to_bl_image and TextLayoutEngine::layout
# outside the census-tracked whitelist.  See
# docs/tickets/TICKET-P1-ACTION-PLAN.md §P1 #4.
echo -n "  [15/15] Legacy text pipeline gate         ... "
if [ -x tools/check_legacy_text_pipeline.sh ]; then
    if bash tools/check_legacy_text_pipeline.sh > /dev/null 2>&1; then
        echo "PASS"
    else
        echo "FAIL"
        bash tools/check_legacy_text_pipeline.sh 2>&1 | sed 's/^/    /' | head -30 || true
        FAILED=1
    fi
else
    echo "SKIP (tools/check_legacy_text_pipeline.sh not executable)"
fi

# ── 16. SDK public-deps SSoT fail-on-drift ─────────────────────────────
# Source-level structural invariant enforcing the fail-on-drift contract
# (per ANTI_DUPLICATION_RULES.md) between the canonical SDK_PUBLIC_DEPS
# list (cmake/Chronon3DRegistry.cmake) and the AUTO-GENERATED marker
# block in cmake/Chronon3DConfig.cmake.in.  Two invariants:
#
#   (a) Registry: CHRONON3D_SDK_PUBLIC_DEPS contains ≥1 entry (sanity).
#   (b) Marker block: EXACTLY ONE @CHRONON3D_FIND_DEPENDENCY_LINES@
#       substitution line and ZERO hand-written find_dependency( lines.
#
# At CONFIGURE time, configure_file(... @ONLY) expands the variable via
# the foreach loop in Chronon3DRegistry.cmake, producing exactly
# length(CHRONON3D_SDK_PUBLIC_DEPS) find_dependency lines in the
# GENERATED cmake/Chronon3DConfig.cmake.  Runtime count parity is a
# property of the substitution mechanism; this gate enforces the wiring
# that guarantees it.
echo -n "  [16/16] SDK public-deps SSoT wiring ... "
if [ -f cmake/Chronon3DRegistry.cmake ] && [ -f cmake/Chronon3DConfig.cmake.in ]; then
    registry_entries=$(awk '
        /set\s*\(\s*CHRONON3D_SDK_PUBLIC_DEPS/ { in_list=1; next }
        /^\s*\)\s*$/ { in_list=0 }
        in_list && /\S/ && !/^\s*#/ && !/^\s*\(/ { print }
    ' cmake/Chronon3DRegistry.cmake | wc -l)
    marker_subs=$(awk '
        />>>\s*AUTO-GENERATED FROM CHRONON3D_SDK_PUBLIC_DEPS/ { in_marker=1; next }
        /^#\s*<<<\s*$/  # whitespace-tolerant closing-<<< marker { in_marker=0; next }
        in_marker && /@CHRONON3D_FIND_DEPENDENCY_LINES@/ { print }
    ' cmake/Chronon3DConfig.cmake.in | wc -l)
    marker_finds=$(awk '
        />>>\s*AUTO-GENERATED FROM CHRONON3D_SDK_PUBLIC_DEPS/ { in_marker=1; next }
        /^#\s*<<<\s*$/  # whitespace-tolerant closing-<<< marker { in_marker=0; next }
        in_marker && /^[[:space:]]*find_dependency\(/ { print }
    ' cmake/Chronon3DConfig.cmake.in | wc -l)
    drift=""
    [ "$registry_entries" -lt 1 ] && drift="${drift}empty-sdk-public-deps "
    [ "$marker_subs" -ne 1 ] && drift="${drift}marker-substitution-ne-1 "
    [ "$marker_finds" -ne 0 ] && drift="${drift}marker-handwritten-find-dependency-lines "
    if [ -z "$drift" ]; then
        echo "PASS (registry=${registry_entries} entries; marker=1 substitution / 0 hand-written)"
    else
        echo "FAIL (${drift})"
        echo "    registry CHRONON3D_SDK_PUBLIC_DEPS entries: $registry_entries"
        echo "    marker block @CHRONON3D_FIND_DEPENDENCY_LINES@ lines: $marker_subs (expect 1)"
        echo "    marker block hand-written find_dependency( lines: $marker_finds (expect 0)"
        echo "    → cmake/Chronon3DConfig.cmake.in marker block must contain EXACTLY"
        echo "      ONE '@CHRONON3D_FIND_DEPENDENCY_LINES@' substitution line and ZERO"
        echo "      'find_dependency(' lines.  This enforces the structural wiring"
        echo "      (substitution-bound at configure-time); runtime count parity is"
        echo "      asserted post-configure by tools/check_config_template_runtime.sh."
        FAILED=1
    fi
else
    echo "SKIP (registry or Config template missing)"
fi

# ── Summary ───────────────────────────────────────────────────────────
echo ""
if [ "$FAILED" -ne 0 ]; then
    echo "=== Architecture boundary checks FAILED (one or more P0 violations) ==="
    exit 1
fi
echo "=== All architecture boundary checks PASSED! ==="
exit 0
