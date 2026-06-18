# ==============================================================================
# src/animations/CMakeLists.txt — chronon3d_animations
#
# Pure animation math (curves, easing, expressions).  No backend or scene
# dependency — background presets live in src/scene/utils/.
# ==============================================================================

add_library(chronon3d_animations STATIC
    animated_value.cpp
)
target_link_libraries(chronon3d_animations PUBLIC
    chronon3d
)
