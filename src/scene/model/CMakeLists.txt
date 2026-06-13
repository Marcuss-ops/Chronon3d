# ==============================================================================
# src/scene/model/CMakeLists.txt
# ==============================================================================

target_sources(chronon3d_scene PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/layer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mask_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/render_node_factory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/transform_resolver.cpp
)
