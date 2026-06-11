# ==============================================================================
# src/backends/software/specialized/CMakeLists.txt
# ==============================================================================

target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fake_box3d_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/grid_plane_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mesh_renderer.cpp
)
