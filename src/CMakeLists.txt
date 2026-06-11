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
# ==============================================================================
add_library(chronon3d_ffmpeg_light INTERFACE)
target_link_libraries(chronon3d_ffmpeg_light INTERFACE swscale avutil)
add_library(chronon3d_ffmpeg_full  INTERFACE)
target_link_libraries(chronon3d_ffmpeg_full  INTERFACE swscale avcodec avformat avutil)



# ==============================================================================
# Base Interface (must be defined before any add_subdirectory that uses it)
# ==============================================================================
add_library(chronon3d INTERFACE)
target_include_directories(chronon3d INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/deps/include>
)
target_compile_definitions(chronon3d INTERFACE 
    CHRONON3D_GIT_COMMIT="${CHRONON3D_GIT_COMMIT}"
    $<$<BOOL:${MSVC}>:_DISABLE_STRING_VIEW_FIND_FIRST_NOT_OF_OPTIMIZATION>
)
option(CHRONON3D_ENABLE_MESH "Enable mesh processing support" ON)

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
add_subdirectory(backends/text)     # chronon3d_backend_text
add_subdirectory(backends/assets)   # chronon3d_backend_assets
add_subdirectory(render_graph)      # chronon3d_graph
add_subdirectory(runtime)           # chronon3d_runtime
add_subdirectory(extension)         # chronon3d_extension
add_subdirectory(backends/software) # chronon3d_backend_software
add_subdirectory(scene)             # chronon3d_scene
add_subdirectory(animations)        # chronon3d_animations

# ==============================================================================
# Final aggregate targets
# ==============================================================================

# --- Aggregati Architetturali finali ---
# Aggregati Architetturali finali
# WHOLE_ARCHIVE removed — all built-in compositions now use explicit
# registration via register_builtin_compositions().
add_library(chronon3d_core INTERFACE)
target_link_libraries(chronon3d_core INTERFACE
    chronon3d_extension
    chronon3d_assets
    chronon3d_registry
    chronon3d_scene
    chronon3d_runtime
    chronon3d_graph
    chronon3d_cache
    chronon3d_animations
    chronon3d_core_impl
    chronon3d_backend_video
)

add_library(chronon3d_software INTERFACE)
target_link_libraries(chronon3d_software INTERFACE
    chronon3d_backend_software
    chronon3d_backend_video
    chronon3d_backend_assets
    chronon3d_backend_image
)

add_library(chronon3d_pipeline INTERFACE)
target_link_libraries(chronon3d_pipeline INTERFACE
    chronon3d_core
    chronon3d_software
    Taskflow::Taskflow
    concurrentqueue::concurrentqueue
    yuv
)
