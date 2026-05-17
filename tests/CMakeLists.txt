find_package(doctest CONFIG REQUIRED)

# Shared test main
set(TEST_MAIN test_main.cpp)

# Core Tests (Math, Geometry, Animation, Timeline)
add_executable(chronon3d_core_tests
    ${TEST_MAIN}
    core/test_core.cpp
    core/test_frame_context.cpp
    core/math/test_math.cpp
    core/math/test_camera_pose.cpp
    core/math/test_camera_2_5d_projection.cpp
    core/math/test_projector_2_5d.cpp
    core/geometry/test_geometry.cpp
    core/animation/test_animation.cpp
    core/animation/test_interpolate.cpp
    core/animation/test_spring.cpp
    core/animation/test_animated_value.cpp
    core/animation/test_keyframes.cpp
    core/animation/test_keyframe_tracks.cpp
    core/timeline/test_timeline.cpp
    core/timeline/test_code_first_composition.cpp
    core/timeline/test_sequence.cpp

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
    renderer/basics/test_gradient_fill.cpp
    renderer/basics/test_trim_path.cpp
    renderer/camera/test_moving_frame.cpp
    renderer/camera/test_camera_2_5d.cpp

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
    renderer/media/test_video_card.cpp
    renderer/cache/test_cache.cpp
    renderer/perf/test_render_perf.cpp
    renderer/lighting/test_light_context.cpp
    renderer/lighting/test_directional_lights.cpp
    renderer/lighting/test_shadows.cpp
    presets/test_text_box.cpp
    renderer/text/test_text_rendering.cpp
    renderer/text/test_text_layout.cpp
    renderer/text/test_text_layout_result.cpp
    renderer/text/test_text_alignment_regression.cpp

    render_graph/test_unified_transform_path.cpp
    renderer/dispatch/test_software_node_dispatcher.cpp
    render_graph/test_graph_executor.cpp
    render_graph/test_precompositions.cpp
    render_graph/test_modular_features.cpp
    render_graph/test_track_matte.cpp
    render_graph/test_camera_2_5d_graph.cpp
    renderer/camera/25d/test_25d_parallax.cpp
    renderer/camera/25d/test_25d_tilt.cpp
    renderer/camera/25d/test_25d_camera_motion.cpp
    renderer/camera/25d/test_25d_layer_rotation.cpp
    renderer/camera/25d/test_25d_dof.cpp
    renderer/camera/25d/test_25d_parenting.cpp
    registry/test_registries.cpp
    cache/test_cache_foundation.cpp
    effects/test_effect_registry.cpp
    deterministic/test_deterministic.cpp
    render_graph/test_render_pipeline.cpp
    render_graph/test_render_backend.cpp
    render_graph/cache_key_tests.cpp
    cache/node_cache_tests.cpp
    render_graph/graph_builder_tests.cpp
    render_graph/graph_executor_tests.cpp
    scene/scene_builder_tests.cpp
    scene/camera_25d_tests.cpp
    effects/effect_graph_tests.cpp
    compositor/blend_mode_tests.cpp
    scene/mask_tests.cpp
    rendering/source_primitives_tests.cpp
    specscene/specscene_boundary_tests.cpp
    registry/registry_tests.cpp
    cli/cli_render_tests.cpp
    golden/golden_render_tests.cpp
    perf/render_graph_perf_tests.cpp
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

# Aggregate target: build all test executables with a single cmake --build ... --target chronon3d_tests
add_custom_target(chronon3d_tests
    DEPENDS
        chronon3d_core_tests
        chronon3d_scene_tests
        chronon3d_renderer_tests
        chronon3d_io_tests
        chronon3d_cli_tests
)

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
    add_dependencies(chronon3d_tests chronon3d_video_tests)
endif()
