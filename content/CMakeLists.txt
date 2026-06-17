# ==============================================================================
# Content Gallery — single flat library (was 9 sub-libraries + 9 modules)
#
# Previously each family (shapes, images, etc.) was a separate STATIC library
# with its own CMakeLists, PCH, UNITY_BUILD setting, and alias target.
# Flattened into one library: simpler, fewer targets, no per-family overhead.
# ==============================================================================

add_library(chronon3d_content_gallery STATIC
    register_content_modules.cpp

    # Shape proofs
    shapes/register_shapes_content.cpp
    shapes/shape_proofs.cpp

    # Image proofs
    images/register_images_content.cpp
    images/image_proofs.cpp

    # Animation proofs
    anims/register_anims_content.cpp
    anims/anim_proofs.cpp

    # Minimalist animations
    minimalist/register_minimalist_content.cpp
    minimalist/minimalist_animations.cpp

    # Special names
    special_names/register_special_names_content.cpp
    special_names/special_names_animations.cpp

    # Important words
    important_words/register_important_words_content.cpp
    important_words/important_words_animations.cpp

    # Effects
    effects/register_effects_content.cpp
    effects/glow_compositions.cpp

    # 2.5D compositions
    2d5/register_2d5_content.cpp
    2d5/compositions.cpp

    # Grid proofs
    grid/register_grid_content.cpp
    grid/grid_proofs.cpp
)

target_include_directories(chronon3d_content_gallery PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_gallery
    PUBLIC chronon3d
    PRIVATE chronon3d_extension
)

add_library(chronon3d::content_gallery ALIAS chronon3d_content_gallery)

# Content module registration (uses ExtensionRegistry)
add_library(chronon3d_content_registry STATIC
    register_content_modules.cpp
)
target_include_directories(chronon3d_content_registry PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_registry
    PUBLIC chronon3d
    PRIVATE chronon3d_extension
)

add_library(chronon3d::content_registry ALIAS chronon3d_content_registry)
