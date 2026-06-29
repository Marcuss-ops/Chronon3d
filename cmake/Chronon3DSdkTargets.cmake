# ==============================================================================
# cmake/Chronon3DSdkTargets.cmake — SDK consumer-facing targets
#
# PURPOSE
#   Single source of truth for the *consumer-facing* surface of the SDK:
#
#     • chronon3d_sdk_impl        — STATIC archive bundling every per-subsystem
#                                    OBJECT into `libchronon3d_sdk_impl.a`
#     • sdk_archive_merge target  — POST_BUILD helper that merges .o files
#                                    into the archive (CMake 3.25 cannot do
#                                    that natively for OBJECT libs)
#     • chronon3d_sdk (INTERFACE) — in-tree + install link closure for the
#                                    consumer.  $<BUILD_INTERFACE:…> consumes
#                                    `chronon3d_pipeline` (which pulls .o
#                                    files via $<TARGET_OBJECTS:…>); the
#                                    $<INSTALL_INTERFACE:…> side replaces it
#                                    with `chronon3d_sdk_impl`.
#     • Chronon3D::SDK alias        — the ONLY namespace-aliased public target.
#
# PRECONDITIONS  (enforced by the caller)
#   • cmake/Chronon3DRegistry.cmake has been included → CHRONON3D_REGISTRY_OBJECT_LIBS
#     is populated.
#   • All per-subsystem `add_subdirectory()` calls in src/CMakeLists.txt have
#     run, so every target listed in the registry either exists OR is gated
#     out by a feature option.  The `if(TARGET …)` guards below handle both
#     cases via the foreach-if-TARGET idiom (TICKET-011 cmake-boundary).
#
# INCLUDED FROM
#   src/CMakeLists.txt (after every add_subdirectory(src/<subsystem>) call,
#   because target_link_libraries(target_sources) requires the targets
#   to exist on the graph).
#
# CONTRACT  (TICKET-011 cmake-boundary — SINGLE aggregated archive)
#   The export walker must register exactly one .a file under
#   <prefix>/<libdir>: `libchronon3d_sdk_impl.a`.  Per-subsystem OBJECT
#   targets are listed in the export set so that `install(EXPORT …)` does
#   not flag them as missing, but they have no ARCHIVE/LIBRARY output of
#   their own (they are OBJECT).
# ==============================================================================

# ==============================================================================
# chronon3d_sdk_impl — the single aggregated STATIC archive
# ==============================================================================
# VERIFIED 2026-06-29 on CMake 3.25.1: target_link_libraries(STATIC PRIVATE
# objlib) does NOT embed .o files into the .a archive — only the marker TU
# lands (1 object file, ~3 KB). The .o propagation works at final link time
# but the intermediate .a is empty.
# WORKAROUND: POST_BUILD custom command (sdk_archive_merge below) runs `ar qc`
# to manually merge all subsystem .o files into the archive. Upgrade to
# CMake ≥3.28 to remove this workaround (native aggregation is fixed upstream).
add_library(chronon3d_sdk_impl STATIC
    ${CMAKE_SOURCE_DIR}/src/sdk_impl_marker.cpp
)

# ── Archive aggregation — derived from central registry ──────────────
# Every OBJECT library registered in cmake/Chronon3DRegistry.cmake is
# automatically linked into the single libchronon3d_sdk_impl.a archive.
# The foreach-if-TARGET loop handles conditional subsystems (targets not
# created in this configuration are silently skipped).
foreach(_reg_obj IN LISTS CHRONON3D_REGISTRY_OBJECT_LIBS)
    if(TARGET ${_reg_obj})
        target_link_libraries(chronon3d_sdk_impl PRIVATE ${_reg_obj})
    endif()
endforeach()

# ── POST_BUILD manifest: registry-driven enumeration ─────────────────
# Two-step enumeration keeps the .o list reproducible:
#
#   1) Whole-tree scan restricted to the same paths the original
#      implementation used:
#        file(GLOB_RECURSE ${CMAKE_BINARY_DIR}/src/*.cpp.o ${BUILD_DIR}/content/*.cpp.o)
#      A recursive glob from `${CMAKE_BINARY_DIR}/*.cpp.o` alone is fragile
#      across CMake versions; the two explicit patterns are known-good.
#
#   2) Registry filter: keep entries whose path contains
#      `/<reg_target>.dir/` for any target in CHRONON3D_REGISTRY_OBJECT_LIBS
#      plus chronon3d_sdk_impl (the marker).  Adding a new OBJECT lib to the
#      registry is therefore the single line of maintenance needed.
#
# Trade-off documented in commit history; supports unity-build=OFF
# (`linux-ci` / install-boundary CI).  The merge script handles
# unity-build=ON gracefully via `if(EXISTS …)` in the .cmake script.
if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(FATAL_ERROR
        "sdk_archive_merge: registry-driven manifest assumes Ninja 'CMakeFiles/<target>.dir/*.cpp.o' intermediate-dir layout "
        "(have generator '${CMAKE_GENERATOR}').")
endif()

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

# Stale-manifest guard (canonical semantics; see commit history).
#
# ORIGINAL bug: the guard fired at CONFIGURE time whenever the manifest held
# fewer than 2 entries.  That broke the canonical fresh-checkout flow
# (`cmake --preset linux-ci --fresh`) because BEFORE any source is compiled,
# the whole-tree GLOB_RECURSE returns 0 files; only the marker TU is present,
# so count=1 looked indistinguishable from a torn-archive state.
#
# CORRECT semantics: the DoD "marker + >= 1 subsystem" check should ONLY
# fail HARD when at least one .cpp.o IS on disk but the registry-driven
# filter dropped the manifest below threshold.  That is the "torn" case
# (e.g. a source was renamed but the old .cpp.o still lingers).
#
# On a FRESH configure (GLOB=0), the marker-only manifest is the legitimate
# pre-build state and the `sdk_archive_merge` build-step populates it
# naturally via the dependency chain
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

# ── POST_BUILD target: merge subsystem .o files into the archive ─────
# CMake 3.25 does not aggregate OBJECT .o into STATIC archives natively;
# this custom target passes a deterministic, registry-derived manifest
# (computed above) to the helper script which calls `ar crs` (response-file
# invocation) to build `libchronon3d_sdk_impl.a` from scratch.
add_custom_target(sdk_archive_merge
    COMMAND ${CMAKE_COMMAND} -E echo "Merging subsystem .o files into SDK archive..."
    COMMAND ${CMAKE_COMMAND} -DARCHIVE="$<TARGET_FILE:chronon3d_sdk_impl>" -DOBJECT_FILES="${_sdk_archive_obj_files}" -DAR="${CMAKE_AR}" -P "${CMAKE_SOURCE_DIR}/cmake/sdk_archive_merge.cmake"
    COMMAND ${CMAKE_COMMAND} -E echo "=== Archive object count after merge ==="
    COMMAND ${CMAKE_AR} t "$<TARGET_FILE:chronon3d_sdk_impl>"
    COMMENT "Merging all subsystem OBJECT .o files into libchronon3d_sdk_impl.a (registry-driven manifest)"
    DEPENDS chronon3d_sdk_impl ${CHRONON3D_REGISTRY_OBJECT_LIBS}
)

# Register the merge target as a dependency of `cmake --install`, so
# downstream consumers always see a populated archive.
install(CODE "execute_process(COMMAND \${CMAKE_COMMAND} --build \${CMAKE_BINARY_DIR} --target sdk_archive_merge)")

set_target_properties(chronon3d_sdk_impl PROPERTIES EXPORT_NAME SDKImpl)

# ==============================================================================
# chronon3d_sdk + Chronon3D::SDK alias
# ==============================================================================
# Two-stage link closure (TICKET-011 cmake-boundary):
#   * BUILD_INTERFACE: chronon3d_pipeline — resolves raw .o files for in-tree
#     consumers (CLI, tests) via INTERFACE → OBJECT propagation.  No archive
#     needed; the linker pulls .o files directly.
#   * INSTALL_INTERFACE: chronon3d_sdk_impl — resolves the single installed
#     archive for downstream `find_package(Chronon3D)` consumers.  This is
#     the only file artifact in `<prefix>/<libdir>`.
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

# ── In-tree link closure — derived from central registry ──────────────
# Every OBJECT library registered in cmake/Chronon3DRegistry.cmake has
# its .o files propagated for in-tree consumers (CLI, tests) via
# $<TARGET_OBJECTS:…>.  The foreach-if-TARGET loop handles conditional
# subsystems automatically.  When you add a new OBJECT library, add its
# name to cmake/Chronon3DRegistry.cmake — this block picks it up automatically.
foreach(_reg_obj IN LISTS CHRONON3D_REGISTRY_OBJECT_LIBS)
    if(TARGET ${_reg_obj})
        target_sources(chronon3d_sdk INTERFACE
            $<BUILD_INTERFACE:$<TARGET_OBJECTS:${_reg_obj}>>)
    endif()
endforeach()

set_target_properties(chronon3d_sdk PROPERTIES EXPORT_NAME SDK)
add_library(Chronon3D::SDK ALIAS chronon3d_sdk)
