# ==============================================================================
# src/animations/CMakeLists.txt — chronon3d_animations
# ==============================================================================

add_library(chronon3d_animations STATIC
    background_presets.cpp
    animated_value.cpp
)
target_link_libraries(chronon3d_animations PUBLIC
    chronon3d
    chronon3d_scene
    chronon3d_backend_software
)
