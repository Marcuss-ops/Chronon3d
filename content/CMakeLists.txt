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

    # ── Experimental gateway (only registers diagnostics when enabled) ─────
    experimental/effects-gateway/effect_diagnostics_gateway.cpp

    # ── Examples ────────────────────────────────────────────────────────────
    examples/text/text_animations.cpp
    examples/light/light_text_animations.cpp
    examples/image/image_animated_reveals.cpp

    # ── Backgrounds ────────────────────────────────────────────────────────
    backgrounds/grid_clean.cpp

    # ── Text placement test suite (content registerable) ─────────────
    text_placement/text_placement_compositions.cpp

    # ── Certification compositions (FASE 3.1-3.4 text verification) ──────
    certification/cert_title.cpp
    certification/cert_lower_third.cpp
    certification/cert_long_text.cpp
    certification/cert_multilingual.cpp

    # ── Sequence V2 migrated compositions (SequenceBuilder-only) ──
    showcases/sequence-v2/sequence_v2_compositions.cpp

    # ── AE Parity camera scenes (10 visual comparison scenes as CLI compositions) ─
    ae_parity/register_ae_parity_compositions.cpp
    ${CMAKE_SOURCE_DIR}/tests/visual/ae_parity/ae_parity_scenes.cpp
)

target_include_directories(chronon3d_content PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content
    PUBLIC chronon3d
    PRIVATE chronon3d_extension
)

# ==============================================================================
# Diagnostics — separate OBJECT target, linked into chronon3d_content ONLY when
# CHRONON3D_BUILD_DIAGNOSTICS=ON.
#
# These .cpp files define the per-domain register_*_compositions() functions
# that register_content_modules.cpp forward-declares and calls under
# #ifdef CHRONON3D_BUILD_DIAGNOSTICS.
# ==============================================================================
if(CHRONON3D_BUILD_DIAGNOSTICS)
    add_library(chronon3d_diagnostics OBJECT
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
        experimental/ae-parity/ae_camera_text_parity.cpp

        # ── Camera spline comparison ──
        experimental/camera-spline/camera_spline_comparison.cpp

        # ── Proof compositions ──
        experimental/proofs/shape_proofs.cpp
        experimental/proofs/shape_motion_proofs.cpp
        experimental/proofs/image_proofs.cpp
        experimental/proofs/image_test_patterns.cpp
    )

    target_include_directories(chronon3d_diagnostics PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(chronon3d_diagnostics
        PUBLIC chronon3d
    )

    target_link_libraries(chronon3d_content PRIVATE chronon3d_diagnostics)
endif()

if(CMAKE_PROJECT_NAME STREQUAL "Chronon3d")
    add_library(chronon3d::content ALIAS chronon3d_content)
endif()
