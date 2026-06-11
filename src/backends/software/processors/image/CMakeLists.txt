# ==============================================================================
# src/backends/software/processors/image/CMakeLists.txt
# ==============================================================================

target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/software_image_processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/software_tiled_image_processor.cpp
)
