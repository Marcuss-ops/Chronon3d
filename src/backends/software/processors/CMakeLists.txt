# ==============================================================================
# src/backends/software/processors/CMakeLists.txt
# ==============================================================================

target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/builtin_processors.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/path_processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/software_effect_processors.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/software_grid_background_processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/software_mesh_processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/software_line_processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/software_shape_processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/software_utility_processors.cpp
)

# Image sub-processors
target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/image/software_image_processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/image/software_tiled_image_processor.cpp
)

# Text sub-processors
target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/text/software_text_effects.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/software_text_processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/text_glow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/text_shadow.cpp
)
