# ==============================================================================
# Content Sub-Targets
# Each category can be built independently or as a whole via chronon3d_content
# ==============================================================================

# Add category subdirectories
add_subdirectory(shapes)
add_subdirectory(images)
add_subdirectory(anims)
add_subdirectory(Minimalist)
add_subdirectory(SpecialNames)
add_subdirectory(ImportantWords)
add_subdirectory(effects)
add_subdirectory(2d5)
add_subdirectory(grid)

# =============================================================================
# Canonical content module list — single source of truth for all content targets.
#
# Any target that needs to link all content modules (whether inside a
# --whole-archive wrapper for shared-lib builds, or as regular PUBLIC deps
# for static/executable targets) should use this INTERFACE target rather
# than listing individual modules separately.
#
# WHOLE_ARCHIVE has been removed from static targets — content modules now
# use explicit ExtensionRegistry::register_composition() calls instead of
# CHRONON_REGISTER_COMPOSITION static-initialisation macros, so the linker
# preserves registration symbols without whole-archive for executable and
# static library targets. Shared libraries (chronon3d_shared) still need
# --whole-archive because STATIC symbols may be dropped by the linker.
# =============================================================================
add_library(chronon3d_all_content_modules INTERFACE)
target_link_libraries(chronon3d_all_content_modules INTERFACE
    chronon3d_content_shapes
    chronon3d_content_images
    chronon3d_content_anims
    chronon3d_content_minimalist
    chronon3d_content_special_names
    chronon3d_content_important_words
    chronon3d_content_effects
    chronon3d_content_2d5
    chronon3d_content_grid
)
add_library(chronon3d::all_content_modules ALIAS chronon3d_all_content_modules)

# Content module registration (links all content modules together)
add_library(chronon3d_content_registry STATIC
    register_content_modules.cpp
)
target_include_directories(chronon3d_content_registry PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_registry
    PUBLIC
        chronon3d
        chronon3d_all_content_modules
    PRIVATE chronon3d_extension
)
add_library(chronon3d::content_registry ALIAS chronon3d_content_registry)

# Meta-target: all content (used for full builds)
add_custom_target(chronon3d_content ALL
    DEPENDS
        chronon3d_all_content_modules
        chronon3d_content_registry
)


