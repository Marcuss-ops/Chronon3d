# ==============================================================================
# Content Sub-Targets
# Each category can be built independently or as a whole via chronon3d_content
# ==============================================================================

# Add category subdirectories
add_subdirectory(text)
add_subdirectory(shapes)
add_subdirectory(images)
add_subdirectory(anims)
add_subdirectory(Minimalist)
add_subdirectory(effects)
add_subdirectory(2d5)
add_subdirectory(grid)

# Content module registration (links all content modules together)
#
# WHOLE_ARCHIVE has been removed — all content modules now use explicit
# ExtensionRegistry::register_composition() calls in their register_with()
# methods instead of CHRONON_REGISTER_COMPOSITION static-initialisation macros.
# The linker no longer needs WHOLE_ARCHIVE to preserve registration objects.
add_library(chronon3d_content_registry STATIC
    register_content_modules.cpp
    register_minimalist_content.cpp
    register_text_content.cpp
    register_shapes_content.cpp
    register_images_content.cpp
    register_2d5_content.cpp
    register_anims_content.cpp
    register_grid_content.cpp
    register_effects_content.cpp
)
target_include_directories(chronon3d_content_registry PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_registry
    PUBLIC
        chronon3d
        chronon3d_content_text
        chronon3d_content_shapes
        chronon3d_content_images
        chronon3d_content_anims
        chronon3d_content_minimalist
        chronon3d_content_effects
        chronon3d_content_2d5
        chronon3d_content_grid
    PRIVATE chronon3d_extension
)
add_library(chronon3d::content_registry ALIAS chronon3d_content_registry)
# Legacy alias — keeps downstream references working
add_library(chronon3d_content_modules ALIAS chronon3d_content_registry)
add_library(chronon3d::content_modules ALIAS chronon3d_content_registry)

# Meta-target: all content (used for full builds)
add_custom_target(chronon3d_content ALL
    DEPENDS
        chronon3d_content_text
        chronon3d_content_shapes
        chronon3d_content_images
        chronon3d_content_anims
        chronon3d_content_minimalist
        chronon3d_content_effects
        chronon3d_content_2d5
        chronon3d_content_grid
        chronon3d_content_registry
)
# Aliases for convenient linking

