# ==============================================================================
# cmake/Chronon3DSdkTargets.cmake — SDK consumer-facing targets
#
# PURPOSE
#   Single source of truth for the *target definitions* of the SDK's
#   consumer-facing surface:
#
#     • chronon3d_sdk_impl        — STATIC archive bundling every per-subsystem
#                                    OBJECT into `libchronon3d_sdk_impl.a`
#                                    (manifest, POST_BUILD merge, install-code
#                                    hook all live in `cmake/Chronon3DSdkArchive.cmake`)
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
# but the intermediate .a is empty. The full ARCHIVE mechanics (manifest
# computation, sdk_archive_merge POST_BUILD target, install-code hook) live
# in `cmake/Chronon3DSdkArchive.cmake`, included below.
#
# WORKAROUND for native aggregation: see header of
# cmake/Chronon3DSdkArchive.cmake (TICKET-011 cmake-boundary contract).
# Upgrade to CMake ≥3.28 to remove the workaround.
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

# Archive mechanics (manifest + POST_BUILD merge + install-code hook).
include(${CMAKE_SOURCE_DIR}/cmake/Chronon3DSdkArchive.cmake)

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
