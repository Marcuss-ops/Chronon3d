# ==============================================================================
# src/backends/software/diagnostics/CMakeLists.txt
# ==============================================================================

target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/bbox_overlay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/layout_preview_overlay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/nulls_overlay.cpp
)

# Internal helpers
target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/internal/helpers.cpp
)
