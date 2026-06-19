# ==============================================================================
# src/backends/software/processors/CMakeLists.txt
# ==============================================================================

# Top-level processor sources
# Image and text sub-processors are added via add_subdirectory(image) and
# add_subdirectory(text) below.
target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/builtin_processors.cpp
)
if(CHRONON3D_USE_BLEND2D)
    target_sources(chronon3d_backend_software PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/path_processor.cpp
    )
endif()
target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/software_effect_processors.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/software_grid_background_processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/software_mesh_processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/software_line_processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/software_shape_processor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/software_utility_processors.cpp
)

add_subdirectory(image)
add_subdirectory(text)
add_subdirectory(text_run)
add_subdirectory(effects)
