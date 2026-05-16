find_package(doctest CONFIG REQUIRED)

# Shared test main
set(TEST_MAIN test_main.cpp)

# Core Tests (Math, Geometry, Animation, Timeline)
add_executable(chronon3d_core_tests
    ${TEST_MAIN}
    core/test_core.cpp
    core/test_frame_context.cpp
    math/test_math.cpp
    math/test_camera_pose.cpp
    math/test_camera_2_5d_projection.cpp
    geometry/test_geometry.cpp
    animation/test_animation.cpp
    animation/test_interpolate.cpp
    animation/test_spring.cpp
    animation/test_animated_value.cpp
    animation/test_keyframes.cpp
    animation/test_keyframe_tracks.cpp
    timeline/test_timeline.cpp
    timeline/test_code_first_composition.cpp
    timeline/test_sequence.cpp
    test_anchor_math.cpp
)
target_link_libraries(chronon3d_core_tests PRIVATE chronon3d_pipeline doctest::doctest)
target_include_directories(chronon3d_core_tests PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME chronon3d_core_tests COMMAND chronon3d_core_tests)

# Scene Tests
add_executable(chronon3d_scene_tests
    ${TEST_MAIN}
    scene/test_scene.cpp
    scene/test_scene_builder.cpp
    scene/test_shape_model.cpp
    scene/test_layer_hierarchy.cpp
    scene/test_camera_hierarchy.cpp
    scene/test_anchor_gallery.cpp
)
target_link_libraries(chronon3d_scene_tests PRIVATE chronon3d_pipeline chronon3d_scene chronon3d_backend_software doctest::doctest)
target_include_directories(chronon3d_scene_tests PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME chronon3d_scene_tests COMMAND chronon3d_scene_tests)

# Renderer & Graph Tests
add_executable(chronon3d_renderer_tests
    ${TEST_MAIN}
    scene/test_line_primitive.cpp
    scene/test_circle_primitive.cpp
    renderer/basics/test_renderer.cpp
    renderer/basics/test_code_first_render.cpp
    renderer/basics/test_background_render.cpp
    renderer/basics/test_background_grid.cpp
    renderer/basics/test_alpha_blending.cpp
    renderer/basics/test_draw_order.cpp
    renderer/basics/test_coordinate_system.cpp
    renderer/basics/test_clipping.cpp
    renderer/basics/test_layer_rendering.cpp
    renderer/camera/test_moving_frame.cpp
    renderer/camera/test_camera_2_5d.cpp
    renderer/camera/test_camera_smoke.cpp
    renderer/camera/test_camera_motion.cpp
    renderer/camera/test_camera_preview_reference.cpp
    renderer/camera/test_camera_tilt_text_preview.cpp
    renderer/camera/test_dof.cpp
    renderer/camera/test_motion_blur.cpp
    renderer/effects/test_effects.cpp
    renderer/effects/test_adjustment_layer.cpp
    renderer/effects/test_effect_stack.cpp
    renderer/effects/test_masks.cpp
    renderer/media/test_image_rendering.cpp
    renderer/cache/test_cache.cpp
    renderer/perf/test_render_perf.cpp
    renderer/text/test_text_rendering.cpp
    renderer/text/test_text_layout.cpp
    renderer/text/test_text_alignment_regression.cpp
    renderer/text/test_text_3d_tilt.cpp
    render_graph/test_unified_transform_path.cpp
    renderer/dispatch/test_software_node_dispatcher.cpp
    render_graph/test_graph_executor.cpp
    render_graph/test_precompositions.cpp
    render_graph/test_modular_features.cpp
    render_graph/test_camera_2_5d_graph.cpp
    registry/test_registries.cpp
    cache/test_cache_foundation.cpp
    effects/test_effect_registry.cpp
    deterministic/test_deterministic.cpp
)
target_link_libraries(chronon3d_renderer_tests
    PRIVATE
        chronon3d_pipeline
        chronon3d_backend_image
        chronon3d_compositions
        chronon3d_graph
        chronon3d_backend_software
        chronon3d_scene
        doctest::doctest
)
if(CHRONON3D_ENABLE_VIDEO)
    target_link_libraries(chronon3d_renderer_tests PRIVATE chronon3d_backend_ffmpeg)
endif()
target_include_directories(chronon3d_renderer_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(chronon3d_renderer_tests PRIVATE ${Stb_INCLUDE_DIR})
add_test(NAME chronon3d_renderer_tests COMMAND chronon3d_renderer_tests)

# IO Tests
add_executable(chronon3d_io_tests
    ${TEST_MAIN}
    io/test_image_writer.cpp
    io/test_png_validity.cpp
)
target_link_libraries(chronon3d_io_tests PRIVATE chronon3d_pipeline chronon3d_backend_image doctest::doctest)
target_include_directories(chronon3d_io_tests PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME chronon3d_io_tests COMMAND chronon3d_io_tests)

# CLI Tests
add_executable(chronon3d_cli_tests
    test_main.cpp
    cli/test_frame_range_parser.cpp
    cli/test_render_job_plan.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/cli_utils.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/render_job_plan.cpp
)

target_link_libraries(chronon3d_cli_tests
    PRIVATE
        chronon3d_pipeline
        doctest::doctest
)
target_include_directories(chronon3d_cli_tests PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME chronon3d_cli_tests COMMAND chronon3d_cli_tests)

# Video Tests (Isolated)
if(CHRONON3D_ENABLE_VIDEO)
    add_executable(chronon3d_video_tests 
        test_main.cpp
        video/test_camera_motion_video.cpp
    )
    target_link_libraries(chronon3d_video_tests 
        PRIVATE 
            chronon3d_pipeline 
            chronon3d_backend_ffmpeg 
            doctest::doctest
    )
    target_compile_definitions(chronon3d_video_tests PRIVATE CHRONON_BUILD_TESTS CHRONON_WITH_VIDEO)
    target_include_directories(chronon3d_video_tests PRIVATE ${CMAKE_SOURCE_DIR})
    add_test(NAME chronon3d_video_tests COMMAND chronon3d_video_tests)
endif()
