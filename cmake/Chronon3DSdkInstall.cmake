# ==============================================================================
# cmake/Chronon3DSdkInstall.cmake — SDK install / export / package-config
#
# PURPOSE
#   Single source of truth for the *install* layer of the SDK:
#     • Explicit public header FILE_SET.
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
# Explicit manifest only; NO GLOB. The OPP-internal surface and any header
# omitted from this manifest are deliberately not installed.
include("${CMAKE_SOURCE_DIR}/cmake/Chronon3DPublicHeaders.cmake")

# Core SDK/transitive public surface.
target_sources(chronon3d_sdk INTERFACE
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS "${CMAKE_SOURCE_DIR}/include"
    FILES ${CHRONON3D_PUBLIC_HEADERS}
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

install(TARGETS ${_chronon3d_install_targets}
    EXPORT Chronon3DTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(TARGET chronon3d_c)
    install(TARGETS chronon3d_c
        EXPORT Chronon3DTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(FILES
        "${CMAKE_SOURCE_DIR}/include/chronon3d/c_api/chronon3d.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/chronon3d/c_api"
    )
endif()

if(TARGET chronon3d_cli)
    set_target_properties(chronon3d_cli PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
    )
    install(TARGETS chronon3d_cli
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

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

install(FILES
    "${CMAKE_SOURCE_DIR}/schemas/chronon.render-plan.v2.schema.json"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/chronon3d/schemas
)

# ── pkg-config metadata ───────────────────────────────────────────────
# For non-CMake consumers (Meson, Makefile, Rust build.rs, Go/cgo) that link
# the C ABI shared library directly:
#     pkg-config --cflags --libs chronon3d
# Describes libchronon3d_c only; C++ consumers keep using find_package().
if(TARGET chronon3d_c)
    # Relative path from ${CMAKE_INSTALL_LIBDIR}/pkgconfig back up to the
    # install prefix, so the generated .pc stays relocatable via ${pcfiledir}
    # (see cmake/chronon3d.pc.in).
    file(RELATIVE_PATH _chronon3d_pc_prefix_rel
         "/${CMAKE_INSTALL_LIBDIR}/pkgconfig" "/")
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/chronon3d.pc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/chronon3d.pc"
        @ONLY
    )
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/chronon3d.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
    )
endif()

# ── Vendor third-party deps (self-contained C++ package) ──────────────
# Chronon3D::SDK is a static archive whose INTERFACE_LINK_LIBRARIES carry
# third-party targets (glm/fmt/spdlog/harfbuzz/freetype/blend2d/TBB/…).  By
# default Chronon3DConfig.cmake resolves those via find_dependency(), which
# forces the consumer to point CMAKE_PREFIX_PATH at a vcpkg triplet.  To make
# `find_package(Chronon3D)` work with NO vcpkg, copy the whole triplet into the
# install prefix at third_party/<triplet> and let Chronon3DConfig.cmake resolve
# the deps from there (see the vendored-resolution block in
# cmake/Chronon3DConfig.cmake.in).
#
# Disable with -DCHRONON3D_VENDOR_THIRD_PARTY=OFF for lean dev installs that
# keep relying on the consumer's own vcpkg triplet.
option(CHRONON3D_VENDOR_THIRD_PARTY
    "Vendor the vcpkg triplet into the install so C++ consumers don't need vcpkg" ON)

if(CHRONON3D_VENDOR_THIRD_PARTY AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(_chronon3d_vendor_src "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    if(IS_DIRECTORY "${_chronon3d_vendor_src}")
        # Trailing slash copies the triplet's contents (include/, lib/, share/,
        # debug/) into <prefix>/third_party/<triplet>/, preserving the layout
        # that vcpkg's CONFIG files expect (${_VCPKG_INSTALLED_DIR}/
        # ${VCPKG_TARGET_TRIPLET}/…).
        install(DIRECTORY "${_chronon3d_vendor_src}/"
            DESTINATION "third_party/${VCPKG_TARGET_TRIPLET}"
            USE_SOURCE_PERMISSIONS
        )
    else()
        message(WARNING "CHRONON3D_VENDOR_THIRD_PARTY=ON but the vcpkg triplet was not found: ${_chronon3d_vendor_src}")
    endif()
endif()

# ── SDK PACKAGE MANIFEST + VERSION (the stage IS the product) ──────────────
# A self-describing install stage carries manifest.json (the SDK PACKAGE
# MANIFEST — product/version/abi/schema/platform/build/features — NOT the
# per-render receipt) and VERSION at its root, so `cmake --install --prefix`
# already produces the distributable product.  tools/build_sdk_bundle.sh only
# wraps that stage into chronon-sdk-<platform>/ and copies these two files
# verbatim; the single source of truth for their content lives here.

string(TOLOWER "${CMAKE_SYSTEM_NAME}" _chronon3d_sdk_os)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _chronon3d_sdk_arch)

find_package(Git QUIET)
set(_chronon3d_sdk_git_sha "unknown")
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE _chronon3d_sdk_git_rc
        OUTPUT_VARIABLE _chronon3d_sdk_git_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT _chronon3d_sdk_git_rc EQUAL 0 OR _chronon3d_sdk_git_sha STREQUAL "")
        set(_chronon3d_sdk_git_sha "unknown")
    endif()
endif()

if(CHRONON3D_ENABLE_VULKAN)
    set(_chronon3d_sdk_vulkan_flag "true")
else()
    set(_chronon3d_sdk_vulkan_flag "false")
endif()
if(CHRONON3D_ENABLE_TEXT)
    set(_chronon3d_sdk_text_flag "true")
else()
    set(_chronon3d_sdk_text_flag "false")
endif()
if(CMAKE_BUILD_TYPE)
    set(_chronon3d_sdk_build_type "${CMAKE_BUILD_TYPE}")
else()
    set(_chronon3d_sdk_build_type "Release")
endif()

set(_chronon3d_sdk_manifest "${CMAKE_CURRENT_BINARY_DIR}/sdk_manifest.json")
file(WRITE "${_chronon3d_sdk_manifest}"
"{\n"
"  \"product\": \"chronon3d-sdk\",\n"
"  \"version\": \"${PROJECT_VERSION}\",\n"
"  \"abi\": 2,\n"
"  \"render_plan_schema\": \"chronon.render-plan.v2\",\n"
"  \"platform\": \"${_chronon3d_sdk_os}-${_chronon3d_sdk_arch}\",\n"
"  \"build\": {\n"
"    \"git_sha\": \"${_chronon3d_sdk_git_sha}\",\n"
"    \"type\": \"${_chronon3d_sdk_build_type}\"\n"
"  },\n"
"  \"features\": {\n"
"    \"software\": true,\n"
"    \"vulkan\": ${_chronon3d_sdk_vulkan_flag},\n"
"    \"text\": ${_chronon3d_sdk_text_flag}\n"
"  }\n"
"}\n"
)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sdk_version.txt"
"${PROJECT_VERSION}\n"
)

install(FILES "${_chronon3d_sdk_manifest}" DESTINATION . RENAME manifest.json)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sdk_version.txt"
        DESTINATION . RENAME VERSION)
