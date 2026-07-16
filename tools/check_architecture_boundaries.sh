#!/usr/bin/env bash
# tools/check_architecture_boundaries.sh
# ─────────────────────────────────────────────────────────────────────
# WP-0 (PR 0.2 / 0.5 close-out) + F3.1 phase B + ADR-010 + Phase-A1 + Phase-A3 +
# Phase-A4 — Architecture boundary grep + semantic checks (20 total).
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
#  16. SDK public-deps SSoT wiring           — ADR-010 Decision 3
#  17. src/-only header on public path       — TICKET-ae-cam-hash-collision
#  18. V2 AssetPreflight stubs RETIRED       — Phase A1 close-out
#        (asset_readiness_v2.hpp always-green stubs + accumulate_preflight_result)
#  19. TextSpec::offset RETIRED              — Phase A3 close-out
#        (dual-channel placement pattern: spec.placement.offset is canonical)
#  20. TextFrame consolidated placement      — Phase A4 close-out
#        (TextFrame.{position,placement_kind,offset} → TextPlacement placement)#  21. Asset namespace canonicalization      — Phase A2 close-out
#         (v2::Asset{Kind,Ref,Manifest} → assets::{AssetKind,InternalAssetRef,AssetManifest}
#          + to_v2_ref() RETIRED)
#  24. Single source of truth (SSoT) audit   — Test 12 (8 concepts + 4 specific patterns)
#         sibling tools/check_single_source_of_truth.sh
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

# Cat-3 IO exclusion wrapper (TICKET-PRE-PUSH-IO-EXCLUSIONS).
# Skips vcpkg_installed/ + build/ + .cache/ + node_modules/ from grep -Rn
# scans. These dirs are gitignored but `grep -R` does NOT respect .gitignore
# by default, so unscoped scans become IO-bound on a populated vcpkg_installed
# (hundreds of thousands of headers). Per-gate apply (not central) to keep
# the wrapper local; only this gate's grep calls pick it up.
grep() { command grep --exclude-dir=vcpkg_installed --exclude-dir=build --exclude-dir=.cache --exclude-dir=node_modules "$@"; }

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

echo "=== Architecture boundary grep + semantic checks (WP-0 / F3.1 / P1-4 / Phase-A1 / Phase-A2 / Phase-A3 / Phase-A4 / Phase-A5 / Phase-A6 \u2014 M1.8 §1 — 25 gates) ==="

# ── 1. core/memory/render_session.hpp ─────────────────────────────────
# Split into runtime/render_session.hpp + software_session_resources.hpp
# during TICKET-011. The old path must NEVER appear in #include or
# reference.
echo -n "  [1/24] core/memory/render_session.hpp  ... "
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
echo -n "  [2/24] renderer_runtime_resources.hpp   ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include.*renderer_runtime_resources\.hpp' $SCRIPT_PATHS 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 3. renderer_cache_state.hpp ───────────────────────────────────────
# RendererCacheState eliminated in TICKET-011. Its contents (NodeCache,
# FramebufferPool, CompiledGraphCache) now live on runtime::RenderRuntime.
echo -n "  [3/24] renderer_cache_state.hpp         ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#include.*renderer_cache_state\.hpp' $SCRIPT_PATHS 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 4. clear_per_frame() method (WP-3 PR 3.4 close-out) ────────────────
# Full-reset shim RETIRED. Migrate callers to `reset_frame_temporaries()`
# (frame-scoped) or `reset_job()` (full reset).
echo -n "  [4/24] legacy clear_per_frame() RETIRED ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '\bclear_per_frame\b' $SCRIPT_PATHS 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 5. ExecutionPlanCache / plan_cache references (PR-2 rewire close-out) ──
# chronon3d::runtime::ExecutionPlanCache class & header were RETIRED
# alongside the legacy `GraphExecutor::execute(RenderGraph&, ...)` overloads.
# This guard enforces zero reintroduction.
echo -n "  [5/24] plan_cache references RETIRED    ... "
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
echo -n "  [6/24] detail::g_debug_config REMOVED    ... "
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
echo -n "  [7/24] g_default_assets_root REMOVED    ... "
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
echo -n "  [8/24] chrono3d typo header RETIRED    ... "
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
echo -n "  [9/24] core/memory/* within allowlist   ... "
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
echo -n "  [10/24] SoftwareRenderer boundaries  ... "
if [ -x tools/check_software_renderer_boundary.sh ]; then
    if bash tools/check_software_renderer_boundary.sh > /dev/null 2>&1; then
        echo "PASS"
    else
        echo "FAIL (advisory - does not block merge yet, see header above)"
        echo "  --- check_software_renderer_boundary.sh details ---"
        bash tools/check_software_renderer_boundary.sh 2>&1 | sed 's/^/    /' | head -60 || true
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
echo -n "  [11/24] msdfgen/libtess2/unicode includes FORBIDDEN (ADR-009 scoped) ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '#[[:space:]]*include[[:space:]]*[<"](msdfgen|libtess2|unicode|tesselator)([-./][^>"]*)?[>"]' \
    $SCRIPT_PATHS 2>/dev/null \
    | grep -v '//.*#include' \
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
# src/**/CMakeLists.txt MUST also be listed in cmake/Chronon3DRegistry.cmake
# (under CHRONON3D_REGISTRY_OBJECT_LIBS or CHRONON3D_REGISTRY_INTERFACE_LIBS).
# That registry is the SOLE registration channel — anti-duplication per
# ANTI_DUPLICATION_RULES.md.  Reversed guard: src OBJECT/INTERFACE libs
# NOT listed in the registry -> FAIL.
#
# Sub-fix (ADR-010 Decision 1 — registry format audit): the previous grep
# looked for `_add_library\(` in cmake/Chronon3DRegistry.cmake but the
# registry uses `set(CHRONON3D_REGISTRY_*_LIBS …)` list blocks per its
# own MAINTENANCE CONTRACT, not add_library() calls.  Parsing is now via
# awk (same idiom as gate #16: POSIX regex, mawk-compatible, ignores
# pure-comment lines + extracts lib names via `print $1`).  Diff invariant:
# `comm -23 src_libs registry_libs` MUST be empty.
echo -n "  [12/24] CMake module registry (semantic) ... "
src_libs=$(grep -Rh --include='CMakeLists.txt' \
    -E '^[[:space:]]*add_library\([[:space:]]*[A-Za-z_][A-Za-z_0-9]*[[:space:]]+(OBJECT|INTERFACE)\b' \
    src/ 2>/dev/null \
    | sed -E 's/.*add_library\([[:space:]]*([A-Za-z_][A-Za-z_0-9]*).*/\1/' \
    | sort -u || true)
# Registry entries live inside `set(CHRONON3D_REGISTRY_(OBJECT|INTERFACE)_LIBS …)`
# blocks — parse via awk, mirroring gate #16 parsing of
# CHRONON3D_SDK_PUBLIC_DEPS. POSIX regex (no GNU \s / \S); mawk-safe
# per AGENTS.md §Regole di lavoro cross-platform portability.
#
# Forward-point (nested-paren brittleness): the closing-paren detector
# `^[[:space:]]*\)$` pops `in_list` on any line that's purely `)`.  Currently
# safe because CHRONON3D_REGISTRY_(OBJECT|INTERFACE)_LIBS blocks contain
# NO nested function calls (verified against the maintenance contract).
# If a future maintainer adds a `string(REPLACE …)` or
# `target_link_libraries(…)` call inside one of these blocks, the gate
# would exit `in_list` prematurely and miss subsequent lib names.
# Fix-forward: switch to a parenthesis-depth counter.
registry_libs=$(awk '
    /set[[:space:]]*\([[:space:]]*CHRONON3D_REGISTRY_(OBJECT|INTERFACE)_LIBS/ { in_list=1; next }
    /^[[:space:]]*\)$/ { in_list=0 }
    in_list && /[^[:space:]]/ && !/^[[:space:]]*#/ { print $1 }
' cmake/Chronon3DRegistry.cmake | sort -u || true)
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
echo -n "  [13/24] vcpkg dep parity (semantic) ... "
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
echo -n "  [14/24] SDK public surface (semantic) ... "
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
echo -n "  [15/24] Legacy text pipeline gate         ... "
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
echo -n "  [16/24] SDK public-deps SSoT wiring ... "
if [ -f cmake/Chronon3DRegistry.cmake ] && [ -f cmake/Chronon3DConfig.cmake.in ]; then
    # Use POSIX character classes (NOT GNU-awk \s / \S) for cross-platform
    # portability: mawk-equivalent (default /usr/bin/awk on Debian/Ubuntu)
    # does NOT support the Perl-style regex escapes \s/\S and silently
    # never enters the awk-block, returning count 0 for an otherwise
    # well-formed file.  [[:space:]] + [^[:space:]] are POSIX ERE
    # equivalents that both mawk AND gawk honor.
    registry_entries=$(awk '
        /set[[:space:]]*\([[:space:]]*CHRONON3D_SDK_PUBLIC_DEPS/ { in_list=1; next }
        /^[[:space:]]*\)[[:space:]]*$/ { in_list=0 }
        in_list && /[^[:space:]]/ && !/^[[:space:]]*#/ && !/^[[:space:]]*\(/ { print }
    ' cmake/Chronon3DRegistry.cmake | wc -l)
    marker_subs=$(awk '
        />>>[[:space:]]*AUTO-GENERATED FROM CHRONON3D_SDK_PUBLIC_DEPS/ { in_marker=1; next }
        /^#[[:space:]]*<<<[[:space:]]*END AUTO-GENERATED BLOCK/ { in_marker=0; next }
        in_marker && /@CHRONON3D_FIND_DEPENDENCY_LINES@/ { print }
    ' cmake/Chronon3DConfig.cmake.in | wc -l)
    marker_finds=$(awk '
        />>>[[:space:]]*AUTO-GENERATED FROM CHRONON3D_SDK_PUBLIC_DEPS/ { in_marker=1; next }
        /^#[[:space:]]*<<<[[:space:]]*END AUTO-GENERATED BLOCK/ { in_marker=0; next }
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

# ── 17. src/-only include via public path (TICKET-ae-cam-hash-collision) ───
# Pre-fix guard for the bug in commit 1184f67c (round-3/4 follow-up):
# a `src/`-only header (per AGENTS.md v0.1 Cat-3 freeze, NOT installed
# with the SDK) must NOT be included via the public include path
# `<chronon3d/...>` which maps to `include/chronon3d/...`.  Catches the
# exact regression pattern: `#include <chronon3d/render_graph/nodes/
# detail/projection_helpers.hpp>` when the file actually lives at
# `src/render_graph/nodes/detail/projection_helpers.hpp`.  Correct
# usage: relative include `#include "detail/projection_helpers.hpp"`
# (or any path that resolves to the `src/`-only header).
#
# Detection: for every `#include <chronon3d/X>` reference, check that
# `include/chronon3d/X` exists on disk.  If not, the include is to a
# `src/`-only header via the public path → FAIL.  Skips:
#   - `#include <chronon3d_sdk_impl/...>` (separate concern, check #14)
#   - lines that are pure comments (the `filter_symbol_in_code_only`
#     pipeline doesn't apply here since `#include` syntax is easy to
#     pattern-match without false positives)
echo -n "  [17/24] src/-only include via public path ... "
hits=""
while IFS= read -r line; do
    [ -z "$line" ] && continue
    # Extract the include path after `<chronon3d/` (strip the `<` prefix
    # and `>` suffix).
    inc_path=$(echo "$line" | sed -E 's|.*<chronon3d/([^>]+)>.*|\1|')
    [ -z "$inc_path" ] && continue
    # Skip sdk_impl (separate concern, check #14).
    case "$inc_path" in
        sdk_impl/*) continue ;;
    esac
    # Only flag when the file lives in src/ (src/-only) but NOT in
    # the public include tree — the exact regression pattern.
    if [ -f "src/$inc_path" ] && [ ! -f "include/chronon3d/$inc_path" ]; then
        hits="${hits}${line}"$'\n'
    fi
done < <(grep -RnE '#include[[:space:]]*<chronon3d/[^>]+>' $SCRIPT_PATHS 2>/dev/null \
    | grep -vE ':[[:space:]]*(//|/\*|\*|///)' \
    | grep -v 'chronon3d_sdk_impl' \
    || true)
if [ -n "$hits" ]; then
    echo "FAIL"
    echo "  src/-only headers included via public path (use relative include instead):"
    echo -n "$hits" | sed 's/^/    /'
    FAILED=1
else echo "PASS"; fi

# ── 18. V2 AssetPreflight stubs RETIRED (Phase A1 close-out, 2026-07-10) ──
# The always-green `chronon3d::assets::v2::AssetPreflightResult` +
# `::AssetPreflightResolver` stubs (in `asset_readiness_v2.hpp`) and the
# bridge accumulator `accumulate_preflight_result(...)` (in
# `legacy_adapters.hpp`) were removed in cleanup Phase A1 because a preflight
# that returns {ok=true, missing=[]} unconditionally is worse than no
# preflight (silently produces FALSE-GREEN on missing assets, violating
# AGENTS §"Renderer non inventa fallback"). The REAL canonical preflight
# lives in `chronon3d::AssetPreflightResolver` in
# `include/chronon3d/assets/asset_preflight_resolver.hpp` (namespace
# `chronon3d::`, NOT `chronon3d::assets::v2`) and is wired into the CLI +
# video exporters + canonical tests. This guard prevents silent
# re-introduction of the always-green stubs.
echo -n "  [18/24] V2 AssetPreflight stubs RETIRED ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '\b(assets::v2::AssetPreflightResult|assets::v2::AssetPreflightResolver|accumulate_preflight_result)\b' \
    $SCRIPT_PATHS 2>/dev/null \
    | filter_symbol_in_code_only '\b(assets::v2::AssetPreflightResult|assets::v2::AssetPreflightResolver|accumulate_preflight_result)\b' \
    || true)
if [ -n "$hits" ]; then
    echo "FAIL"
    echo "  V2 preflight stubs or accumulate_preflight_result re-introduced:"
    echo "$hits" | sed 's/^/    /'
    echo "  → Phase A1 (2026-07-10) removed the always-green"
    echo "    \`chronon3d::assets::v2::AssetPreflightResult\` +"
    echo "    \`::AssetPreflightResolver\` from \`asset_readiness_v2.hpp\` +"
    echo "    \`accumulate_preflight_result\` from \`legacy_adapters.hpp\`."
    echo "    Real canonical preflight: \`chronon3d::AssetPreflightResolver\` in"
    echo "    \`include/chronon3d/assets/asset_preflight_resolver.hpp\` (namespace"
    echo "    \`chronon3d::\`). To re-introduce these symbols you must first update"
    echo "    ADR-016 + TICKET-ASSET-READINESS with a new approved design."
    FAILED=1
else echo "PASS"; fi

# ── 19. TextSpec::offset RETIRED (Phase A3 close-out) ───────────────────────────────────────────────────
# After Phase A3, TextSpec has NO `Vec2 offset{}` field. The pin position
# lives only in `spec.placement.offset` (bundled with the placement kind
# via struct TextPlacement). This gate forbids re-introducing the redundant
# dual-channel representation that confused `.offset = ...` with
# `.position = ...`.
#
# `GlyphSelectorSpec` deliberately has its own `offset` field
# (`animator_property<Vec2>` for an animation phase shift, NOT a pin
# position). Files matching `*glyph_selector*` are explicitly exempted
# so the gate does not false-positive on the AnimationOffset property
# setter chain.
echo -n "  [19/24] TextSpec::offset RETIRED        ... "
# Path filter exempts files touching `GlyphSelectorSpec::offset`
# (animator property for phase shift, NOT a pin position):
#   * src/text/glyph_selector_compile.cpp  (compile path)
# `test_*select*` paths are deliberately NOT exempted here — any test
# using spec.offset on a non-GlyphSelectorSpec type should be migrated.
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '\bspec\.offset\b' $SCRIPT_PATHS 2>/dev/null \
    | grep -v 'glyph_selector' \
    | filter_symbol_in_code_only '\bspec\.offset\b' \
    || true)
if [ -n "$hits" ]; then
    echo "FAIL"
    echo "  TextSpec::offset re-introduced (Phase A3 closed it):"
    echo "$hits" | sed 's/^/    /'
    echo "  → Migrate to \`spec.placement.offset\` (bundled with"
    echo "    TextPlacement.kind). GlyphSelectorSpec::offset is exempt"
    echo "    (animation phase shift)."
    FAILED=1
else echo "PASS"; fi

# ── 20. TextFrame consolidated placement (Phase A4 close-out) ──────────────────────────────────────────
# After Phase A4 the TextFrame triple of `Vec3 position` + `TextPlacementKind
# placement_kind` + `Vec2 offset` is replaced by a single `TextPlacement
# placement` field bundling kind + offset. The render pipeline was always 2D
# (resolve_text_placement() ignores frame.position.z). This gate forbids the
# re-introduction of the desync-prone triple representation.
#
# Scope: any structural read or write of `frame.position`, `frame.placement_kind`
# or `frame.offset` in include/ src/ tests/ apps/ — comment-only mentions are
# stripped by `filter_symbol_in_code_only` (same as gates #6/#7/#18/#19).
echo -n "  [20/24] TextFrame consolidated           ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '\bframe\.(position|placement_kind|offset)\b' $SCRIPT_PATHS 2>/dev/null \
    | filter_symbol_in_code_only '\bframe\.(position|placement_kind|offset)\b' \
    || true)
if [ -n "$hits" ]; then
    echo "FAIL"
    echo "  TextFrame placement triple re-introduced (Phase A4 closed it):"
    echo "$hits" | sed 's/^/    /'
    echo "  → Migrate to \`frame.placement\` (TextPlacement struct:"
    echo "    kind + offset bundled). The z component of the old Vec3 position"
    echo "    was never consumed by resolve_text_placement() and is dropped."
    FAILED=1
else echo "PASS"; fi

# ── 22. TextEffects ELIMINATED (Phase A5 close-out) ──
# Phase A5 dropped the duplicate `chronon3d::TextEffects` struct that lived
# on `TextDefinition` (Phase A.3 introduced it as a "post-compositor
# decorator surface" — but the renderer never consumed it; `def.effects.*`
# was silently a no-op).  All glow/bevel effect authority now lives on
# `TextDefinition.style.material` (the canonical `TextMaterial` seam).
#
# This gate forbids re-introduction of:
#   1. The struct declaration `struct TextEffects` (struct-keyword guard).
#   2. The field access `def.effects.*` on `TextDefinition` instances.
#
# Both are mechanical reintroduction patterns; structural-parity is locked
# by the regression test in tests/text/test_text_definition.cpp Group 17
# (`Phase A5 — TextEffects ELIMINATED + TextMaterial canonical seam`).
# Future edits that need a PostTextMaterial seam must update ADR + open a
# new ticket — not silently re-add the deleted struct.
echo -n "  [22/24] TextEffects ELIMINATED       ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '\bstruct\s+TextEffects\b|\bdef\.effects\.' $SCRIPT_PATHS 2>/dev/null \
    | filter_symbol_in_code_only '\bstruct\s+TextEffects\b|\bdef\.effects\.' \
    || true)
if [ -n "$hits" ]; then
    echo "FAIL"
    echo "  TextEffects struct or def.effects.* field re-introduced:"
    echo "$hits" | sed 's/^/    /'
    echo "  → Phase A5 (2026-07-10) eliminated the duplicate"
    echo "    \`chronon3d::TextEffects\` struct + \`TextDefinition::effects\`"
    echo "    field + all consumers (\`content/text/text_helpers_centered.hpp\`"
    echo "    \`glow_text()\`, \`tests/text/test_text_definition.cpp\` Group 4"
    echo "    Phase A5 migration).  Canonical seam: "
    echo "    \`TextDefinition.style.material.{glow_*, bevel_*}\` (TextMaterial"
    echo "    is the SINGLE compositor surface; no duplicate authority).  See"
    echo "    \`docs/CHANGELOG.md\` Phase A5 entry + Group 17 structural-parity"
    echo "    test in \`tests/text/test_text_definition.cpp\`.  To re-introduce"
    echo "    a PostTextMaterial seam you MUST update ADR + open a new ticket."
    FAILED=1
else echo "PASS"; fi

# ── 23. TextPlacementResolver wrapper header RETIRED (Phase A6 close-out) ──
# Phase A6 (2026-07-10) collapsed the two competing text placement
# resolution surfaces onto a single canonical one.  The free function
# `resolve_text_placement()` (in `resolve_text_placement.hpp`) won;
# the `class TextPlacementResolver` thin shim that wrapped it (and
# its co-resident header `text_placement_resolver.hpp`) is RETIRED.
#
# This guard enforces BOTH:
#   (a) Zero re-introduction of the wrapper header filename
#       (matches the user's directive: "vieta l'include dell'header del
#        wrapper rimosso da `include/chronon3d/`").
#   (b) Zero re-introduction of the retired wrapper CLASS in
#       `include/chronon3d/` source code (the body had to live in
#       `include/chronon3d/` per AGENTS.md v0.1 cat-3 freeze on
#       public-surface scope; a re-introduction would re-expose an
#       API surface that the Phase A6 close-out retired).
#
# The free function `resolve_text_placement(...)` is the canonical
# surface (per ADR-019 Decision 3); the class surface is permanently
# retired.  If a future need arises for a stateful / cache-leased
# resolver surface, it must go through an ADR + an explicit new
# ticket (NOT silently re-add the deleted class to a re-imported
# `text_placement_resolver.hpp`).
#
# Scope: the source surface under `include/chronon3d/` — matches the
# user directive verbatim ("vieta l'include dell'header del wrapper
# rimosso da `include/chronon3d/`").  src/ tests/ apps/ are NOT scanned
# (build-time errors will already trip on the deleted-header
# references after Phase A6; this gate is the structural compile-fail
# sentinel against future re-import under the canonical public
# include-tree scope).
echo -n "  [23/24] TextPlacementResolver wrapper RETIRED ... "
# Structural sentinel (P1 reviewer hardening): the deleted wrapper header
# must NEVER be re-created on disk. Even a re-rename from
# resolve_text_placement.hpp would silently re-expose the wrapped
# surface; this check catches that pre-grep.
if [ -f include/chronon3d/text/text_placement_resolver.hpp ] \
    || [ -f src/text/text_placement_resolver.cpp ]; then
    echo "FAIL"
    echo "  The retired wrapper files are back on disk:"
    [ -f include/chronon3d/text/text_placement_resolver.hpp ] && \
        echo "    include/chronon3d/text/text_placement_resolver.hpp"
    [ -f src/text/text_placement_resolver.cpp ] && \
        echo "    src/text/text_placement_resolver.cpp"
    echo "  → Phase A6 (2026-07-10) deleted both files. If you need to reintroduce"
    echo "    the TextPlacementResolver class surface, follow the user-directive"
    echo "    procedure (ADR + new ticket) rather than git-renaming the canonical"
    echo "    header back to the old filename."
    FAILED=1
    hits=""
    inc_hits=""
else
    hits=$(grep -RnE 'class\s+TextPlacementResolver\b' include/chronon3d/ 2>/dev/null \
        | filter_symbol_in_code_only 'class\s+TextPlacementResolver\b' \
        || true)
    # Symmetric include scan (P1 reviewer hardening): also cover .cpp under
    # include/chronon3d/ to catch relative `#include "text_placement_resolver.hpp"`
    # patterns a future commit might introduce.
    inc_hits=$(grep -Rn --include='*.hpp' --include='*.inl' --include='*.cpp' \
        -E '#\s*include\s+<chronon3d/text/text_placement_resolver\.hpp>' \
        include/chronon3d/ 2>/dev/null \
        || true)
    if [ -n "$hits" ] || [ -n "$inc_hits" ]; then
        echo "FAIL"
        if [ -n "$hits" ]; then
            echo "  class TextPlacementResolver re-declared in include/chronon3d/:"
            echo "$hits" | sed 's/^/    /'
        fi
        if [ -n "$inc_hits" ]; then
            echo "  <chronon3d/text/text_placement_resolver.hpp> included from include/chronon3d/:"
            echo "$inc_hits" | sed 's/^/    /'
        fi
        echo "  → Phase A6 (2026-07-10) retired the TextPlacementResolver wrapper"
        echo "    surface. The free function \`resolve_text_placement()\` in"
        echo "    \`include/chronon3d/text/resolve_text_placement.hpp\` is the"
        echo "    canonical surface (per ADR-019 Decision 3). The retired wrapper"
        echo "    header \`text_placement_resolver.hpp\` cannot be re-imported"
        echo "    from any header under \`include/chronon3d/\` (this gate, per"
        echo "    the user's directive: 'vieta l'include dell'header del wrapper"
        echo "    rimosso da include/chronon3d/'). To re-expose a stateful"
        echo "    resolver class, you MUST update ADR-019 + open a new ticket;"
        echo "    do NOT silently re-introduce the class via a re-imported"
        echo "    \`text_placement_resolver.hpp\`."
        FAILED=1
    else echo "PASS"; fi
fi

# ── 24. Single source of truth (SSoT) audit — Test 12 ───────────────────
# Closes TICKET-TEST-12-SSOT-AUDIT. Sibling gate that audits 8 canonical
# concepts (Asset/Placement/Layout/Animation/Composition/Render/Diagnostica/
# Sequence) + 4 specific user-spec FAIL patterns (asset legacy+v2, offset+
# placement.offset, text effects+material glow, CLI/SDK render unified
# orchestration). Output: VIOLATIONS bash array, one GATE_FAIL per violation.
# Wired as a sibling (not inlined) per the §honesty + Cat-3 anti-duplication
# pattern established by gates #10 (SoftwareRenderer boundary) + #15 (legacy
# text pipeline) — each sibling is a single-concern gate that exits 0/1/2.
#
# Concept 2 (Placement) has a HARD-CAP for the pre-existing
# TICKET-TEXT-LEGACY-POSITION-ROT rot: 200+ sites still assign
# `TextSpec{.position = Vec3{...}}` (tracked open blocker). The audit
# reports the current count and FAILS ONLY IF the count exceeds the cap
# (i.e., a regression above the known baseline). This is the
# established pattern for tracking pre-existing rot without failing the
# audit on every push.
#
# Concept 8 (Sequence) drops the `class SequenceBuilder` pattern from
# legacy detection: the wrapper at include/chronon3d/scene/builders/
# sequence_builder.hpp is the LEGITIMATE public API that DELEGATES to
# SceneBuilder::compile_sequence (per the C2 comment). It is NOT a
# parallel implementation.
echo -n "  [24/24] SSoT audit (8 concepts + 4 patterns) ... "
if [ -x tools/check_single_source_of_truth.sh ]; then
    if bash tools/check_single_source_of_truth.sh > /dev/null 2>&1; then
        echo "PASS"
    else
        echo "FAIL"
        bash tools/check_single_source_of_truth.sh 2>&1 | sed 's/^/    /' | head -40 || true
        FAILED=1
    fi
else
    echo "SKIP (tools/check_single_source_of_truth.sh not executable)"
fi
# Gate #25 — M1.8 §1 dual text API invariant (forward-only anti-duplication).
# Folded from the previously-untracked local tools/check_no_dual_text_api.sh
# (I1 audit remediation 2026-07-13). Enforces the Text Simplicity §1 rule via
# 4 categories on the active source surface:
#
#   [1/4] LayerBuilder::text_<variant>           (canonical: text + text_run only)
#   [2/4] centered_text / glow_text DEFINITION  (canonical preset registry only:
#                                                  src/scene/presets/ + src/presets/ +
#                                                  include/chronon3d/presets/ +
#                                                  content/text/)
#   [3/4] TextSpec.position non-migrated       (hard-cap 250 grandfathered sites
#                                                  tracked by TICKET-TEXT-LEGACY-POSITION-ROT)
#   [4/4] pin_to + TextAnchor + .text() co-occurrence per TU
#                                                 (blocking per ADR-019 §3)
#
# VIOLATIONS bash array pattern (consistent with check_single_source_of_truth.sh
# sibling-pattern + AGENTS.md §Regole di lint documentale INFO-level style).
# Implementation note: regex patterns use PCRE (`grep -P`) which is GNU-grep only;
# consistent with sibling gates #18 #19 #20 #22 #23 (the canonical grep surface
# across check_architecture_boundaries.sh). macOS/BSD grep -P fails by design;
# canonical build host is Linux per AGENTS.md §Regole di lavoro.
#
# Path scope is GATE25_SCAN_PATHS (content/ included on top of the gate-wide
# SCRIPT_PATHS, because centered_text/glow_text definitions canonically live
# in content/text/).
GATE25_SCAN_PATHS='src include content apps tests'
echo -n "  [25/25] M1.8 §1 dual text API invariant ... "
VIOLATIONS=()

# [1/4] LayerBuilder::text_* method variants (canonical: text + text_run).
hits_1=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    '\bLayerBuilder::text_[a-zA-Z_]+\b' $SCRIPT_PATHS 2>/dev/null \
    | grep -v 'LayerBuilder::text_run' \
    | grep -oE 'LayerBuilder::text_[a-zA-Z_]+' \
    | sort -u || true)
if [ -n "$hits_1" ]; then
    VIOLATIONS+=("[1/4] LayerBuilder::text_* variant: $(echo "$hits_1" | tr '\n' ' ')")
fi

# [2/4] centered_text / glow_text DEFINITION outside canonical preset scope.
def_pat='(^[[:space:]]*#[[:space:]]*define[[:space:]]+(centered_text|glow_text)([[:space:]]|\()|(^[[:space:]]*(class|struct)[[:space:]]+(centered_text|glow_text)\b)|(^[[:space:]]*(template[[:space:]]*<[^>]*>[[:space:]]*)?(static|inline|const|void|int|bool|auto|Layer|std::string)[a-zA-Z0-9_:[:space:]*&]*\b(centered_text|glow_text)[[:space:]]*\()'
hits_2=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' -E "$def_pat" $GATE25_SCAN_PATHS 2>/dev/null \
    | filter_symbol_in_code_only '(centered_text|glow_text)' \
    | grep -Ev '(src/scene/presets/|src/presets/|include/chronon3d/presets/|content/text/)' \
    || true)
if [ -n "$hits_2" ]; then
    VIOLATIONS+=("[2/4] centered_text/glow_text outside canonical preset scope ($(echo "$hits_2" | wc -l) site(s))")
fi

# [3/4] TextSpec.position non-migrated ASSIGNMENT (HARD-CAP 250 grandfathered).
# Cap matches SSoT Concept 2 ceiling for TICKET-TEXT-LEGACY-POSITION-ROT;
# raising the cap requires TICKET-TEXT-LEGACY-POSITION-ROT forward-point
# regime review (NOT a silent bump). include/chronon3d/scene/builders/
# builder_params.hpp is exempt because it is the field-declaration site.
# Path-safe iteration (handles whitespace-in-filenames, latent for future authors).
hits_3_count=0
file_count_3=0
while IFS= read -r f; do
    [ -z "$f" ] && continue
    case "$f" in
        include/chronon3d/scene/builders/builder_params.hpp) continue ;;
    esac
    file_hits=$(grep -Pon '\.position(?=\s*[={])' "$f" 2>/dev/null | wc -l || true)
    hits_3_count=$((hits_3_count + file_hits))
    file_count_3=$((file_count_3 + 1))
done < <(grep -Rl --include='*.hpp' --include='*.cpp' --include='*.h' \
             '\bTextSpec\b' $SCRIPT_PATHS 2>/dev/null || true)
# Cap = 76, calibrated to empirically-measured 26 + safety margin 50.
# Raising the cap requires TICKET-TEXT-LEGACY-POSITION-ROT forward-point
# regime review (NOT a silent bump). include/chronon3d/scene/builders/
# builder_params.hpp is exempt because it is the field-declaration site.
if [ "$hits_3_count" -gt 76 ]; then
    VIOLATIONS+=("[3/4] TextSpec.position assignments ($hits_3_count > 76 hard-cap, TICKET-TEXT-LEGACY-POSITION-ROT forward-point)")
fi

# [4/4] pin_to + TextAnchor + .text() co-occurrence per TU (ADR-019 §3).
suspect_files=""
while IFS= read -r f; do
    [ -z "$f" ] && continue
    if grep -lqE '\bTextAnchor::' "$f" 2>/dev/null \
       && grep -lqE '\.text\(' "$f" 2>/dev/null; then
        suspect_files="${suspect_files}${f}"$'\n'
    fi
done < <(grep -Rl --include='*.hpp' --include='*.cpp' --include='*.h' \
             '\bpin_to\(' $SCRIPT_PATHS 2>/dev/null || true)
suspect_files=$(echo "$suspect_files" | grep -v '^$' | sort -u || true)
if [ -n "$suspect_files" ]; then
    VIOLATIONS+=("[4/4] pin_to+TextAnchor+.text() co-occurrence ($(echo "$suspect_files" | wc -l) TU(s))")
fi

if [ "${#VIOLATIONS[@]}" -ne 0 ]; then
    echo "FAIL"
    for v in "${VIOLATIONS[@]}"; do
        echo "    - $v"
    done
    FAILED=1
else
    echo "PASS"
    echo "[INFO] check_architecture_boundaries: 4 M1.8 §1 dual text API categories clean (no parallel text API on the active source surface)"
fi



# ── Summary ───────────────────────────────────────────────────────────
echo ""
if [ "$FAILED" -ne 0 ]; then
    echo "=== Architecture boundary checks FAILED (one or more P0 violations) ==="
    exit 1
fi
echo "=== All architecture boundary checks PASSED! ==="
exit 0
