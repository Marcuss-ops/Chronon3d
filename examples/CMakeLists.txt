add_executable(code_first_smoke
    code_first_smoke.cpp
)
target_link_libraries(code_first_smoke PRIVATE chronon3d)

add_executable(remotion_style_guide
    remotion_style_guide.cpp
)
target_link_libraries(remotion_style_guide PRIVATE chronon3d)

add_executable(basic_shapes
    basic_shapes.cpp
)
target_link_libraries(basic_shapes PRIVATE chronon3d chronon3d_io)

add_executable(moving_rect
    moving_rect.cpp
)
target_link_libraries(moving_rect PRIVATE chronon3d chronon3d_io)

add_executable(axis_debug
    axis_debug.cpp
)
target_link_libraries(axis_debug PRIVATE chronon3d chronon3d_io)

add_executable(blending_showcase
    blending_showcase.cpp
)
target_link_libraries(blending_showcase PRIVATE chronon3d chronon3d_io)
