# ==============================================================================
# Content — single unified OBJECT target
#
# Directory structure:
#   content/showcases/    — production-ready, polished demos
#   content/examples/     — educational, simpler API usage
#   content/experimental/ — diagnostics & WIP (gated behind CHRONON3D_BUILD_DIAGNOSTICS)
#   content/common/       — shared helpers (backgrounds, text, animation)
#   content/backgrounds/  — reusable backgrounds
# ==============================================================================

add_library(chronon3d_content OBJECT
    # Registration orchestrator
    register_content_modules.cpp

    # ── Showcases ──────────────────────────────────────────────────────────
    showcases/minimalist/minimalist_animations.cpp
    showcases/minimalist/minimalist_image_presets.cpp
    showcases/special-names/special_names_animations.cpp
    showcases/important-words/important_words_animations.cpp
    showcases/grid/grid_showcase.cpp

    # Anim registration gateway (bridges showcases + examples + experimental)
    animation_compositions.cpp

    # Cinematic showcases (catmull-rom, dolly-zoom, title reveal, text camera)
    showcases/cinematic/catmull_rom_showcase.cpp
    showcases/cinematic/cinematic_title_reveal.cpp
    showcases/cinematic/deep_parallax_cascade.cpp
    showcases/cinematic/whip_pan_hero_reveal.cpp
    showcases/cinematic/orbit_handheld_glow.cpp
    showcases/cinematic/rack_focus_title_swap.cpp
    showcases/cinematic/abyss_freefall_stagger.cpp
    showcases/cinematic/dolly_zoom_showcase.cpp
    showcases/cinematic/tilt_sweep_title_v2.cpp

    # ── Product launches (end-to-end demo compositions for first-principles
    # product check Test #1) — additive to cinematic showcases; the
    # factory function exposes `product_launch()` (see hpp) and is
    # registered into the CLI registry by the launches block below.
    launches/product_launch.cpp
    launches/launches.cpp

    # ── Experimental gateway (only registers diagnostics when enabled) ─────
    experimental/effects-gateway/effect_diagnostics_gateway.cpp

    # ── Examples ────────────────────────────────────────────────────────────
    # TICKET-REFACTOR-CONTENT-EXAMPLES-17 (2026-07-12) — the monolithic
    # `examples/text/text_animations.cpp` (>360 LoC) has been split into 3
    # dedicated files by domain:
    #   - examples/text/easy_text_animations.cpp  (5 easy anims + helpers)
    #   - examples/text/typewriter_animations.cpp (5 typewriters using
    #     Step 10 `build_2line_typewriter(spec)` from text_reveal_helpers.hpp)
    #   - examples/text/text_animation_registration.cpp (10
    #     `registry.add(...)` calls consolidated)
    # The 10 inline `registry.add(...)` calls in animation_compositions.cpp
    # have been REMOVED; the new file's `register_text_animation_compositions`
    # is called from `register_anim_compositions` instead.
    examples/text/easy_text_animations.cpp
    examples/text/typewriter_animations.cpp
    examples/text/text_animation_registration.cpp
    examples/light/light_text_animations.cpp  # drift-allow: stale-ref
    examples/image/image_animated_reveals.cpp  # drift-allow: stale-ref

    # ── Backgrounds ────────────────────────────────────────────────────────
    backgrounds/grid_clean.cpp

    # ── Text placement test suite (content registerable) ─────────────
    text_placement/text_placement_compositions.cpp

    # ── Certification compositions (FASE 3.1-3.4 text verification) ──────
    certification/cert_title.cpp
    certification/cert_lower_third.cpp
    certification/cert_long_text.cpp
    certification/cert_multilingual.cpp

    # ── Render runtime & framebuffer certification (P0, TICKET-RENDER-RUNTIME-CERT) ──
    certification/cert_render_runtime.cpp

    # ── Timeline & sequence certification (P1, TICKET-TIMELINE-CERT) ──
    certification/cert_timeline.cpp

    # ── Compositing & effects certification (P1, TICKET-COMPOSITING-CERT) ──
    certification/cert_compositing.cpp

    # ── Determinism & cache certification (P1, TICKET-DETERMINISM-CERT) ──
    certification/cert_determinism.cpp

    # ── Sequence V2 migrated compositions (SequenceBuilder-only) ──
    showcases/sequence-v2/sequence_v2_compositions.cpp

    # TICKET-CLI-ISOLATE-RUNTIME-DEV (2026-07-12) — AE Parity camera scenes
    # (AE_CAM_01..10) are now DEV-only per the user-spec verbatim §3 (isola
    # runtime/dev).  The `tests/visual/ae_parity/ae_parity_scenes.cpp` factory
    # functions are compiled into chronon3d_cli_dev (when CHRONON3D_BUILD_CLI_DEV=ON)
    # and registered via `apps/chronon3d_cli/register_dev_compositions.cpp`
    # (DEV-gated).  Production CLI (CHRONON3D_BUILD_CLI_DEV=OFF) does not
    # include AE_CAM_* at all.  Action 16 (forward-point (b) of
    # TICKET-CLI-ISOLATE-RUNTIME-DEV closure, 2026-07-13) DELETED the
    # dead-code `content/ae_parity/register_ae_parity_compositions.{cpp,hpp}`
    # files — they had zero callers upstream (orchestrator already removed
    # in the original 2026-07-12 chore).  Test fixtures continue to
    # reference the AE_CAM_* factory functions via the DEV-only path.
)

target_include_directories(chronon3d_content PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content
    PUBLIC chronon3d
    PRIVATE chronon3d_extension
)

# ==============================================================================
# Diagnostics — source files condensed inline into chrono3d_content
#
# These .cpp files define the per-domain register_*_compositions() functions
# and the camera_spline_comparison() composition that register_content_modules.cpp
# (and animation_compositions.cpp) forward-declare and call under
# #ifdef CHRONON3D_BUILD_DIAGNOSTICS.
#
# They are inlined directly into chrono3d_content (under the diagnostic gate)
# rather than wrapped in a separate `chronon3d_diagnostics` OBJECT target,
# because OBJECT-to-OBJECT PRIVATE/PRIVATE-or-PUBLIC link propagation has
# historically shown inconsistent behaviour across CMake versions and Unity
# build configurations in this repo.  Inlining sidesteps the entire
# propagation question: the .o files become part of chronon3d_content's
# unity chunks and reach the consumer's final link line unconditionally.
# ==============================================================================
if(CHRONON3D_BUILD_DIAGNOSTICS)
    target_sources(chronon3d_content PRIVATE
        # ── Camera diagnostic suite ──
        experimental/camera/two_point_five_d_compositions.cpp
        experimental/camera/camera_calibration_scene.cpp
        experimental/camera/camera_test_orchestrator.cpp
        experimental/camera/camera_advanced_tests_rig.cpp
        experimental/camera/camera_advanced_tests_scene.cpp
        experimental/camera/camera_advanced_tests_diag.cpp
        experimental/camera/camera_near_plane_test.cpp
        experimental/camera/camera_pitch_tests.cpp
        experimental/camera/camera_floor_grid_test.cpp

        # ── 2.5D reference suite ──
        experimental/ref-2d5/reference_2_5d_suite_scenes.cpp
        experimental/ref-2d5/reference_2_5d_suite_cards.cpp

        # ── AE parity stress test ──
        # Moved here from apps/chronon3d_cli/CMakeLists.txt so that the
        # symbol is available to all targets linking chronon3d_content
        # (not just the CLI).  animation_compositions.cpp registers
        # ae_camera_text_parity() under #ifdef CHRONON3D_BUILD_DIAGNOSTICS,
        # so the definition must live in the same library.
        experimental/ae-parity/ae_camera_text_parity.cpp

        # ── Camera spline comparison ──
        experimental/camera-spline/camera_spline_comparison.cpp

        # ── Proof compositions ──
        experimental/proofs/shape_proofs.cpp
        experimental/proofs/shape_motion_proofs.cpp
        experimental/proofs/image_proofs.cpp
        experimental/proofs/image_test_patterns.cpp
    )
endif()

if(NOT TARGET chronon3d::content)
    add_library(chronon3d::content ALIAS chronon3d_content)
endif()

# ── Production-location header + implementation companion (added on this chore) ──
# content/compositions/chronon_glow_final.hpp — moved from tests/visual/ae_parity/glow_final_compositions.hpp
# on 2026-07-12 (TICKET-GLOW-FINAL-COMPOSITIONS-MOVE / refactor(glow) §4 cleanup).  Originally
# a header-only inline factory (287 LoC, all inline functions); SPLIT on this chore into:
#   - chronon_glow_final.hpp  (slim, ~64 LoC, decl-only — the public surface contract)
#   - chronon_glow_final.cpp  (NEW, implementation companion — bodies of the 3 public
#     factories + TU-local helpers in anonymous namespace: GlowLayout, resolve_layout,
#     CinematicGlowPreset, opacity_for_frame, scale_for_frame, build_chronon_glow_scene).
# Both live in `chronon3d::content::glow_final` namespace.
# `chronon3d_content` OBJECT library; consumers include the header only and resolve
# the symbol at link time.  CMAKE_SOURCE_DIR include dir already covers the new path.
compositions/chronon_glow_final.cpp
