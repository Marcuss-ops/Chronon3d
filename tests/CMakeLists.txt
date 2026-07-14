# tests/CMakeLists.txt — Test Orchestrator
#
# Single source of truth for per-area test definitions.
# ADD-ONLY discipline: each entry below is registered to both
# `CMAKE_CONFIGURE_DEPENDS` AND `include()` via a single foreach
# loop (ADR-018 — addresses the rot-pattern observed on `main@872993ea`
# when a test definition was added to the include() block without a
# matching CMAKE_CONFIGURE_DEPENDS entry, silently leaving build.ninja
# stale on subsequent edits).
#
# Per-area .cmake files self-guard their feature gates via early-return
# `if(NOT <COND>) return() endif()` — see canonical pattern in
#   tests/sabotage_tests.cmake        (CHRONON3D_BUILD_TESTS)
#   tests/visibility_contract_tests.cmake  (CHRONON3D_BUILD_DIAGNOSTICS)
#   tests/inspect_text_tests.cmake    (CHRONON3D_BUILD_CLI_DEV)
#   tests/text/text_clip_policy_tests.cmake (CHRONON3D_BUILD_DIAGNOSTICS)
# Each new test area must:
#   1. add ONE entry to CHRONON3D_TEST_DEFINITIONS
#   2. write an early-return guard at the top of the per-area .cmake
#      file whenever the test requires a feature flag
#   3. NEVER introduce a duplicate manual include() list, a duplicate
#      CMAKE_CONFIGURE_DEPENDS list, or a duplicate `if(CHRONON3D_*)`
#      block here
#
# The aggregate custom targets + label applications + per-target
# CHRONON3D_FAST_TEST_DEPS / CHRONON3D_RENDER_TEST_DEPS appends below
# the foreach are guarded by the SINGLE `if(CHRONON3D_BUILD_TESTS)`
# wrapper — that is the only feature gate left in this orchestrator.

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

# ── Single test manifest (ADR-018 — TICKET-CMAKE-TEST-MANIFEST-UNIFICATION) ──
# The for_each loop below expands to BOTH:
#   1. set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
#   2. include("${path}")
# for every entry, so ADD-ONLY discipline = 1 line per new test area.
# Per-file gates live in the .cmake file (early-return), not here.
set(CHRONON3D_TEST_DEFINITIONS
    # Unconditional (no feature gate)
    backends_software_tests.cmake
    debug/CMakeLists.txt
    # CHRONON3D_BUILD_TESTS-gated (per-area early-return on each file)
    core_tests.cmake
    scene_tests.cmake
    cli_tests.cmake
    optimizer_tests.cmake
    preflight_tests.cmake
    cache_tests.cmake
    compositor_tests.cmake
    benchmarks.cmake
    # CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT-gated
    authoring_tests.cmake
    animation_tests.cmake
    renderer_tests.cmake
    io_tests.cmake
    breathing_golden_tests.cmake
    visual_tests.cmake
    graphics_tests.cmake
    gradient_visual_tests.cmake
    rounded_rect_visual_tests.cmake
    deterministic_tests.cmake
    text_golden_tests.cmake
    text_production_v1_tests.cmake
    diagnostic_overlay_tests.cmake
    presets_golden_tests.cmake
    text_preset_visual_tests.cmake
    # Unconditional (math / harness only — no rendering backend)
    precomp_focus_tests.cmake
    timeline_tests.cmake
    timeline_functional_v1_tests.cmake
    text_definition_tests.cmake
    safe_area_placement_tests.cmake
    text_builder_ergonomics_tests.cmake
    animation_helpers_tests.cmake
    text_simplicity_adapters_tests.cmake
    text/text_clip_policy_tests.cmake  # corrected path — was tests/text_clip_policy_tests.cmake (file does not exist at root)
    text_presets_stability_tests.cmake
    text_layout_advanced_tests.cmake
    # CHRONON3D_BUILD_CONTENT-gated
    content_tests.cmake
    showcase/CMakeLists.txt
    # CHRONON3D_ENABLE_VIDEO-gated (single block in the old orchestrator;
    # the duplicate `if(CHRONON3D_ENABLE_VIDEO)` for media_tests.cmake is
    # folded here so both files share the same per-file early-return gate)
    video_tests.cmake
    media_tests.cmake
    # Unconditional (SDK / SIMD / render-graph / sabotage)
    sdk_tests.cmake
    simd/simd_parity_blend_tests.cmake
    render_graph/compiler/fusion_pass_tests.cmake
    render_graph/pipeline/glow_fullframe_audit_tests.cmake
    sabotage_tests.cmake
)

foreach(definition IN LISTS CHRONON3D_TEST_DEFINITIONS)
    set(path "${CMAKE_CURRENT_SOURCE_DIR}/${definition}")
    set_property(
        DIRECTORY APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${path}"
    )
    include("${path}")
endforeach()

# ── Aggregate targets + per-area appends + label applications ─────────
# Single `if(CHRONON3D_BUILD_TESTS)` wrap that guards ALL the
#   * aggregate custom targets (chronon3d_tests_fast / _render / _video / _tests),
#   * per-target `if(TARGET ...)` appends to CHRONON3D_FAST_TEST_DEPS /
#     CHRONON3D_RENDER_TEST_DEPS,
#   * umbrella labels (chronon3d_text_full_acceptance, chronon3d_sanitizer_subsystems),
#   * architecture_tests.cmake include (Python + Gate 11 + parity scripts)
# in one shot. Per-area .cmake files self-guard individually (early-return);
# the wrap here is only for the AGGREGATE bookkeeping. It is the
# ONLY feature gate left in this orchestrator by design.
if(CHRONON3D_BUILD_TESTS)

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
    chronon3d_timeline_tests
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

# M1.8 §10 / TICKET-TEXT-FULL-ACCEPTANCE — umbrella BUILD target + ctest
# label for the Text V1 milestone cert.  Single command:
#   ctest -L text-full-acceptance
# runs all 7 tests in one invocation (the 6 chronon3d_text_*_tests
# + chronon3d_inspect_text_tests).  Mirrors the chronon3d_tests_fast
# / chronon3d_tests_render / chronon3d_tests_video pattern above:
# conditional `if(TARGET ...)` guards because some targets are gated
# by CHRONON3D_USE_BLEND2D / CHRONON3D_BUILD_CLI_DEV at the per-area
# .cmake level.  Forward-point: the `chronon3d_add_test_suite()` `LABELS`
# arg is the canonical per-suite forward path; this centralized
# application here is intentional (one place to add/remove the label
# when a new text_*_tests target is added to the umbrella).
set(CHRONON3D_TEXT_FULL_ACCEPTANCE_DEPS "")
if(TARGET chronon3d_text_presets_stability_tests)
    list(APPEND CHRONON3D_TEXT_FULL_ACCEPTANCE_DEPS chronon3d_text_presets_stability_tests)
endif()
if(TARGET chronon3d_text_simplicity_adapters_tests)
    list(APPEND CHRONON3D_TEXT_FULL_ACCEPTANCE_DEPS chronon3d_text_simplicity_adapters_tests)
endif()
if(TARGET chronon3d_text_preset_visual_tests)
    list(APPEND CHRONON3D_TEXT_FULL_ACCEPTANCE_DEPS chronon3d_text_preset_visual_tests)
endif()
if(TARGET chronon3d_text_golden_tests)
    list(APPEND CHRONON3D_TEXT_FULL_ACCEPTANCE_DEPS chronon3d_text_golden_tests)
endif()
if(TARGET chronon3d_text_builder_ergonomics_tests)
    list(APPEND CHRONON3D_TEXT_FULL_ACCEPTANCE_DEPS chronon3d_text_builder_ergonomics_tests)
endif()
if(TARGET chronon3d_text_definition_tests)
    list(APPEND CHRONON3D_TEXT_FULL_ACCEPTANCE_DEPS chronon3d_text_definition_tests)
endif()
if(TARGET chronon3d_inspect_text_tests)
    list(APPEND CHRONON3D_TEXT_FULL_ACCEPTANCE_DEPS chronon3d_inspect_text_tests)
endif()
add_custom_target(chronon3d_text_full_acceptance
    DEPENDS ${CHRONON3D_TEXT_FULL_ACCEPTANCE_DEPS}
)
foreach(_tfa_target IN LISTS CHRONON3D_TEXT_FULL_ACCEPTANCE_DEPS)
    set_tests_properties(${_tfa_target} PROPERTIES LABELS "text-full-acceptance")
endforeach()

# P2-A / TICKET-SANITIZER-GATES — chronon3d_sanitizer_subsystems umbrella
# BUILD target + ctest label for the 7-subsystem sanitizer cert
# (0 OOB / 0 UAF / 0 UB / 0 data races across FontEngine, glyph cache,
# layout cache, asset resolver, text audit snapshots, renderer session,
# factory registration).  Single command:
#   ctest -L sanitizer-subsystems
# runs all 5 test targets in one invocation, under the linux-asan or
# linux-tsan preset (the preset sets the -fsanitize=* flags and the
# test-preset `environment:` block sets the ASAN/UBSAN/TSAN_OPTIONS
# for the cert).  Mirrors the chronon3d_text_full_acceptance pattern:
# conditional `if(TARGET ...)` guards because some targets are gated
# by CHRONON3D_USE_BLEND2D / CHRONON3D_BUILD_CLI_DEV /
# CHRONON3D_BUILD_DIAGNOSTICS at the per-area .cmake level.
#
# 7-subsytem → test target mapping:
#   FontEngine         → chronon3d_core_tests (test_font_engine, test_freetype_face_cache_concurrency, test_font_io_fence, test_draw_text_run_scratch_state, test_font_identity_contract)
#   Glyph cache        → chronon3d_core_tests (test_glyph_atlas_metadata)
#   Layout cache       → chronon3d_core_tests (test_layout_cache_collision)
#   Asset resolver     → chronon3d_core_tests (test_asset_resolver, test_asset_registry)
#   Text audit snapshots → chronon3d_visibility_contract_tests + chronon3d_pipeline_parity_tests + chronon3d_inspect_text_tests
#   Renderer session   → chronon3d_core_tests (test_render_runtime_isolation, test_render_session_reset_and_isolation, test_camera_session_*)
#   Factory registration → chronon3d_core_tests (test_text_preset_registry, test_text_preset_descriptor) + chronon3d_text_presets_stability_tests
set(CHRONON3D_SANITIZER_SUBSYSTEMS_DEPS "")
if(TARGET chronon3d_core_tests)
    list(APPEND CHRONON3D_SANITIZER_SUBSYSTEMS_DEPS chronon3d_core_tests)
endif()
if(TARGET chronon3d_text_presets_stability_tests)
    list(APPEND CHRONON3D_SANITIZER_SUBSYSTEMS_DEPS chronon3d_text_presets_stability_tests)
endif()
if(TARGET chronon3d_visibility_contract_tests)
    list(APPEND CHRONON3D_SANITIZER_SUBSYSTEMS_DEPS chronon3d_visibility_contract_tests)
endif()
if(TARGET chronon3d_text_clip_policy_tests)
    list(APPEND CHRONON3D_SANITIZER_SUBSYSTEMS_DEPS chronon3d_text_clip_policy_tests)
endif()
if(TARGET chronon3d_inspect_text_tests)
    list(APPEND CHRONON3D_SANITIZER_SUBSYSTEMS_DEPS chronon3d_inspect_text_tests)
endif()
add_custom_target(chronon3d_sanitizer_subsystems
    DEPENDS ${CHRONON3D_SANITIZER_SUBSYSTEMS_DEPS}
)
# ORDER DEPENDENCY: this umbrella runs AFTER chronon3d_text_full_acceptance
# (so get_test_property() reads the prior text-full-acceptance label for
# the 2 overlapping targets). Do not reorder the 2 umbrellas.
# Apply `sanitizer-subsystems` label WITHOUT clobbering pre-existing labels
# (chronon3d_text_presets_stability_tests + chronon3d_inspect_text_tests
# are in BOTH the chronon3d_text_full_acceptance umbrella AND this one,
# so a naive `set_tests_properties(... LABELS "sanitizer-subsystems")` would
# strip their `text-full-acceptance` label and silently break
# `ctest -L text-full-acceptance` for those 2 tests). The append pattern
# below preserves any existing labels via get_test_property() + set.
foreach(_ss_target IN LISTS CHRONON3D_SANITIZER_SUBSYSTEMS_DEPS)
    get_test_property(${_ss_target} LABELS _ss_existing_labels)
    if(_ss_existing_labels)
        set_tests_properties(${_ss_target} PROPERTIES
            LABELS "${_ss_existing_labels};sanitizer-subsystems")
    else()
        set_tests_properties(${_ss_target} PROPERTIES
            LABELS "sanitizer-subsystems")
    endif()
endforeach()

include(${CMAKE_CURRENT_SOURCE_DIR}/architecture_tests.cmake)
endif()  # CHRONON3D_BUILD_TESTS

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
