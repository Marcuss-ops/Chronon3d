find_package(doctest CONFIG REQUIRED)

add_executable(chronon3d_tests
    test_main.cpp
    
    # Core
    core/test_core.cpp
    core/test_frame_context.cpp
    
    # Math & Geometry
    math/test_math.cpp
    geometry/test_geometry.cpp
    
    # Animation
    animation/test_animation.cpp
    animation/test_interpolate.cpp
    
    # Timeline
    timeline/test_timeline.cpp
    timeline/test_code_first_composition.cpp
    
    # Scene
    scene/test_scene.cpp
    scene/test_scene_builder.cpp
    scene/test_line_primitive.cpp
    scene/test_circle_primitive.cpp
    
    # Rendering
    renderer/test_renderer.cpp
    renderer/test_code_first_render.cpp
    renderer/test_alpha_blending.cpp
    
    # Other
    deterministic/test_deterministic.cpp
    
    # IO
    io/test_image_writer.cpp
)

target_link_libraries(chronon3d_tests
    PRIVATE
        chronon3d
        chronon3d_io
        doctest::doctest
)

add_test(NAME chronon3d_tests COMMAND chronon3d_tests)
