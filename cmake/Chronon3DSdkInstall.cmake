# ==============================================================================
# cmake/Chronon3DSdkInstall.cmake — SDK install / export / package-config
#
# PURPOSE
#   Single source of truth for the *install* layer of the SDK:
#     • Public headers (kept in the root; this module does not duplicate
#       the verbatim copy into <prefix>/include/, see AGENTS.md "single
#       source of truth").  The HEADER_INSTALL line is duplicated below
#       for module isolation so the consumer can `include()` only this
#       file and reproduce the public layout in isolation if needed.
#     • install(TARGETS) for every OBJECT + INTERFACE/STATIC aggregate
#       listed in cmake/Chronon3DRegistry.cmake.
#     • install(EXPORT Chronon3DTargets) → Chronon3DTargets.cmake.
#     • configure_package_config_file → Chronon3DConfig.cmake.
#     • write_basic_package_version_file → Chronon3DConfigVersion.cmake.
#
# PRECONDITIONS  (enforced by the caller)
#   • cmake/Chronon3DRegistry.cmake has been included → the two lists
#     CHRONON3D_REGISTRY_OBJECT_LIBS and CHRONON3D_REGISTRY_INTERFACE_LIBS
#     are populated.
#   • cmake/Chronon3DSdkTargets.cmake has been included from
#     src/CMakeLists.txt — every chronon3d_* target listed below
#     already exists on the graph (or is gated out by feature options).
#
# INCLUDED FROM
#   The root CMakeLists.txt after all add_subdirectory() calls have
#   registered their targets.  The include() must come BEFORE the
#   `chronon3d_dev_fast` aggregate target, Hygiene include, and the
#   `chronon3d_architecture_check` custom_target block at the bottom
#   of the root file.
#
# CONTRACT  (TICKET-011 cmake-boundary — PUBLIC SURFACE)
#   The exported targets intentionally DO NOT use
#   `NAMESPACE Chronon3D::`.  Combined with the single public alias
#   `add_library(Chronon3D::SDK ALIAS chronon3d_sdk)` registered in
#   cmake/Chronon3DSdkTargets.cmake, this means the documented public
#   API surface is EXACTLY `Chronon3D::SDK`.  Inner targets remain
#   technically importable (for transitive link closure) under their
#   raw names (chronon3d_cache etc.), but they are NOT under the
#   Chronon3D:: prefix and should never be linked directly by
#   consumers — the export graph resolves transitively through
#   Chronon3D::SDK.
#
# Layout produced under `--prefix`:
#   <prefix>/include/chronon3d/*.hpp                    (public headers)
#   <prefix>/<libdir>/libchronon3d_sdk_impl.a           (single ARCHIVE
#                                                            artifact — the
#                                                            only .a file
#                                                            in <libdir>)
#   <prefix>/<libdir>/cmake/Chronon3D/
#     Chronon3DConfig.cmake        (entry: find_dependency + include targets)
#     Chronon3DConfigVersion.cmake (version compatibility gate)
#     Chronon3DTargets.cmake       (imported targets, raw names — only
#                                   chronon3d_sdk is publicly aliased as
#                                   Chronon3D::SDK via Chronon3DSdkTargets)
# ==============================================================================

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ── Public headers ─────────────────────────────────────────────────────
# Installed verbatim under <prefix>/include/chronon3d/. Single source of
# truth; the root CMakeLists.txt does NOT emit a duplicate install()
# rule — this is the only place the public-header layout is declared.
#
# V0.1 freeze policy (build(sdk) install policy via FILE_SET +
# explicit Chronon3DPublicHeaders manifest):  NO GLOB.  The OPP-internal
# surface (src/*/include_private/) and the OPP-public surface
# (include/chronon3d/{advanced,api,..} excluding the manifest entries)
# are deliberately NOT installed.  Only the 7 explicit headers listed in
# cmake/Chronon3DPublicHeaders.cmake travel with the SDK payload,
# delivered to downstream consumers via a `FILE_SET public_headers`
# registered on the `chronon3d_sdk` INTERFACE target.
include("${CMAKE_SOURCE_DIR}/cmake/Chronon3DPublicHeaders.cmake")

# Register the explicit manifest on the public INTERFACE target so the
# in-tree consumers (CLI, tests, content) get the same `#include`
# resolution surface as downstream `find_package(Chronon3D)` users.
# BASE_DIRS uses the absolute include root so path resolution does not
# depend on CMAKE_CURRENT_SOURCE_DIR (this call site lives inside
# cmake/Chronon3DSdkInstall.cmake, not the source root).
target_sources(chronon3d_sdk INTERFACE
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/include"
    FILES ${CHRONON3D_PUBLIC_HEADERS}
)

# ── Aggregate install + export target list ────────────────────────────
# Derived from the central registry.  Replaces the previous hand-
# maintained ~40-entry list (root CMakeLists.txt before this module).
# Conditional targets (text_core, backend_video, content, diagnostics,
# blend2d_paint, ffmpeg_*, etc.) are present in the registry list
# unconditionally — the `if(TARGET …)` filter below drops those that
# were not created in the current configuration, so the same registry-
# driven derivation works without any conditional appends.
set(_chronon3d_install_targets_raw
    ${CHRONON3D_REGISTRY_OBJECT_LIBS}
    ${CHRONON3D_REGISTRY_INTERFACE_LIBS}
)

# Filter: skip targets that were never created in this configuration
# (e.g. chronon3d_text_core when CHRONON3D_ENABLE_TEXT=OFF,
# chronon3d_content when neither BUILD_CONTENT nor BUILD_DIAGNOSTICS
# is set, chronon3d_diagnostics / chronon3d_backend_software_diagnostics
# when BUILD_DIAGNOSTICS=OFF, etc.).  install(TARGETS …) skips missing
# targets silently, but emitting the filtered list keeps the export
# walker deterministic and makes the install set audit-friendly in CI.
set(_chronon3d_install_targets "")
foreach(_tgt IN LISTS _chronon3d_install_targets_raw)
    if(TARGET ${_tgt})
        list(APPEND _chronon3d_install_targets ${_tgt})
    endif()
endforeach()

# Install + export.  EXPORT NAME mapping:
#
# FILE_SET HEADERS clause: the `chronon3d_sdk` target (in
# `_chronon3d_install_targets` since it is registered in
# CHRONON3D_REGISTRY_INTERFACE_LIBS) carries the public_headers
# FILE_SET declared above.  CMake silently skips the FILE_SET clause
# for the other exported targets that have none.  Combined into a
# SINGLE install(TARGETS …) call to avoid duplicate `Chronon3DTargets`
# export sets (which CMake rejects as `install(EXPORT) given unknown
# / duplicate export`).
install(TARGETS ${_chronon3d_install_targets}
    EXPORT Chronon3DTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    # `chronon3d_sdk` carries the public_headers FILE_SET declared via
    # target_sources() above. CMake's export graph validates that EVERY
    # interface file set on a target is explicitly installed; the
    # conventional default CMake FILE_SET name is HEADERS, but this
    # target uses the semantic name `public_headers` (TICKET-011 —
    # PUBLIC SURFACE manifest). The clause is silently skipped for
    # targets that do not define a matching file set. Add additional
    # `FILE_SET <name> DESTINATION ...` lines here when new subsets
    # are introduced through the registry.
    FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Export internal targets WITHOUT NAMESPACE — they remain importable so
# the transitive link closure resolves correctly through Chronon3D::SDK,
# but the public `Chronon3D::` namespace prefix yields ONLY the SDK
# alias (`add_library(Chronon3D::SDK ALIAS chronon3d_sdk)` in
# cmake/Chronon3DSdkTargets.cmake).  Consumers who try to link an
# internal target by its raw name (chronon3d_cache etc.) get a CMake
# error because there's no Chronon3D::chronon3d_cache alias.
install(EXPORT Chronon3DTargets
    FILE Chronon3DTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Chronon3D
)

# ── Package config + version file ─────────────────────────────────────
# Installable config: substitutes @PACKAGE_INIT@ / @OPTIONAL_…@
# placeholders in the .in template.
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/Chronon3DConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Chronon3DConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Chronon3D
)

# Version file — consumers gate find_package() calls on COMPATIBILITY.
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/Chronon3DConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/Chronon3DConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/Chronon3DConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Chronon3D
)
