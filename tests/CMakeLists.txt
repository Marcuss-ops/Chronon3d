find_package(doctest CONFIG REQUIRED)

# Shared test main
set(TEST_MAIN test_main.cpp)

function(chronon3d_enable_test_pch target_name)
    if(target_name STREQUAL "chronon3d_core_tests")
        target_precompile_headers(chronon3d_core_tests PRIVATE
            <algorithm>
            <chrono>
            <filesystem>
            <memory>
            <optional>
            <string>
            <vector>
            <chronon3d/chronon3d.hpp>
            <spdlog/spdlog.h>
            <fmt/format.h>
        )
    else()
        target_precompile_headers(${target_name} REUSE_FROM chronon3d_core_tests)
    endif()
endfunction()

# Core Tests (Math, Geometry, Animation, Timeline)
add_executable(chronon3d_core_tests
    ${TEST_MAIN}
    core/test_frame_context.cpp
    core/trace_test.cpp
    core/math/test_math.cpp
    core/math/test_output_transform.cpp
    simd/test_simd_kernels.cpp
    cache/test_lru_weight.cpp
    cache/test_framebuffer_pool.cpp
    core/math/test_camera_pose.cpp
    core/math/test_camera_2_5d_projection.cpp
    core/math/test_projector_2_5d.cpp
    core/geometry/test_geometry.cpp
    core/animation/test_animation.cpp
    core/animation/test_interpolate.cpp
    core/animation/test_spring.cpp
    core/animation/test_animated_value.cpp
    core/animation/test_animated_value_expression.cpp
    core/animation/test_keyframes.cpp
    core/animation/test_keyframe_tracks.cpp
    core/animation/test_animated_transform.cpp
    core/animation/test_cubic_bezier.cpp
    core/timeline/test_timeline.cpp
    core/timeline/test_code_first_composition.cpp
    core/timeline/test_sequence.cpp
    assets/test_svg_path_loader.cpp
    assets/test_render_preflight.cpp
    assets/test_asset_registry.cpp
    core/math/test_color_space.cpp
    core/math/test_expression.cpp
)
target_link_libraries(chronon3d_core_tests PRIVATE chronon3d_pipeline doctest::doctest)
target_include_directories(chronon3d_core_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_core_tests)
add_test(NAME chronon3d_core_tests COMMAND chronon3d_core_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Scene Tests
add_executable(chronon3d_scene_tests
    ${TEST_MAIN}
    scene/layout/test_scene_builder.cpp
    scene/shapes/test_path_utils.cpp
    scene/shapes/test_shape_model.cpp
    scene/layout/test_layer_hierarchy.cpp
    scene/camera/test_camera_hierarchy.cpp
    scene/rendering/test_anchor_gallery.cpp
    scene/layout/test_layer_builder_animated.cpp
    scene/rendering/test_render_node_factory.cpp
    scene/rendering/test_depth_role.cpp
    scene/layout/test_layout_solver.cpp
)
target_link_libraries(chronon3d_scene_tests PRIVATE chronon3d_pipeline chronon3d_scene chronon3d_backend_software doctest::doctest)
target_include_directories(chronon3d_scene_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_scene_tests)
add_test(NAME chronon3d_scene_tests COMMAND chronon3d_scene_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Renderer & Graph Tests
add_executable(chronon3d_renderer_tests
    ${TEST_MAIN}
    renderer/camera/test_camera_motion.cpp
    renderer/camera/test_camera_preview_reference.cpp
    renderer/camera/test_dof.cpp
    renderer/camera/test_motion_blur.cpp
    renderer/effects/test_effects.cpp
    renderer/effects/test_adjustment_layer.cpp
    renderer/effects/test_effect_stack.cpp
    renderer/effects/test_masks.cpp
    renderer/media/test_image_rendering.cpp
    renderer/media/test_video_card.cpp
    renderer/perf/test_render_perf.cpp
    renderer/lighting/test_light_context.cpp
    renderer/lighting/test_directional_lights.cpp
    renderer/lighting/test_shadows.cpp
    presets/test_motion_object.cpp
    presets/test_motion_resolver.cpp
    presets/test_motion_3d.cpp
    presets/test_motion_renderer.cpp
    presets/test_phrase_presets.cpp
    presets/test_motion_visual_smoke.cpp
    render_graph/features/test_unified_transform_path.cpp
    renderer/dispatch/test_software_node_dispatcher.cpp
    render_graph/features/test_precompositions.cpp
    render_graph/features/test_modular_features.cpp
    render_graph/features/test_track_matte.cpp
    render_graph/features/test_transition.cpp
    render_graph/features/test_camera_2_5d_graph.cpp
    registry/test_registries.cpp
    cache/test_cache_sharding.cpp
    effects/test_effect_registry.cpp
    deterministic/test_deterministic.cpp
    render_graph/pipeline/test_render_pipeline.cpp
    render_graph/pipeline/test_render_backend.cpp
    render_graph/cache/cache_key_tests.cpp
    render_graph/cache/cache_frame_invariant_tests.cpp
    render_graph/pipeline/test_dirty_rects.cpp
    cache/node_cache_tests.cpp
    render_graph/pipeline/graph_builder_tests.cpp
    render_graph/pipeline/graph_executor_tests.cpp
    scene/camera/camera_25d_tests.cpp
    effects/effect_graph_tests.cpp
    compositor/blend_mode_tests.cpp
    scene/shapes/mask_tests.cpp
    rendering/source_primitives_tests.cpp
    specscene/specscene_boundary_tests.cpp
    golden/golden_render_tests.cpp
    perf/render_graph_perf_tests.cpp
    runtime/test_telemetry.cpp
)
target_link_libraries(chronon3d_renderer_tests
    PRIVATE
        chronon3d_pipeline
        chronon3d_scene
        doctest::doctest
)
target_compile_definitions(chronon3d_renderer_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(CHRONON3D_ENABLE_VIDEO)
    target_link_libraries(chronon3d_renderer_tests PRIVATE chronon3d_backend_ffmpeg)
endif()
target_include_directories(chronon3d_renderer_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(chronon3d_renderer_tests PRIVATE ${Stb_INCLUDE_DIR})
chronon3d_enable_test_pch(chronon3d_renderer_tests)
add_test(NAME chronon3d_renderer_tests COMMAND chronon3d_renderer_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# IO Tests
add_executable(chronon3d_io_tests
    ${TEST_MAIN}
    io/test_image_writer.cpp
    io/test_png_validity.cpp
    io/test_exr_writer.cpp
)
target_link_libraries(chronon3d_io_tests PRIVATE chronon3d_pipeline chronon3d_backend_image doctest::doctest)
target_include_directories(chronon3d_io_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_io_tests)
add_test(NAME chronon3d_io_tests COMMAND chronon3d_io_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# CLI Tests
add_executable(chronon3d_cli_tests
    test_main.cpp
    cli/test_frame_range_parser.cpp
    cli/test_render_job_plan.cpp
    cli/test_frame_chunks.cpp
    cli/test_ffmpeg_pipe_encoder.cpp
    cli/bench_json_tests.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/common/cli_utils.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/job/render_job_plan.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/video/ffmpeg_pipe_command.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/video/ffmpeg_pipe_yuv.cpp
)

target_link_libraries(chronon3d_cli_tests
    PRIVATE
        chronon3d_pipeline
        CLI11::CLI11
        doctest::doctest
)
target_include_directories(chronon3d_cli_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_cli_tests)
add_test(NAME chronon3d_cli_tests COMMAND chronon3d_cli_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

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
