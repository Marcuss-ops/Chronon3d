# ==============================================================================
# src/CMakeLists.txt — Thin dispatcher
#
# Each per-subsystem CMakeLists.txt defines its own OBJECT library target(s).
# This file handles:
#   1) External link flags (FFmpeg)
#   2) The base chronon3d INTERFACE target
#   3) add_subdirectory() calls in dependency order
#   4) Final aggregate INTERFACE targets (chronon3d_core, chronon3d_software,
#      chronon3d_pipeline, chronon3d_graph) — each propagates a per-subsystem
#      OBJECT set as a logical grouping
#   5) chronon3d_sdk_impl — the SINGLE STATIC archive that bundles every
#      per-subsystem OBJECT into `libchronon3d_sdk_impl.a` at install time.
#      This is the only RUNTIME/ARCHIVE artifact in `<prefix>/<libdir>`.  Each
#      per-subsystem OBJECT is build-time-only (no install artifact).
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
    $<INSTALL_INTERFACE:include>
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
add_subdirectory(timeline)          # TICKET-034: composes `<chronon3d/timeline/composition.cpp>` into chronon3d_scene OBJECT
add_subdirectory(animations)        # chronon3d_animations
if(CHRONON3D_ENABLE_TEXT)
    add_subdirectory(text)              # chronon3d_text_core
endif()

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
    # chronon3d_backend_video is linked via chronon3d_software, not core.
    # Core should be dependency-minimal for server-side / headless use.
)
if(TARGET chronon3d_text_core)
    target_link_libraries(chronon3d_core INTERFACE chronon3d_text_core)
endif()

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
# chronon3d_sdk_impl — the single aggregated STATIC archive
#
# TICKET-011 cmake-boundary (OBJECT lift final stage): bundles every
# per-subsystem OBJECT library into one STATIC that is the only RUNTIME
# /ARCHIVE artifact installed under <prefix>/<libdir>.  Each per-subsystem
# OBJECT is build-time only — it has no install output.  Conditional
# subsystems are gated with `if(TARGET …)` so disabled features do not
# produce dangling references.
#
# Marker TU (`sdk_impl_marker.cpp`) is the canonical CMake idiom for
# forcing a `STATIC` archive to be emitted when the target has no
# own source files.  See commentary at the top of that file.
# ==============================================================================
# TICKET-011 cmake-boundary (Branch C — target_link_libraries form):
# CMake 3.12+ NATIVELY aggregates OBJECT files into the consuming STATIC
# target's archive when OBJECT libs are linked via `target_link_libraries(
# chronon3d_sdk_impl PRIVATE objlib)`. The .o files propagate automatically
# via the OBJECT target's INTERFACE — no `$<TARGET_OBJECTS:...>` generator
# expressions needed. (Per `cmake(1)` docs §"LIBRARY OUTPUT", §"OBJECT
# library association" since CMake 3.12.)
#
# The marker TU (`sdk_impl_marker.cpp`) is now compiled DIRECTLY into the
# STATIC target (not via a separate `chronon3d_sdk_impl_marker` OBJECT
# library), to:
#   1. Force CMake to deduce `LINKER_LANGUAGE = CXX` from the concrete
#      source file — guarantees the `ar` rule is emitted even when the
#      aggregate targets are empty due to conditional subsystem gating.
#   2. Avoid duplicate-symbol errors that would arise from compiling the
#      same TU into a separate OBJECT lib AND linking it into the STATIC.
#
# This replaces the prior `target_sources(... PRIVATE $<TARGET_OBJECTS:x>)`
# pattern which silently skipped `ar` on CMake 3.25 even with
# `set_target_properties(LINKER_LANGUAGE CXX)` (the generator-expression
# form was emitting nothing at build time).
add_library(chronon3d_sdk_impl STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk_impl_marker.cpp
)
# ── Archive aggregation — derived from central registry ────────────
# Every OBJECT library registered in cmake/Chronon3DRegistry.cmake is
# automatically linked into the single libchronon3d_sdk_impl.a archive.
# The foreach-if-TARGET loop handles conditional subsystems (targets not
# created in this configuration are silently skipped).  The old manual
# list + conditional foreach blocks (~40 lines) are replaced by 6 lines.
foreach(_reg_obj IN LISTS CHRONON3D_REGISTRY_OBJECT_LIBS)
    if(TARGET ${_reg_obj})
        target_link_libraries(chronon3d_sdk_impl PRIVATE ${_reg_obj})
    endif()
endforeach()

set_target_properties(chronon3d_sdk_impl PROPERTIES EXPORT_NAME SDKImpl)

# ==============================================================================
# Consumer SDK alias — external programs link against Chronon3D::SDK
#
# Two-stage link closure (TICKET-011 cmake-boundary):
#   * BUILD_INTERFACE: chronon3d_pipeline — resolves raw .o files for in-tree
#     consumers (CLI, tests) via INTERFACE → OBJECT propagation.  No archive
#     needed; the linker pulls .o files directly.
#   * INSTALL_INTERFACE: chronon3d_sdk_impl — resolves the single installed
#     archive for downstream `find_package(Chronon3D)` consumers.  This is
#     the only file artifact in `<prefix>/<libdir>`.
# ==============================================================================
add_library(chronon3d_sdk INTERFACE)
target_include_directories(chronon3d_sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(chronon3d_sdk INTERFACE
    $<BUILD_INTERFACE:chronon3d_pipeline>
    $<INSTALL_INTERFACE:chronon3d_sdk_impl>
    # Public third-party deps that consumer headers transitively need.
    # The STATIC archive (chronon3d_sdk_impl) bundles all .o files but
    # does NOT propagate PUBLIC link deps from the aggregated OBJECT
    # libraries (PRIVATE link).  We explicitly list the non-OBJECT
    # third-party targets here so downstream find_package() consumers
    # get glm, fmt, spdlog, etc. include dirs and link flags.
    $<INSTALL_INTERFACE:glm::glm>
    $<INSTALL_INTERFACE:fmt::fmt>
    $<INSTALL_INTERFACE:spdlog::spdlog_header_only>
    $<INSTALL_INTERFACE:TBB::tbb>
    $<INSTALL_INTERFACE:magic_enum::magic_enum>
    $<INSTALL_INTERFACE:nlohmann_json::nlohmann_json>
    $<INSTALL_INTERFACE:xxHash::xxhash>
)
# ── In-tree link closure — derived from central registry ─────────────
# Every OBJECT library registered in cmake/Chronon3DRegistry.cmake has
# its .o files propagated for in-tree consumers (CLI, tests) via
# $<TARGET_OBJECTS:…>.  The foreach-if-TARGET loop handles conditional
# subsystems automatically.  The old manual MAINTENANCE CONTRACT block
# (~50 lines) is replaced by 6 lines below.
#
# MAINTENANCE: when you add a new OBJECT library, add its name to
# cmake/Chronon3DRegistry.cmake — this block picks it up automatically.
foreach(_reg_obj IN LISTS CHRONON3D_REGISTRY_OBJECT_LIBS)
    if(TARGET ${_reg_obj})
        target_sources(chronon3d_sdk INTERFACE
            $<BUILD_INTERFACE:$<TARGET_OBJECTS:${_reg_obj}>>)
    endif()
endforeach()
# Conditional OBJECT libraries — already handled by the foreach above.
# The old manual foreach block for conditional subsystems is retired.
set_target_properties(chronon3d_sdk PROPERTIES EXPORT_NAME SDK)
add_library(Chronon3D::SDK ALIAS chronon3d_sdk)

# Keep the old alias for backward compatibility during migration.
add_library(Chronon3D_SDK ALIAS chronon3d_sdk)

# Expressions V2 (Path-B engine) is now quarantined under
# experimental/expressions/ and is gated by CHRONON3D_BUILD_EXPERIMENTAL.
# See the root CMakeLists.txt and docs/EXPRESSIONS_V2_PROMOTION.md.
