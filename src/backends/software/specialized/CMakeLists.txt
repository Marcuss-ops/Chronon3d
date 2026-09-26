# ==============================================================================
# src/backends/software/specialized/CMakeLists.txt
# ==============================================================================

target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fake_box3d_renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/grid_plane_renderer.cpp
)
# The triangle rasterizer consumes only the in-tree Mesh POD and is required
# for the shared inline RenderPlan primitive even in the lean dependency set.
target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mesh_renderer.cpp
)
