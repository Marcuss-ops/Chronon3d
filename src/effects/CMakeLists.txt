# ==============================================================================
# src/effects/CMakeLists.txt — chronon3d_effects
# ==============================================================================

add_library(chronon3d_effects STATIC
    effect_registry.cpp
    effect_catalog.cpp
    curves.cpp
    color_pipeline.cpp
)
target_link_libraries(chronon3d_effects PUBLIC chronon3d ${CHRONON3D_MESHOPTIMIZER_LINK})
