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
)

add_library(chronon3d::content ALIAS chronon3d_content)
