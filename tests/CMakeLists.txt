find_package(doctest CONFIG REQUIRED)

add_executable(chronon3d_tests
    test_main.cpp
    
    # Core
    core/test_core.cpp
    core/test_frame_context.cpp
    core/test_composition_registration.cpp
    
    # Math & Geometry
    math/test_math.cpp
    math/test_camera_2_5d_projection.cpp
    geometry/test_geometry.cpp
    
    # Animation
    animation/test_animation.cpp
    animation/test_interpolate.cpp
    animation/test_spring.cpp
    animation/test_animated_value.cpp
    animation/test_keyframes.cpp
    animation/test_keyframe_tracks.cpp
    
    # Timeline
    timeline/test_timeline.cpp
    timeline/test_code_first_composition.cpp
    timeline/test_sequence.cpp
    
    # Scene
    scene/test_scene.cpp
    scene/test_scene_builder.cpp
    scene/test_shape_model.cpp
    scene/test_line_primitive.cpp
    scene/test_circle_primitive.cpp
    scene/test_layer_hierarchy.cpp
    
    # Rendering
    renderer/basics/test_renderer.cpp
    renderer/basics/test_code_first_render.cpp
    renderer/basics/test_background_grid.cpp
    renderer/basics/test_alpha_blending.cpp
    renderer/basics/test_draw_order.cpp
    renderer/basics/test_coordinate_system.cpp
    renderer/basics/test_clipping.cpp
    renderer/basics/test_layer_rendering.cpp
    renderer/camera/test_moving_frame.cpp
    renderer/camera/test_camera_2_5d.cpp
    renderer/camera/test_dof.cpp
    renderer/camera/test_motion_blur.cpp
    renderer/effects/test_effects.cpp
    renderer/effects/test_adjustment_layer.cpp
    renderer/effects/test_effect_stack.cpp
    renderer/effects/test_effects_stack.cpp
    renderer/effects/test_masks.cpp
    renderer/media/test_image_rendering.cpp
    renderer/media/test_video_frame_provider.cpp
    renderer/cache/test_cache.cpp
    renderer/perf/test_render_perf.cpp
    renderer/text/test_text_rendering.cpp
    renderer/text/test_text_layout.cpp
    render_graph/test_graph_executor.cpp
    render_graph/test_precompositions.cpp
    render_graph/test_modular_features.cpp
    render_graph/test_camera_2_5d_graph.cpp

    # Registries & cache
    registry/test_registries.cpp
    cache/test_cache_foundation.cpp

    # Effects
    effects/test_effect_registry.cpp

    # Other
    deterministic/test_deterministic.cpp
    
    # IO
    io/test_image_writer.cpp
    io/test_png_validity.cpp
)

target_link_libraries(chronon3d_tests
    PRIVATE
        chronon3d_pipeline
        chronon3d_io
        doctest::doctest
        $<TARGET_OBJECTS:chronon3d_graph>
        $<TARGET_OBJECTS:chronon3d_software>
        $<TARGET_OBJECTS:chronon3d_scene>
)
target_include_directories(chronon3d_tests PRIVATE ${Stb_INCLUDE_DIR})

# No longer need WHOLEARCHIVE since we use OBJECT libraries

add_executable(debug_parenting ../scratch/debug_parenting.cpp)
target_link_libraries(debug_parenting PRIVATE chronon3d_scene)
target_include_directories(debug_parenting PRIVATE ${Stb_INCLUDE_DIR})

add_test(NAME chronon3d_tests COMMAND chronon3d_tests)

