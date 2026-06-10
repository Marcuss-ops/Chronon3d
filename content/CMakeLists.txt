# ==============================================================================
# Content Sub-Targets
# Each category can be built independently or as a whole via chronon3d_content
# ==============================================================================

# Text content
add_library(chronon3d_content_text STATIC
    text/text_module.cpp
    text/typewriter/text_typewriter.cpp
)
set_target_properties(chronon3d_content_text PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_text PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_text
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_text PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)

# Shapes content
add_library(chronon3d_content_shapes STATIC
    shapes/proofs/shape_proofs.cpp
    shapes/motion/shape_motion_proofs.cpp
    shapes/registration/shape_registrations.cpp
)
set_target_properties(chronon3d_content_shapes PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_shapes PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_shapes
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_shapes PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)

# Images content
add_library(chronon3d_content_images STATIC
    images/compositions/image_compositions.cpp
    images/images_module.cpp
)
set_target_properties(chronon3d_content_images PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_images PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_images
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_images PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)

# Animations content
add_library(chronon3d_content_anims STATIC
    anims/compositions/animation_compositions.cpp
    anims/compositions/cinematic_showcase.cpp
    Minimalist/minimalist_clean_quote.cpp
    Minimalist/minimalist_focus_quote.cpp
    Minimalist/minimalist_hero_explainer.cpp
    Minimalist/minimalist_phrase_float.cpp
    Minimalist/minimalist_presets_showcase.cpp
    Minimalist/minimalist_scale_explainer.cpp
    Minimalist/minimalist_image_presets.cpp
    Minimalist/minimalist_module.cpp
)
set_target_properties(chronon3d_content_anims PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_anims PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_anims
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_anims PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)

# Effects content
add_library(chronon3d_content_effects STATIC
    effects/common/glow_test_common.hpp
    effects/glow/glow_01_neon_sign.cpp
    effects/glow/glow_02_orb_galaxy.cpp
    effects/glow/glow_03_cinematic_text.cpp
    effects/glow/glow_04_ui_card.cpp
    effects/glow/glow_05_pulse_wave.cpp
    effects/glow/glow_06_video_layer.cpp
    effects/glow/glow_07_premium_suite.cpp
    effects/thumbnails/premium_thumbnail_showcase.cpp
    effects/ref_2_5d/reference_2_5d_suite.cpp
)
set_target_properties(chronon3d_content_effects PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_effects PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_effects
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_effects PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)

# 2.5D content
add_library(chronon3d_content_2d5 STATIC
    2d5/two_point_five_d_module.cpp
    2d5/compositions/two_point_five_d_compositions.cpp
    2d5/compositions/camera_advanced_tests.cpp
    2d5/compositions/camera_test_orchestrator.cpp
)
set_target_properties(chronon3d_content_2d5 PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_2d5 PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_2d5
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_2d5 PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)

# Grid content
add_library(chronon3d_content_grid STATIC
    grid/compositions/grid_showcase.cpp
)
set_target_properties(chronon3d_content_grid PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_grid PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_grid
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_grid PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)

# Content module registration (links all content modules together)
add_library(chronon3d_content_modules STATIC
    register_content_modules.cpp
    register_minimalist_content.cpp
    register_text_content.cpp
    register_2d5_content.cpp
    register_images_content.cpp
)
target_include_directories(chronon3d_content_modules PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_modules
    PUBLIC chronon3d
    PRIVATE chronon3d_content_anims chronon3d_content_text chronon3d_content_2d5 chronon3d_content_images chronon3d_extension
)
add_library(chronon3d::content_modules ALIAS chronon3d_content_modules)

# Meta-target: all content (used for full builds)
add_custom_target(chronon3d_content ALL
    DEPENDS
        chronon3d_content_text
        chronon3d_content_shapes
        chronon3d_content_images
        chronon3d_content_anims
        chronon3d_content_effects
        chronon3d_content_2d5
        chronon3d_content_grid
        chronon3d_content_modules
)

# Aliases for convenient linking (no WHOLE_ARCHIVE needed — each is a small target)
add_library(chronon3d::content_text ALIAS chronon3d_content_text)
add_library(chronon3d::content_shapes ALIAS chronon3d_content_shapes)
add_library(chronon3d::content_images ALIAS chronon3d_content_images)
add_library(chronon3d::content_anims ALIAS chronon3d_content_anims)
add_library(chronon3d::content_effects ALIAS chronon3d_content_effects)
add_library(chronon3d::content_2d5 ALIAS chronon3d_content_2d5)
add_library(chronon3d::content_grid ALIAS chronon3d_content_grid)
