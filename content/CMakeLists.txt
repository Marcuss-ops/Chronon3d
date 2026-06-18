# ==============================================================================
# Content Gallery — product compositions (proofs, demos)
# ==============================================================================

add_library(chronon3d_content_gallery STATIC
    # 2.5D (product demos)
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

    # Text
    text/text_basic_glow.cpp
)

target_include_directories(chronon3d_content_gallery PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_gallery
    PUBLIC chronon3d
    PRIVATE chronon3d_extension
)

add_library(chronon3d::content_gallery ALIAS chronon3d_content_gallery)

# ==============================================================================
# Diagnostics Gallery — test/calibration/A/B compositions
# Only built when CHRONON3D_BUILD_DIAGNOSTICS is ON
# ==============================================================================

if(CHRONON3D_BUILD_DIAGNOSTICS)
    add_library(chronon3d_diagnostics_gallery STATIC
        # 2.5D camera tests
        2d5/compositions/camera_test_orchestrator.cpp
        2d5/compositions/camera_advanced_tests_diag.cpp
        2d5/compositions/camera_advanced_tests_rig.cpp
        2d5/compositions/camera_advanced_tests_scene.cpp
        2d5/compositions/camera_calibration_scene.cpp
        2d5/compositions/camera_floor_grid_test.cpp
        2d5/compositions/camera_near_plane_test.cpp
        2d5/compositions/camera_pitch_tests.cpp

        # Shape proofs
        shapes/proofs/shape_proofs.cpp
        shapes/motion/shape_motion_proofs.cpp

        # Image proofs + test patterns
        images/compositions/image_proofs.cpp
        images/compositions/image_test_patterns.cpp

        # Camera spline comparison
        anims/compositions/camera_spline_comparison.cpp

        # Glow A/B tests
        effects/glow/glow_v2_ab_tests.cpp

        # 2.5D reference suite
        effects/ref_2_5d/reference_2_5d_suite_cards.cpp
        effects/ref_2_5d/reference_2_5d_suite_scenes.cpp
    )
    target_include_directories(chronon3d_diagnostics_gallery PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(chronon3d_diagnostics_gallery
        PUBLIC chronon3d
        PRIVATE chronon3d_extension
    )
    add_library(chronon3d::diagnostics_gallery ALIAS chronon3d_diagnostics_gallery)
endif()

# ==============================================================================
# Content Registry — registers all compositions (product + optionally diagnostics)
# ==============================================================================

add_library(chronon3d_content_registry STATIC
    # NOTE: register_content_modules.cpp is owned by the registry —
    # it must NOT also live in content_gallery or diagnostics_gallery
    # or the linker sees duplicate symbols.
    register_content_modules.cpp
)
target_include_directories(chronon3d_content_registry PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_registry
    PUBLIC chronon3d
    PRIVATE chronon3d_extension
    PUBLIC chronon3d_content_gallery
)

if(CHRONON3D_BUILD_DIAGNOSTICS)
    target_link_libraries(chronon3d_content_registry PUBLIC chronon3d_diagnostics_gallery)
endif()

add_library(chronon3d::content_registry ALIAS chronon3d_content_registry)
