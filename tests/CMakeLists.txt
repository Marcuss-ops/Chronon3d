# tests/CMakeLists.txt — Orchestrator
#
# This file includes per-area test definitions from .cmake sub-files.
# To add a new test target, create a new .cmake file (or a sub-directory)
# and include it here. This keeps merge conflicts small when multiple
# developers add tests in parallel.

# ADR-018 — declare the per-area `.cmake` files that are `include()`d
# below as configure-time dependencies, so Ninja / Make re-runs
# `cmake --preset` whenever any of them is touched. Without this, an
# edit to e.g. `text_preset_visual_tests.cmake` silently leaves the
# generated `build.ninja` and per-target `link.txt` stale (the F-A
# diagnostic surfaced this as a missing `link.txt` when the link
# step fails). Adding a `.cmake` to the property list is the canonical
# fix; do NOT add new per-area `.cmake` files without extending this
# list too. ADR-018 keeps the rule explicit and the file list co-located
# with the orchestrator.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/backends_software_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/core_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/scene_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/cli_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/optimizer_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/preflight_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/cache_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/compositor_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/authoring_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/animation_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/io_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/breathing_golden_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/visual_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/graphics_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/gradient_visual_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/rounded_rect_visual_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/deterministic_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/text_preset_visual_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/text_golden_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/diagnostic_overlay_tests.cmake  # M1.8 §4C — diagnostic overlay
    ${CMAKE_CURRENT_SOURCE_DIR}/text_definition_tests.cmake  # M1.8 §3 — pure struct + adapter lock tests (no Blend2D dependency, unconditional)
    ${CMAKE_CURRENT_SOURCE_DIR}/safe_area_placement_tests.cmake  # M1.8 §3B — SafeAreaTop/Bottom/Left/Right/Center + resolve_safe_area (no Blend2D, unconditional)
    ${CMAKE_CURRENT_SOURCE_DIR}/text_builder_ergonomics_tests.cmake  # M1.8 §5 — author handle chain + 1px-bbox invariant (no Blend2D, unconditional)
    ${CMAKE_CURRENT_SOURCE_DIR}/animation_helpers_tests.cmake  # M1.8 §3A — 17 animation helpers (interpolate/spring/sequence/stagger/loop/delay/ease/clamp/...) (no Blend2D, unconditional)
    ${CMAKE_CURRENT_SOURCE_DIR}/text_simplicity_adapters_tests.cmake  # M1.8 §6 — centered_text deprecation + glow consolidation + migration test (no Blend2D, unconditional)
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline_parity_tests.cmake  # M1.8 §4A — 7-pipeline parity (render/video/inspect-text + warmup/tile_pruning/serial/diagnostic) × 6 fields × 3 properties = 126 CHECKs (conditional on CHRONON3D_BUILD_DIAGNOSTICS via early-return in the .cmake file) — 7-pipeline parity (render/video/inspect-text + warmup/tile_pruning/serial/diagnostic) × 6 fields × 3 properties = 126 CHECKs (conditional on CHRONON3D_BUILD_DIAGNOSTICS via early-return in the .cmake file)
    ${CMAKE_CURRENT_SOURCE_DIR}/visibility_contract_tests.cmake  # M1.8 §1E — TextVisibilityAudit PASS/FAIL cascade + §9 FU04 violation response (4 TEST_CASEs / ~12 CHECKs) (conditional on CHRONON3D_BUILD_DIAGNOSTICS via early-return in the .cmake file)
    ${CMAKE_CURRENT_SOURCE_DIR}/inspect_text_tests.cmake  # M1.8 §4B — chronon3d_cli inspect-text (6 TEST_CASEs) — no Blend2D, requires CHRONON3D_BUILD_CLI_DEV=ON — TextVisibilityAudit PASS/FAIL cascade + §9 FU04 violation response (4 TEST_CASEs / ~12 CHECKs) (conditional on CHRONON3D_BUILD_DIAGNOSTICS via early-return in the .cmake file)
    ${CMAKE_CURRENT_SOURCE_DIR}/text_presets_stability_tests.cmake  # M1.8 §3C / TICKET-SIMPLICITY-PRESETS — 5 presets × 3 CHECKs = 15 assertions (UNCONDITIONAL, pure struct inspection)
    ${CMAKE_CURRENT_SOURCE_DIR}/presets_golden_tests.cmake  # M1.8 §3C / TICKET-SIMPLICITY-PRESETS — 5 PNG goldens in test_renders/golden/text/presets/ (Blend2D-gated INTEGRATION tier)
    ${CMAKE_CURRENT_SOURCE_DIR}/precomp_focus_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/content_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/video_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/media_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/architecture_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/showcase/CMakeLists.txt
)

find_package(doctest CONFIG REQUIRED)
include(doctest)

# Shared test main
set(TEST_MAIN test_main.cpp)

function(chronon3d_enable_test_pch target_name)
    # PCH disabled to prevent compiler crashes on resource-constrained environments
endfunction()

# §12.1 — Test source registration helpers (registration-only; see
# cmake/Chronon3DTestSuite.cmake for the §11 forward-compat design).
# This include must come BEFORE the per-area .cmake files so any
# call to chronon3d_add_test_suite() / chronon3d_register_test_source()
# from those .cmake files is recognised.
include(${CMAKE_SOURCE_DIR}/cmake/Chronon3DTestSuite.cmake)

# R2 — SoftwareProcessorContext acceptance test bundle (06 R2 deliverable).
# No `CHRONON3D_USE_BLEND2D`/`CHRONON3D_BUILD_CONTENT` guard: this test
# exercises the header-only POD default-state only, so it compiles in
# every preset that turns on `CHRONON3D_BUILD_TESTS`.
include(${CMAKE_CURRENT_SOURCE_DIR}/backends_software_tests.cmake)

# M1.5#8 / TICKET-GATE-10-PHASE-4-BLACK diagnostic — minimal save_png roundtrip
# test (white 64x64 Framebuffer → save_png → stbi_load → compare).  Lives in
# tests/debug/ to keep it isolated from the per-area test suite.  TIER=UNIT
# (no rendering backend dependency, no fixtures).  AGENTS.md V0.1 cat-1
# (build-fix diagnostic) freeze-compliant.
if(CHRONON3D_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/debug/CMakeLists.txt)
    include(${CMAKE_CURRENT_SOURCE_DIR}/debug/CMakeLists.txt)
endif()

# ── Per-area test definitions ───────────────────────────────────────
# Guarded by CHRONON3D_BUILD_TESTS (the project's own flag) rather than
# BUILD_TESTING, because BUILD_TESTING is only set by `include(CTest)`
# whereas `enable_testing()` (used at the top level) does NOT define it.
# Without this alignment, the per-area test executables and the
# chronon3d_tests_fast / chronon3d_tests_render / chronon3d_tests_video
# aggregate custom targets are silently dropped, which makes the
# chronon3d_dev_fast aggregate target unbuildable (ninja reports the
# custom target as a missing rule).
if(CHRONON3D_BUILD_TESTS)
include(${CMAKE_CURRENT_SOURCE_DIR}/core_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/scene_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cli_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/optimizer_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/preflight_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cache_tests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/compositor_tests.cmake)
# Authoring tests require text subsystem (glyph_selector, evaluate_selector, etc.)
if(CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT)
    include(${CMAKE_CURRENT_SOURCE_DIR}/authoring_tests.cmake)
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/benchmarks.cmake)
# Expressions V2 tests are quarantined under experimental/ and gated by
# CHRONON3D_BUILD_EXPERIMENTAL. See docs/EXPRESSIONS_V2_PROMOTION.md.

# Tests requiring Blend2D + text
if(CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT)
    include(${CMAKE_CURRENT_SOURCE_DIR}/animation_tests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/renderer_tests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/io_tests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/breathing_golden_tests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/visual_tests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/graphics_tests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/gradient_visual_tests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/rounded_rect_visual_tests.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/deterministic_tests.cmake)

# ── TXT-QA-01 — Real Golden Text Harness (standalone, UNITY_BUILD OFF)
include(${CMAKE_CURRENT_SOURCE_DIR}/text_golden_tests.cmake)
# ── M1.8 §4C — Diagnostic overlay golden test (Blend2D-gated INTEGRATION tier).
# Self-guards on CHRONON3D_USE_BLEND2D + CHRONON3D_ENABLE_TEXT (no-op in
# SDK-only builds).  Same pattern as `text_presets_visual_tests.cmake`.
include(${CMAKE_CURRENT_SOURCE_DIR}/diagnostic_overlay_tests.cmake)

# ── PR-A4 (Blocco A, Fase 1 followup) — 16 text-style preset visual regression
# The executable is gated by the outer `if(CHRONON3D_USE_BLEND2D AND
# CHRONON3D_ENABLE_TEXT)` wrap above; the render-aggregator append is the
# single canonical site (below) — never edit a copy here.
include(${CMAKE_CURRENT_SOURCE_DIR}/text_preset_visual_tests.cmake)
endif()

# WP-0 PR 0.2 — focused compile-coherence target for the PrecompNode
# TU family.  Compiles only `precomp_node_execute.cpp` + the direct
# test TU `test_precomp_node_cache.cpp` against the public
# `chronon3d_sdk` alias (no inner OBJECT libraries).  Routed through
# CTest so it runs in CI before the heavy renderer-test bag.
# Note: the .cmake file self-guards on CHRONON3D_USE_BLEND2D so the
# include is unconditional here.
include(${CMAKE_CURRENT_SOURCE_DIR}/precomp_focus_tests.cmake)

# M1.8 §3 / TICKET-SIMPLICITY-TEXTDEFINITION — pure struct + adapter
# lock tests.  Listed here (UNCONDITIONAL) and NOT inside the
# Blend2D+Text conditional above because: (i) the test exercises only
# pure data structs (TextDefinition, TextSpec, TextDocument, TextDocumentBuilder)
# with no compositor / no GPU / no FontEngine allocation; (ii) the
# include must register even on SDK-only builds where
# CHRONON3D_USE_BLEND2D / CHRONON3D_ENABLE_TEXT are OFF.  Per ADR-018,
# this file is also listed in CMAKE_CONFIGURE_DEPENDS at the top of this
# orchestrator.
include(${CMAKE_CURRENT_SOURCE_DIR}/text_definition_tests.cmake)

# M1.8 §3B / TICKET-SIMPLICITY-PLACEMENT — pure SafeArea solver lock
# tests.  Listed here (UNCONDITIONAL) and NOT inside the Blend2D+Text
# conditional above because: (i) the test exercises only
# `resolve_placement_origin()` + `resolve_safe_area()`-style pure
# math (no compositor / no GPU / no FontEngine allocation); (ii) the
# include must register even on SDK-only builds where
# CHRONON3D_USE_BLEND2D / CHRONON3D_ENABLE_TEXT are OFF.  Per ADR-018,
# this file is also listed in CMAKE_CONFIGURE_DEPENDS at the top of
# this orchestrator.
include(${CMAKE_CURRENT_SOURCE_DIR}/safe_area_placement_tests.cmake)

# M1.8 §5 / TICKET-SIMPLICITY-BUILDER exit-gate validation — locks the
# canonical `Layer::text(content) → Text handle → ... .commit()-less
# chain` ergonomics surface ("centra un titolo in < 10 righe"
# criterion, M1.8 §2B acceptance gate).  Listed here (UNCONDITIONAL)
# and NOT inside the Blend2D+Text conditional above because: (i) the
# test exercises only the authoring façade (PendingTextRun + Text
# setters + canonical resolver) with no compositor / no GPU / no
# FontEngine allocation; (ii) the include must register even on
# SDK-only builds where `CHRONON3D_USE_BLEND2D` /
# `CHRONON3D_ENABLE_TEXT` are OFF.  Per ADR-018, this file is also
# listed in `CMAKE_CONFIGURE_DEPENDS` at the top of this orchestrator.
include(${CMAKE_CURRENT_SOURCE_DIR}/text_builder_ergonomics_tests.cmake)
# M1.8 §3A / TICKET-SIMPLICITY-ANIMATION — locks ALL 17 animation helpers
# exported by `interpolate.hpp` with 17 × 2 = 34 doctest CHECK assertions
# (determinism + edge case per helper).  Pure math harness, no Blend2D /
# FontEngine / HarfBuzz / Framebuffer / GPU dependency.  UNCONDITIONAL
# registration (out of the Blend2D+Text conditional).
include(${CMAKE_CURRENT_SOURCE_DIR}/animation_helpers_tests.cmake)

# M1.8 §6 / TICKET-SIMPLICITY-ADAPTERS — deprecation + glow consolidation +
# migration test.  Listed here (UNCONDITIONAL) and NOT inside the
# Blend2D+Text conditional above because: (i) the math/diagnostic tests
# (#1-6 + #10) run in every build profile (no compositor / no GPU /
# no FontEngine allocation); (ii) the render tests (#7-9) are wrapped
# in `#ifdef CHRONON3D_USE_BLEND2D` inside the .cpp file so the math
# tests run in slim configurations without breaking the build.  Per
# ADR-018, this file is also listed in `CMAKE_CONFIGURE_DEPENDS` at the
# top of this orchestrator.
include(${CMAKE_CURRENT_SOURCE_DIR}/text_simplicity_adapters_tests.cmake)

# M1.8 §4A / TICKET-SIMPLICITY-PIPELINE-PARITY — locks the 7-pipeline
# parity contract (chronon3d_cli render / video / inspect-text + warmup
# on/off + tile_pruning on/off + serial/parallel + Debug/Release) that
# produces the same `glyph_count`, `layout_bbox`, `world_bbox`,
# `predicted_bbox`, `alpha_bbox`, hash (max ±2px differenza per bbox,
# byte-exact per `glyph_count`/`hash`).  7 pipelines × 18 CHECKs = 126
# assertions.  Listed here (UNCONDITIONAL) and NOT inside the
# Blend2D+Text conditional above because: (i) the test exercises the
# in-process SoftwareRenderer + text visibility audit harness +
# canonical framebuffer_hash (pure math + harness, no Blend2D / GPU
# dependency for the base identity checks); (ii) the include must
# register even on SDK-only builds where CHRONON3D_USE_BLEND2D /
# CHRONON3D_ENABLE_TEXT are OFF.  Per ADR-018, this file is also
# listed in `CMAKE_CONFIGURE_DEPENDS` at the top of this orchestrator.
include(${CMAKE_CURRENT_SOURCE_DIR}/pipeline_parity_tests.cmake)

# M1.8 §1E / TICKET-SIMPLICITY-VISIBILITY-CONTRACT — locks the FU04
# TextVisibilityAudit status cascade (PASS/FAIL/INDETERMINATE) + the
# §9 violation response (should_disable_tile_pruning +
# expanded_predicted_bbox) with 4 TEST_CASEs / ~12 CHECK assertions.
# Pure math harness, no Blend2D / GPU dependency.  CONDITIONAL on
# CHRONON3D_BUILD_DIAGNOSTICS via early-return in the .cmake file
# (the audit function is gated).  Per ADR-018, this file is also
# listed in CMAKE_CONFIGURE_DEPENDS at the top of this orchestrator.
include(${CMAKE_CURRENT_SOURCE_DIR}/visibility_contract_tests.cmake)

# M1.8 §4B / TICKET-SIMPLICITY-INSPECT-TEXT — locks the inspect-text
# CLI subcommand JSON schema + exit code mapping with 6 TEST_CASEs
# (PASS / FAIL / JSON-array format / entries / non-diagnostic build /
# frame number).  CONDITIONAL on CHRONON3D_BUILD_CLI_DEV via
# early-return in the .cmake file (the inspect-text subcommand lives
# in the chronon3d_cli_dev sub-target, OFF by default).  Per ADR-018,
# this file is also listed in CMAKE_CONFIGURE_DEPENDS at the top of
# this orchestrator.
include(${CMAKE_CURRENT_SOURCE_DIR}/inspect_text_tests.cmake)

# M1.8 §3C / TICKET-SIMPLICITY-PRESETS — 5 presets × 3 CHECKs = 15 assertions
# (UNCONDITIONAL, pure struct inspection).  Per ADR-018, this file is also
# listed in CMAKE_CONFIGURE_DEPENDS at the top of this orchestrator.
include(${CMAKE_CURRENT_SOURCE_DIR}/text_presets_stability_tests.cmake)

# Tests requiring content
if(CHRONON3D_BUILD_CONTENT)
    include(${CMAKE_CURRENT_SOURCE_DIR}/content_tests.cmake)
    # AGENT 4 / TICKET-A4 — cinematic camera showcase end-to-end verification.
    # Gated by CHRONON3D_BUILD_CONTENT because the cinematic_text_camera
    # compositions live under content/anims/compositions/.  See
    # tests/showcase/CMakeLists.txt for the local target registry.
    include(${CMAKE_CURRENT_SOURCE_DIR}/showcase/CMakeLists.txt)
endif()

# Tests requiring video
if(CHRONON3D_ENABLE_VIDEO)
    include(${CMAKE_CURRENT_SOURCE_DIR}/video_tests.cmake)
endif()

if(CHRONON3D_ENABLE_VIDEO)
    include(${CMAKE_CURRENT_SOURCE_DIR}/media_tests.cmake)
endif()

# TICKET-GATE-10-PHASE-4-BLACK SDK public-surface regression locks.
# Mirrors the gate #10 Phase 4 consumer pixel-hash invariant exactly
# so a silent-fallback regression in the SDK render path surfaces in
# BOTH this unit test AND the CI consumer gate. No Blend2D / text
# guard: the test exercises the SDK facade end-to-end against the
# installed Chronon3D package (umbrella include), so it compiles in
# every preset that turns on CHRONON3D_BUILD_TESTS.
include(${CMAKE_CURRENT_SOURCE_DIR}/sdk_tests.cmake)

# ── Aggregate targets: build test executables grouped by cost/speed ──
# Note: benchmarks are conditional; included only when TARGET chronon3d_benchmarks exists
if(TARGET chronon3d_benchmarks)
    set(CHRONON3D_BENCHMARK_DEP
        chronon3d_benchmarks
        chronon3d_scene_program_benchmarks)
else()
    set(CHRONON3D_BENCHMARK_DEP "")
endif()

# Fast tests: core + scene + CLI + deterministic (no rendering, no video, no content)
set(CHRONON3D_FAST_TEST_DEPS
    chronon3d_core_tests
    chronon3d_scene_tests
    chronon3d_optimizer_tests
    chronon3d_cache_tests
    chronon3d_compositor_tests
)
if(TARGET chronon3d_authoring_tests)
    list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_authoring_tests)
endif()
if(CHRONON3D_USE_BLEND2D)
    list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_deterministic_tests)
endif()
if(TARGET chronon3d_cli_tests)
    list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_cli_tests)
endif()
# WP-0 PR 0.2 — PrecompNode compile-coherence target wired into FAST
# so it runs on every CI build (matches the WP-0 PR 0.2 requirement
# that precomp integration is compiled by a required target).
if(TARGET chronon3d_precomp_focus_tests)
    list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_precomp_focus_tests)
endif()
add_custom_target(chronon3d_tests_fast
    DEPENDS ${CHRONON3D_FAST_TEST_DEPS}
)

# Render tests: scene + renderer + golden + content (heavy, needs software backend + content)
set(CHRONON3D_RENDER_TEST_DEPS "")
if(TARGET chronon3d_renderer_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_renderer_tests)
endif()
if(TARGET chronon3d_io_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_io_tests)
endif()
if(TARGET chronon3d_animation_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_animation_tests)
endif()
if(TARGET chronon3d_content_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_content_tests)
endif()
if(TARGET chronon3d_breathing_golden_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_breathing_golden_tests)
endif()
if(TARGET chronon3d_visual_test_support_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_visual_test_support_tests)
endif()
if(TARGET chronon3d_camera_visual_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_camera_visual_tests)
endif()
if(TARGET chronon3d_cinematic_motion_visual_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_cinematic_motion_visual_tests)
endif()
if(TARGET chronon3d_gradient_visual_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_gradient_visual_tests)
endif()
if(TARGET chronon3d_rounded_rect_visual_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_rounded_rect_visual_tests)
endif()
if(TARGET chronon3d_render_graph_node_visual_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_render_graph_node_visual_tests)
endif()
if(TARGET chronon3d_pr3_composition_visual_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_pr3_composition_visual_tests)
endif()
# PR-A4: visual executable created by the outer `if(CHRONON3D_USE_BLEND2D AND
# CHRONON3D_ENABLE_TEXT)` wrap above. Single aggregator append — do not
# reintroduce duplicate appends before the `set(... "")` reset below.
if(TARGET chronon3d_text_preset_visual_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_text_preset_visual_tests)
endif()
# TXT-QA-01 — text golden harness (standalone, UNITY_BUILD OFF)
if(TARGET chronon3d_text_golden_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_text_golden_tests)
endif()
# M1.8 §3C / TICKET-SIMPLICITY-PRESETS — 5 PNG goldens (Blend2D-gated INTEGRATION tier)
if(TARGET chronon3d_presets_golden_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_presets_golden_tests)
endif()
# M1.8 §4C / TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY — 5 PNG overlay goldens
# (Blend2D-gated INTEGRATION tier, one per clip variant).
if(TARGET chronon3d_diagnostic_overlay_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_diagnostic_overlay_tests)
endif()
# AGENT 4 / TICKET-A4 — append the cinematic camera showcase binary to the
# render aggregator (single canonical site, mirrors the PR-A4 append
# above; do not duplicate).  Conditional on CHRONON3D_BUILD_CONTENT so
# the build doesn't wire a target that's intentionally absent.
if(CHRONON3D_BUILD_CONTENT AND TARGET chronon3d_cinematic_camera_showcase_tests)
    list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_cinematic_camera_showcase_tests)
endif()
add_custom_target(chronon3d_tests_render
    DEPENDS ${CHRONON3D_RENDER_TEST_DEPS}
)

# Video tests: video pipeline tests (isolated, no pipeline dependency)
if(TARGET chronon3d_media_video_tests)
    add_custom_target(chronon3d_tests_video
        DEPENDS
            chronon3d_media_video_tests
    )
else()
    add_custom_target(chronon3d_tests_video)
endif()

# All tests: everything
add_custom_target(chronon3d_tests
    DEPENDS
        chronon3d_tests_fast
        chronon3d_tests_render
        chronon3d_tests_video
        ${CHRONON3D_BENCHMARK_DEP}
)

if(TARGET chronon3d_media_video_tests)
    add_dependencies(chronon3d_tests chronon3d_media_video_tests)
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/architecture_tests.cmake)
endif()

# §12.1 — Test source registry dump step.
# Reads the GLOBAL property populated by chronon3d_add_test_suite()
# and chronon3d_register_test_source() calls in the per-area .cmake
# files above, and writes the canonical list to
# ${CMAKE_BINARY_DIR}/registered_test_sources.txt.  The Python gate
# (`tools/check_test_source_registration.py`, §12.3) diffs this file
# against the on-disk `tests/**/test_*.cpp` enumeration to detect
# orphan / stale / duplicate sources.
#
# This step always runs (placed outside the `if(CHRONON3D_BUILD_TESTS)`
# block above) so the artifact is always present — empty when tests
# are not configured, populated when they are.  Determinism: the
# global property is appended in call order, which is deterministic
# (per CMake documentation on GLOBAL properties).
get_property(CHRONON3D_ALL_REGISTERED_TEST_SOURCES
    GLOBAL
    PROPERTY CHRONON3D_REGISTERED_TEST_SOURCES
)
file(WRITE
    "${CMAKE_BINARY_DIR}/registered_test_sources.txt"
    "${CHRONON3D_ALL_REGISTERED_TEST_SOURCES}"
)
