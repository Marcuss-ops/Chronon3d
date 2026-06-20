# ==============================================================================
# src/CMakeLists.txt — Thin dispatcher
#
# Each per-subsystem CMakeLists.txt defines its own library target(s).
# This file handles:
#   1) External link flags (FFmpeg)
#   2) The base chronon3d INTERFACE target
#   3) add_subdirectory() calls in dependency order
#   4) Final aggregate targets (chronon3d_core, chronon3d_software, chronon3d_pipeline)
# ==============================================================================

# ==============================================================================
# CENTRALIZED EXTERNAL LINK FLAGS
# These are bare linker flags that don't have a CMake imported target. Keep
# all references to a given system library routed through these variables so
# the dependency set lives in one place (and the README build instructions
# stay in sync with the actual link command).
#
# As IMPORTED targets (so callers can target_link_libraries them like any
# other modern CMake target — no more raw "-l" strings in the graph).
# The targets exist with the CHRONON3D_ prefix so they don't collide with
# the system / vcpkg-provided FFmpeg imported targets (avcodec, avformat, ...).
#
# Only defined when native FFmpeg is enabled — otherwise the code paths
# that use swscale/avutil are compiled out via #ifdef guards.
# ==============================================================================
if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    add_library(chronon3d_ffmpeg_light INTERFACE)
    target_link_libraries(chronon3d_ffmpeg_light INTERFACE swscale avutil)
    add_library(chronon3d_ffmpeg_full  INTERFACE)
    target_link_libraries(chronon3d_ffmpeg_full  INTERFACE swscale avcodec avformat avutil)
endif()



# ==============================================================================
# Base Interface (must be defined before any add_subdirectory that uses it)
# ==============================================================================
add_library(chronon3d INTERFACE)
target_include_directories(chronon3d INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)
target_compile_definitions(chronon3d INTERFACE 
    CHRONON3D_GIT_COMMIT="${CHRONON3D_GIT_COMMIT}"
    $<$<BOOL:${MSVC}>:_DISABLE_STRING_VIEW_FIND_FIRST_NOT_OF_OPTIMIZATION>
)
# CHRONON3D_ENABLE_MESH is defined in the root CMakeLists.txt.

# Only link meshoptimizer when mesh support is active
set(CHRONON3D_MESHOPTIMIZER_LINK "")
if(CHRONON3D_ENABLE_MESH)
    set(CHRONON3D_MESHOPTIMIZER_LINK meshoptimizer::meshoptimizer)
    target_compile_definitions(chronon3d INTERFACE CHRONON3D_ENABLE_MESH)
endif()

target_link_libraries(chronon3d INTERFACE
    glm::glm
    ${CHRONON3D_MESHOPTIMIZER_LINK}
    xxHash::xxhash
    magic_enum::magic_enum
    chronon3d_core_impl
    # PR1 — transitively expose `chronon3d_animations` so that anything
    # linking chronon3d (scene, render_graph, ...) automatically sees the
    # implementations of motion::Timeline, AnimatedValue, and the new
    # chronon3d::temporal::generate_temporal_samples symbol.
    chronon3d_animations
)

# ==============================================================================
# Per-subsystem targets (ordered by dependency graph)
# ==============================================================================
add_subdirectory(core)              # chronon3d_core_impl
add_subdirectory(cache)             # chronon3d_cache
add_subdirectory(effects)           # chronon3d_effects
add_subdirectory(registry)          # chronon3d_registry
add_subdirectory(assets)            # chronon3d_assets
add_subdirectory(backends/image)    # chronon3d_backend_image
add_subdirectory(media)             # chronon3d_media_interface + chronon3d_backend_video
if(CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D)
    add_subdirectory(backends/text)     # chronon3d_backend_text (needs Blend2D for glyph rasteriser)
endif()
add_subdirectory(backends/assets)   # chronon3d_backend_assets
add_subdirectory(render_graph)      # chronon3d_graph
add_subdirectory(runtime)           # chronon3d_runtime
add_subdirectory(extension)        # chronon3d_extension (Fase 5)
add_subdirectory(backends/software) # chronon3d_backend_software
add_subdirectory(scene)             # chronon3d_scene
add_subdirectory(animations)        # chronon3d_animations
add_subdirectory(text)              # chronon3d_text_core

# ==============================================================================
# Final aggregate targets
# ==============================================================================

add_library(chronon3d_core INTERFACE)
target_link_libraries(chronon3d_core INTERFACE
    chronon3d_assets
    chronon3d_registry
    chronon3d_scene
    chronon3d_runtime
    chronon3d_graph
    chronon3d_cache
    chronon3d_animations
    chronon3d_core_impl
    chronon3d_text_core
    # chronon3d_backend_video is linked via chronon3d_software, not core.
    # Core should be dependency-minimal for server-side / headless use.
)

if(CHRONON3D_ENABLE_VIDEO)
    target_link_libraries(chronon3d_core INTERFACE chronon3d_backend_video)
endif()

add_library(chronon3d_software INTERFACE)
target_link_libraries(chronon3d_software INTERFACE
    chronon3d_backend_software
    chronon3d_backend_assets
    chronon3d_backend_image
)

if(CHRONON3D_ENABLE_VIDEO)
    target_link_libraries(chronon3d_software INTERFACE chronon3d_backend_video)
endif()

add_library(chronon3d_pipeline INTERFACE)
target_link_libraries(chronon3d_pipeline INTERFACE
    chronon3d_core
    chronon3d_software
)

# ==============================================================================
# Consumer SDK alias — external programs link against Chronon3D::SDK
# ==============================================================================
add_library(chronon3d_sdk INTERFACE)
target_include_directories(chronon3d_sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(chronon3d_sdk INTERFACE
    $<BUILD_INTERFACE:chronon3d_pipeline>
)
set_target_properties(chronon3d_sdk PROPERTIES EXPORT_NAME SDK)
add_library(Chronon3D::SDK ALIAS chronon3d_sdk)

# Keep the old alias for backward compatibility during migration.
add_library(Chronon3D_SDK ALIAS chronon3d_sdk)
