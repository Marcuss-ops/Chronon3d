# ==============================================================================
# src/backends/software/diagnostics/CMakeLists.txt
#
# Diagnostics overlay files live in `chronon3d_backend_software_diagnostics`,
# a dedicated static target PRIVATE-linked into `chronon3d_backend_software`
# only when CHRONON3D_BUILD_DIAGNOSTICS=ON.  This mirrors the
# `chronon3d_diagnostics` split used for content/, keeping diagnostic code
# opt-in and isolated.
# ==============================================================================

if(CHRONON3D_BUILD_DIAGNOSTICS)
    # TICKET-011 cmake-boundary (OBJECT lift) — chronon3d_backend_software_diagnostics
    # is now an OBJECT library.  PRIVATE-linked above into chronon3d_backend_software
    # (also OBJECT); CMake propagates the .o files transitively, and the aggregate
    # chronon3d_sdk_impl.a bundles them at the static-link step.
    add_library(chronon3d_backend_software_diagnostics OBJECT
        ${CMAKE_CURRENT_SOURCE_DIR}/bbox_overlay.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/layout_preview_overlay.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nulls_overlay.cpp
        # internal/ helpers used by nulls_overlay + layout_preview_overlay
        ${CMAKE_CURRENT_SOURCE_DIR}/internal/helpers.cpp
    )

    target_include_directories(chronon3d_backend_software_diagnostics PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )

    target_link_libraries(chronon3d_backend_software_diagnostics
        PUBLIC chronon3d
        PRIVATE spdlog::spdlog_header_only glm::glm
    )

    # In-tree-only convenience alias (TICKET-011 cmake-boundary OBJECT lift):
    # same rationale as `chronon3d::content` in content/CMakeLists.txt —
    # `chronon3d_backend_software_diagnostics` is now an OBJECT lib with
    # no installed ARCHIVE; downstream `find_package(Chronon3D)` consumers
    # must use the raw target name.
    if(CMAKE_PROJECT_NAME STREQUAL "Chronon3d")
        add_library(chronon3d::backend_software_diagnostics ALIAS chronon3d_backend_software_diagnostics)
    endif()
endif()
