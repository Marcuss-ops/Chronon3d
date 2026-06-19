# ==============================================================================
# Content — single unified target
#
# PR 5: Merged chronon3d_content_gallery + chronon3d_content_registry into
# one target.  Each domain file owns its own register_*_compositions()
# function; register_content_modules.cpp orchestrates them all.
#
# PR 7: ExtensionRegistry removed — composition registration uses
# detail::add_builtin_composition() directly.
# ==============================================================================

add_library(chronon3d_content STATIC
    # Registration orchestrator
    register_content_modules.cpp

    # 2.5D
    2d5/compositions/two_point_five_d_compositions.cpp

    # Shapes (product demos only — proofs go to diagnostics)
    # shapes/proofs/shape_proofs.cpp → diagnostics
    # shapes/motion/shape_motion_proofs.cpp → diagnostics

    # Images (product demos)
    images/compositions/image_animated_reveals.cpp

    # Animations
    anims/compositions/animation_compositions.cpp
    anims/compositions/catmull_rom_showcase.cpp
    anims/compositions/cinematic_title_reveal.cpp
    anims/compositions/dolly_zoom_showcase.cpp
    anims/compositions/text_animations.cpp
    anims/compositions/tilt_sweep_title_v2.cpp
    anims/compositions/cinematic_text_camera.cpp

    # Minimalist
    Minimalist/minimalist_animations.cpp
    Minimalist/minimalist_image_presets.cpp

    # Special names
    SpecialNames/special_names_animations.cpp

    # Important words
    ImportantWords/important_words_animations.cpp

    # Effects (product: glow demos + thumbnails; A/B tests → diagnostics)
    effects/glow/glow_02_orb_galaxy.cpp
    effects/glow/glow_basic_word.cpp
    effects/thumbnails/premium_thumbnail_showcase.cpp

    # Grid
    grid/compositions/grid_showcase.cpp

    # Backgrounds (product: GridCleanBackground)
    backgrounds/grid_clean.cpp
)

target_include_directories(chronon3d_content PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content
    PUBLIC chronon3d
    PRIVATE chronon3d_extension
)

# ==============================================================================
# Diagnostics — separate static target, linked into chronon3d_content ONLY when
# CHRONON3D_BUILD_DIAGNOSTICS=ON.
#
# These .cpp files define the per-domain register_*_compositions() functions
# that register_content_modules.cpp forward-declares and calls under
# #ifdef CHRONON3D_BUILD_DIAGNOSTICS.  Keeping them in a separate target means:
#   - WITH diagnostics:    diagnostics symbols resolve at final link; no
#                          undefined-reference errors.
#   - WITHOUT diagnostics: the diagnostics TU simply doesn't exist; the
#                          orchestrator's #ifdef block is empty so no
#                          dangling extern symbols either.
#
# Sources:
#   shapes/proofs/shape_proofs.cpp            → register_shape_compositions()
#   shapes/motion/shape_motion_proofs.cpp     → factory called by the above
#   images/compositions/image_proofs.cpp      → register_image_compositions()
#   images/compositions/image_test_patterns.cpp → factories called by the above
# ==============================================================================
if(CHRONON3D_BUILD_DIAGNOSTICS)
    add_library(chronon3d_diagnostics STATIC
        # ── Shape + motion proofs (called by register_shape_compositions) ──
        shapes/proofs/shape_proofs.cpp
        shapes/motion/shape_motion_proofs.cpp

        # ── Image proofs + test patterns (called by register_image_compositions) ──
        images/compositions/image_proofs.cpp
        images/compositions/image_test_patterns.cpp

        # ── 2.5D camera diagnostic suite (called by register_2d5_compositions
        # under #ifdef CHRONON3D_BUILD_DIAGNOSTICS) ──
        2d5/compositions/camera_calibration_scene.cpp
        2d5/compositions/camera_test_orchestrator.cpp
        2d5/compositions/camera_advanced_tests_rig.cpp
        2d5/compositions/camera_advanced_tests_scene.cpp
        2d5/compositions/camera_advanced_tests_diag.cpp
        2d5/compositions/camera_near_plane_test.cpp
        2d5/compositions/camera_pitch_tests.cpp
        2d5/compositions/camera_floor_grid_test.cpp

        # ── 2.5D reference suite (called by register_effect_compositions
        # under #ifdef CHRONON3D_BUILD_DIAGNOSTICS) ──
        effects/ref_2_5d/reference_2_5d_suite_scenes.cpp
        effects/ref_2_5d/reference_2_5d_suite_cards.cpp

        # ── Glow V2 A/B tests (called by register_effect_compositions
        # under #ifdef CHRONON3D_BUILD_DIAGNOSTICS) ──
        effects/glow/glow_v2_ab_tests.cpp

        # ── Camera spline comparison (called by register_anim_compositions
        # under #ifdef CHRONON3D_BUILD_DIAGNOSTICS) ──
        anims/compositions/camera_spline_comparison.cpp
    )

    target_include_directories(chronon3d_diagnostics PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(chronon3d_diagnostics
        PUBLIC chronon3d
    )

    # PRIVATE: the diagnostic Composition factories (camera_*_test, etc.)
    # are only consumed internally by the orchestrator TUs in chronon3d_content
    # (two_point_five_d_compositions.cpp, glow_02_orb_galaxy.cpp,
    # animation_compositions.cpp, register_content_modules.cpp) which call them
    # under #ifdef CHRONON3D_BUILD_DIAGNOSTICS guards.
    # Downstream consumers of chronon3d::content never see these symbols
    # because the orchestrator TUs in the content library are the only callers.
    target_link_libraries(chronon3d_content PRIVATE chronon3d_diagnostics)
endif()

add_library(chronon3d::content ALIAS chronon3d_content)
