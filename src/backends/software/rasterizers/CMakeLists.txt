# ==============================================================================
# src/backends/software/rasterizers/CMakeLists.txt
# ==============================================================================

target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/line_rasterizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scanline_rasterizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shape_rasterizer.cpp
)

# Path sub-rasterizers
target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/path/path_cache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/path/path_geometry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/path/path_rasterizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/path/path_stroke.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/path/pip.cpp
)

# Card sub-rasterizers
target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/card/projected_card_rasterizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/card/card3d_material_rasterizer.cpp
)
