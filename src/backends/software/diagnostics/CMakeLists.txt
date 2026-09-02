# ==============================================================================
# src/backends/software/diagnostics/CMakeLists.txt
#
# Diagnostics overlay files live in `chronon3d_backend_software_diagnostics`,
# a dedicated OBJECT target linked into `chronon3d_backend_software` only when
# the engine-level CHRONON3D_ENABLE_DIAGNOSTICS option is enabled.
# ==============================================================================

if(CHRONON3D_ENABLE_DIAGNOSTICS)
    add_library(chronon3d_backend_software_diagnostics OBJECT
        ${CMAKE_CURRENT_SOURCE_DIR}/bbox_overlay.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/layout_preview_overlay.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nulls_overlay.cpp
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

    if(CMAKE_PROJECT_NAME STREQUAL "Chronon3d")
        add_library(chronon3d::backend_software_diagnostics ALIAS chronon3d_backend_software_diagnostics)
    endif()
endif()
