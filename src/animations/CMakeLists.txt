# ==============================================================================
# src/animations/CMakeLists.txt — chronon3d_animations
# ==============================================================================

add_library(chronon3d_animations STATIC
    animation_catalog.cpp
    background_presets.cpp
    animation_curve.cpp
    animated_value.cpp
)
target_link_libraries(chronon3d_animations PUBLIC
    chronon3d
    chronon3d_scene
    chronon3d_backend_software
    nlohmann_json::nlohmann_json
)
