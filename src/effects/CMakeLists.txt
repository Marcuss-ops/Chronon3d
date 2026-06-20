# ==============================================================================
# src/effects/CMakeLists.txt — chronon3d_effects
# ==============================================================================

add_library(chronon3d_effects STATIC
    effect_catalog.cpp
    effect_catalog_data.cpp
    curves.cpp
    color_pipeline.cpp
)
target_link_libraries(chronon3d_effects PUBLIC chronon3d chronon3d_graph_nodes ${CHRONON3D_MESHOPTIMIZER_LINK})
