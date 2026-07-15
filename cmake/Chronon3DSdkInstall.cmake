# ==============================================================================
# cmake/Chronon3DSdkInstall.cmake — SDK install / export / package-config
#
# PURPOSE
#   Single source of truth for the *install* layer of the SDK:
#     • Explicit public header FILE_SETs (core + focused authoring subset).
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
#   registered their targets. The include() must come BEFORE the
#   `chronon3d_dev_fast` aggregate target, Hygiene include, and the
#   `chronon3d_architecture_check` custom_target block at the bottom
#   of the root file.
#
# CONTRACT  (TICKET-011 cmake-boundary — PUBLIC SURFACE)
#   The exported targets intentionally DO NOT use `NAMESPACE Chronon3D::`.
#   Combined with the single public alias `Chronon3D::SDK`, the documented
#   public link surface remains exactly one target. Focused header FILE_SETs
#   do not create additional link targets or umbrella headers.
# ==============================================================================

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ── Public headers ─────────────────────────────────────────────────────
# Explicit manifests only; NO GLOB. The OPP-internal surface and any header
# omitted from these manifests are deliberately not installed.
include("${CMAKE_SOURCE_DIR}/cmake/Chronon3DPublicHeaders.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/Chronon3DAuthoringPublicHeaders.cmake")

# Core SDK/transitive public surface.
target_sources(chronon3d_sdk INTERFACE
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/include"
    FILES ${CHRONON3D_PUBLIC_HEADERS}
)

# Focused authoring surface. This enables the documented explicit includes:
# authoring/asset.hpp, authoring/layer.hpp, authoring/text.hpp and their exact
# transitive authoring dependencies without introducing an umbrella header.
target_sources(chronon3d_sdk INTERFACE
    FILE_SET authoring_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/include"
    FILES ${CHRONON3D_AUTHORING_PUBLIC_HEADERS}
)

# ── Aggregate install + export target list ────────────────────────────
# Derived from the central registry. Conditional targets are filtered by
# existence so one registry drives every supported feature configuration.
set(_chronon3d_install_targets_raw
    ${CHRONON3D_REGISTRY_OBJECT_LIBS}
    ${CHRONON3D_REGISTRY_INTERFACE_LIBS}
)

set(_chronon3d_install_targets "")
foreach(_tgt IN LISTS _chronon3d_install_targets_raw)
    if(TARGET ${_tgt})
        list(APPEND _chronon3d_install_targets ${_tgt})
    endif()
endforeach()

# Install + export both explicit header subsets from the single public target.
install(TARGETS ${_chronon3d_install_targets}
    EXPORT Chronon3DTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILE_SET authoring_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Export internal targets WITHOUT NAMESPACE — they remain importable so the
# transitive link closure resolves through Chronon3D::SDK, while the public
# Chronon3D:: namespace still exposes only the SDK alias.
install(EXPORT Chronon3DTargets
    FILE Chronon3DTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Chronon3D
)

# ── Package config + version file ─────────────────────────────────────
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/Chronon3DConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Chronon3DConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Chronon3D
)

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
