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
# VERIFIED 2026-06-29 on CMake 3.25.1: target_link_libraries(STATIC PRIVATE
# objlib) does NOT embed .o files into the .a archive — only the marker TU
# lands (3.1 KB archive, 1 object file). The .o propagation works at final
# link time (executables resolve symbols correctly) but the intermediate
# .a is empty.
#
# WORKAROUND: A POST_BUILD custom command (sdk_archive_merge below) runs
# `ar qc` to manually merge all subsystem .o files into the archive.
# Upgrade to CMake ≥3.28 to remove this workaround (native aggregation
# is fixed upstream).
#
# Marker TU (`sdk_impl_marker.cpp`) is the canonical CMake idiom for
# forcing a `STATIC` archive to be emitted when the target has no
# own source files.  See commentary at the top of that file.
# ==============================================================================
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

# ── POST_BUILD: merge subsystem .o files into the archive ──────────
# CMake 3.25 does not aggregate OBJECT .o files into STATIC archives.
# This custom target collects all compiled .o files from the build tree
# and appends them to libchronon3d_sdk_impl.a via `ar qc`. Depends on
# all registered OBJECT targets to ensure compilation completes first.
# ── Deterministic object manifest for sdk_archive_merge ─────────────
# Two-step registry-driven enumeration to keep the .o list reproducible:
#
# 1) Whole-tree scan:
#      file(GLOB_RECURSE var ${CMAKE_BINARY_DIR}/*.cpp.o)
#      — known-good pattern (Ninja places ALL target .cpp.o files under
#      <build>/**/CMakeFiles/<target>.dir/*.cpp.o, so a recursive glob
#      over <build> picks them up).
#
# 2) Registry filter:
#      Keep entries whose path contains '/<reg_target>.dir/' for any
#      target in CHRONON3D_REGISTRY_OBJECT_LIBS + chronon3d_sdk_impl
#      (the marker).  This eliminates stale or non-SDK .cpp.o files
#      (tests/, third-party intermediate, etc.) and ties the manifest
#      to the central registry — adding a new OBJECT lib = single line
#      of registry.
#
# Trade-off documented: this replaces the original
# `file(GLOB_RECURSE ${BUILD_DIR}/src/*.cpp.o ${BUILD_DIR}/content/*.cpp.o)`
# in `sdk_archive_merge.cmake`.  The new approach is REFRESH-SAFE
# (deterministic via the registry filter) and GLOB lives outside the
# -P script (which has no target scope).  Requires a build artifact
# to exist on disk (otherwise the glob returns empty — see early-fail
# guard below).
#
# NOTE: works for unity-build=OFF (linux-ci / install-boundary CI).
# Under unity-build=ON, sources are batched and individual .cpp.o
# paths may not exist — the merge step handles that gracefully via
# `if(EXISTS …)` in the script.
if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(FATAL_ERROR
        "sdk_archive_merge: registry-driven manifest assumes Ninja 'CMakeFiles/<target>.dir/*.cpp.o' intermediate-dir layout "
        "(have generator '${CMAKE_GENERATOR}').")
endif()

# Whole-tree scan restricted to the same paths the original pre-Fase-1
# GLOB_RECURSE used.  Recursive glob from `${CMAKE_BINARY_DIR}/*.cpp.o`
# alone is fragile across CMake versions (some versions don't recurse
# from a single absolute path); the two explicit patterns mirror the
# original layout and are known-good from the Fase 0 baseline run.
file(GLOB_RECURSE _all_cpp_o
    "${CMAKE_BINARY_DIR}/src/*.cpp.o"
    "${CMAKE_BINARY_DIR}/content/*.cpp.o"
)

set(_sdk_archive_obj_files "")
foreach(_obj IN LISTS _all_cpp_o)
    set(_keep FALSE)
    foreach(_reg_obj IN LISTS CHRONON3D_REGISTRY_OBJECT_LIBS)
        string(FIND "${_obj}" "/${_reg_obj}.dir/" _hit)
        if(_hit GREATER -1)
            set(_keep TRUE)
            break()
        endif()
    endforeach()
    if(_keep)
        list(APPEND _sdk_archive_obj_files "${_obj}")
    endif()
endforeach()

# Marker TU: chronon3d_sdk_impl is a STATIC library (not in OBJECT_LIBS),
# but its single source produces a per-source .o under the same Ninja
# layout — at
#   <build>/src/CMakeFiles/chronon3d_sdk_impl.dir/sdk_impl_marker.cpp.o
list(APPEND _sdk_archive_obj_files
    "${CMAKE_BINARY_DIR}/src/CMakeFiles/chronon3d_sdk_impl.dir/sdk_impl_marker.cpp.o")

list(REMOVE_DUPLICATES _sdk_archive_obj_files)
list(SORT _sdk_archive_obj_files)
list(LENGTH _sdk_archive_obj_files _sdk_archive_count)

# Stale-manifest guard (fix: gate on non-empty GLOB).
#
# The original guard fired at CONFIGURE time whenever the manifest
# held fewer than 2 entries.  That broke the canonical fresh-checkout
# flow (`cmake --preset linux-ci --fresh`) because BEFORE any source
# is compiled, the whole-tree GLOB_RECURSE returns 0 files; only the
# marker TU is present, so count=1 looked indistinguishable from a
# torn-archive state.
#
# Correct semantics: the DoD "marker + >= 1 subsystem" check should
# ONLY fail HARD when at least one .cpp.o IS on disk but the
# registry-driven filter dropped the manifest below threshold.  That
# is the "torn" case (e.g. a source was renamed but the old .cpp.o
# still lingers in CMakeFiles/<target>.dir/) -- the genuine stale
# registry we want to surface loud.
#
# On a FRESH configure (GLOB=0), the marker-only manifest is the
# legitimate pre-build state and the `sdk_archive_merge` build-step
# populates it naturally via the dependency chain
# `DEPENDS chronon3d_sdk_impl ${CHRONON3D_REGISTRY_OBJECT_LIBS}`.
list(LENGTH _all_cpp_o _all_cpp_o_count)
if(_all_cpp_o_count GREATER 0 AND _sdk_archive_count LESS 2)
    message(FATAL_ERROR
        "sdk_archive_merge: stale manifest.  Found ${_all_cpp_o_count} raw .cpp.o file(s) "
        "on disk but the registry-driven filter retained only ${_sdk_archive_count} "
        "(marker TU only).  This typically indicates a renamed/removed source whose "
        "old .cpp.o lingered in CMakeFiles/<target>.dir/.  Clean the build dir "
        "(`rm -rf build/<dir>`) and re-run the build.")
endif()

if(_sdk_archive_count LESS 2)
    message(STATUS
        "sdk_archive_merge: ${_sdk_archive_count} .o object(s) derived from registry "
        "(fresh build; _all_cpp_o=${_all_cpp_o_count}; populate via "
        "`cmake --build <build-dir> --target sdk_archive_merge`)")
else()
    message(STATUS "sdk_archive_merge: ${_sdk_archive_count} .o objects derived from registry (manifest)")
endif()

# ── POST_BUILD: merge subsystem .o files into the archive ──────────
# CMake 3.25 does not aggregate OBJECT .o files into STATIC archives.
# This custom target passes a deterministic, registry-derived manifest
# (computed above) to the helper script, which calls `ar crs` to build
# libchronon3d_sdk_impl.a from scratch.  No file globbing anywhere.
add_custom_target(sdk_archive_merge
    COMMAND ${CMAKE_COMMAND} -E echo "Merging subsystem .o files into SDK archive..."
    COMMAND ${CMAKE_COMMAND} -DARCHIVE="$<TARGET_FILE:chronon3d_sdk_impl>" -DOBJECT_FILES="${_sdk_archive_obj_files}" -DAR="${CMAKE_AR}" -P "${CMAKE_SOURCE_DIR}/cmake/sdk_archive_merge.cmake"
    COMMAND ${CMAKE_COMMAND} -E echo "=== Archive object count after merge ==="
    COMMAND ${CMAKE_AR} t "$<TARGET_FILE:chronon3d_sdk_impl>"
    COMMENT "Merging all subsystem OBJECT .o files into libchronon3d_sdk_impl.a (registry-driven manifest)"
    DEPENDS chronon3d_sdk_impl ${CHRONON3D_REGISTRY_OBJECT_LIBS}
)

# Also register the merge target as a dependency of the install step
install(CODE "execute_process(COMMAND \${CMAKE_COMMAND} --build \${CMAKE_BINARY_DIR} --target sdk_archive_merge)")

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

# PR-7c: legacy `Chronon3D_SDK` alias retired (canonical is `Chronon3D::SDK` above).

# Expressions V2 (Path-B engine) is now quarantined under
# experimental/expressions/ and is gated by CHRONON3D_BUILD_EXPERIMENTAL.
# See the root CMakeLists.txt and docs/EXPRESSIONS_V2_PROMOTION.md.
