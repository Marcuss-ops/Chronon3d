find_package(doctest CONFIG REQUIRED)

# Shared test main
set(TEST_MAIN test_main.cpp)

function(chronon3d_enable_test_pch target_name)
    # PCH disabled to prevent compiler crashes on resource-constrained environments
endfunction()

# Core Tests (Math, Geometry, Animation, Timeline)
add_executable(chronon3d_core_tests
    ${TEST_MAIN}
    core/test_frame_context.cpp
    core/math/test_math.cpp
    core/math/test_output_transform.cpp
    simd/test_simd_kernels.cpp
    cache/test_lru_weight.cpp
    cache/test_framebuffer_pool.cpp
    cache/test_video_frame_cache.cpp
    cache/test_persistent_bake_cache.cpp
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
    text/test_text_layout.cpp
    text/test_text_cache_key.cpp
    media/test_media_placement.cpp
)
target_link_libraries(chronon3d_core_tests PRIVATE chronon3d_pipeline doctest::doctest)
target_include_directories(chronon3d_core_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_core_tests)
add_test(NAME chronon3d_core_tests COMMAND chronon3d_core_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Animation Catalog Tests
add_executable(chronon3d_animation_tests
    ${TEST_MAIN}
    animations/test_animation_catalog.cpp
    animations/test_background_catalog.cpp
)
target_link_libraries(chronon3d_animation_tests PRIVATE chronon3d_pipeline doctest::doctest)
# WHOLE_ARCHIVE removed: content targets are now small and explicitly registered
if(CHRONON3D_BUILD_CONTENT)
    if(TARGET chronon3d_content_anims)
        target_link_libraries(chronon3d_animation_tests PRIVATE chronon3d_content_anims)
    endif()
endif()
target_include_directories(chronon3d_animation_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_animation_tests)
add_test(NAME chronon3d_animation_tests COMMAND chronon3d_animation_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Scene Tests
add_executable(chronon3d_scene_tests
    ${TEST_MAIN}
    scene/layout/test_scene_builder.cpp
    scene/shapes/test_path_utils.cpp
    scene/shapes/test_shape_model.cpp
    scene/layout/test_layer_hierarchy.cpp
    scene/camera/test_camera_hierarchy.cpp
    scene/layout/test_layer_builder_animated.cpp
    scene/rendering/test_render_node_factory.cpp
    scene/rendering/test_depth_role.cpp
    scene/layout/test_layout_solver.cpp
    presets/test_presets.cpp
)
target_link_libraries(chronon3d_scene_tests PRIVATE chronon3d_pipeline chronon3d_scene chronon3d_backend_software doctest::doctest)
target_include_directories(chronon3d_scene_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_scene_tests)
add_test(NAME chronon3d_scene_tests COMMAND chronon3d_scene_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Renderer & Graph Tests
add_executable(chronon3d_renderer_tests
    ${TEST_MAIN}
    renderer/camera/test_camera_motion.cpp
    renderer/camera/test_dof.cpp
    renderer/effects/test_adjustment_layer.cpp
    renderer/effects/test_advanced_effects.cpp
    renderer/effects/test_effect_stack.cpp
    renderer/effects/test_invariants.cpp
    renderer/media/test_video_card.cpp
    renderer/perf/test_render_perf.cpp
    renderer/lighting/test_light_context.cpp
    renderer/lighting/test_directional_lights.cpp
    renderer/lighting/test_shadows.cpp
    render_graph/features/test_unified_transform_path.cpp
    renderer/dispatch/test_software_node_dispatcher.cpp
    render_graph/features/test_transition.cpp
    registry/test_registries.cpp
    cache/test_cache_sharding.cpp
    cache/test_tile_cache.cpp
    effects/test_effect_registry.cpp
    deterministic/test_deterministic.cpp
    render_graph/pipeline/test_render_pipeline.cpp
    render_graph/pipeline/test_line_grid.cpp
    render_graph/pipeline/test_pipeline_robustness.cpp
    render_graph/pipeline/test_graph_health.cpp
    render_graph/pipeline/test_grid_math.cpp
    render_graph/pipeline/test_render_backend.cpp
    render_graph/pipeline/test_dirty_rects.cpp
    render_graph/optimizer/test_graph_optimizer.cpp
    render_graph/pipeline/test_dirty_rects_v2.cpp
    render_graph/pipeline/test_tile_grid.cpp
    render_graph/pipeline/test_dirty_tiles_output.cpp
    render_graph/pipeline/test_tile_parallel.cpp
    render_graph/pipeline/test_graph_preflight_diagnostics.cpp
    scene/camera/camera_25d_tests.cpp
    effects/effect_graph_tests.cpp
    scene/shapes/mask_tests.cpp
    specscene/specscene_boundary_tests.cpp
    golden/golden_render_tests.cpp
    golden/glow_golden_tests.cpp
    golden/suite_chronon3d_tests.cpp
    runtime/test_telemetry.cpp
    runtime/test_telemetry_semantic.cpp
    runtime/test_telemetry_report.cpp
)
target_link_libraries(chronon3d_renderer_tests
    PRIVATE
        chronon3d_pipeline
        chronon3d_scene
        doctest::doctest
)
# WHOLE_ARCHIVE removed: content targets are now small and explicitly registered
if(CHRONON3D_BUILD_CONTENT)
    if(TARGET chronon3d_content_text)
        target_link_libraries(chronon3d_renderer_tests PRIVATE chronon3d_content_text)
    endif()
    if(TARGET chronon3d_content_shapes)
        target_link_libraries(chronon3d_renderer_tests PRIVATE chronon3d_content_shapes)
    endif()
    if(TARGET chronon3d_content_images)
        target_link_libraries(chronon3d_renderer_tests PRIVATE chronon3d_content_images)
    endif()
    if(TARGET chronon3d_content_anims)
        target_link_libraries(chronon3d_renderer_tests PRIVATE chronon3d_content_anims)
    endif()
    if(TARGET chronon3d_content_effects)
        target_link_libraries(chronon3d_renderer_tests PRIVATE chronon3d_content_effects)
    endif()
    if(TARGET chronon3d_content_2d5)
        target_link_libraries(chronon3d_renderer_tests PRIVATE chronon3d_content_2d5)
    endif()
    if(TARGET chronon3d_content_grid)
        target_link_libraries(chronon3d_renderer_tests PRIVATE chronon3d_content_grid)
    endif()
endif()
target_compile_definitions(chronon3d_renderer_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
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
    cli/test_frame_chunks.cpp
    cli/test_ffmpeg_pipe_encoder.cpp
    cli/test_video_pipeline_producer_consumer.cpp
    cli/bench_json_tests.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/common/cli_utils.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/video/ffmpeg_pipe_write_frame.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder_uring.cpp
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

# ── Optimizer Tests
add_executable(chronon3d_optimizer_tests
    ${TEST_MAIN}
    render_graph/optimizer/test_graph_optimizer.cpp
)
target_link_libraries(chronon3d_optimizer_tests PRIVATE chronon3d_pipeline doctest::doctest)
target_include_directories(chronon3d_optimizer_tests PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME chronon3d_optimizer_tests COMMAND chronon3d_optimizer_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# ── Cache Tests
add_executable(chronon3d_cache_tests
    ${TEST_MAIN}
    cache/test_persistent_bake_cache.cpp
)
target_link_libraries(chronon3d_cache_tests PRIVATE chronon3d_pipeline doctest::doctest)
target_include_directories(chronon3d_cache_tests PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME chronon3d_cache_tests COMMAND chronon3d_cache_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Aggregate targets: build test executables grouped by cost/speed
# Note: benchmarks are conditional; included only when TARGET chronon3d_benchmarks exists
if(TARGET chronon3d_benchmarks)
    set(CHRONON3D_BENCHMARK_DEP chronon3d_benchmarks)
else()
    set(CHRONON3D_BENCHMARK_DEP "")
endif()

# Fast tests: core + scene + CLI (no rendering, no video, no content)
add_custom_target(chronon3d_tests_fast
    DEPENDS
        chronon3d_core_tests
        chronon3d_scene_tests
        chronon3d_cli_tests
        chronon3d_optimizer_tests
        chronon3d_cache_tests
)

# Render tests: scene + renderer + golden (heavy, needs software backend + content)
add_custom_target(chronon3d_tests_render
    DEPENDS
        chronon3d_renderer_tests
        chronon3d_io_tests
        chronon3d_animation_tests
)

# Video tests: video pipeline tests (isolated, no content needed)
add_custom_target(chronon3d_tests_video
    DEPENDS
        chronon3d_video_tests
)

# All tests: everything
add_custom_target(chronon3d_tests
    DEPENDS
        chronon3d_tests_fast
        chronon3d_tests_render
        chronon3d_tests_video
        ${CHRONON3D_BENCHMARK_DEP}
)

# Video Tests (Isolated)
add_executable(chronon3d_video_tests
    ${TEST_MAIN}
    video/test_frame_converter.cpp
    video/test_video_diff.cpp
    video/test_long_export.cpp
    video/test_near_static_frames.cpp
)
target_link_libraries(chronon3d_video_tests
    PRIVATE
        chronon3d_pipeline
        doctest::doctest
)
target_include_directories(chronon3d_video_tests PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME chronon3d_video_tests COMMAND chronon3d_video_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Benchmarks (only built when explicitly enabled and the package is available)
if(CHRONON3D_BUILD_BENCHMARKS AND TARGET benchmark::benchmark_main)
    add_executable(chronon3d_benchmarks
        bench/micro_benchmarks.cpp
    )
    target_link_libraries(chronon3d_benchmarks
        PRIVATE
            chronon3d_pipeline
            benchmark::benchmark_main
    )
    target_include_directories(chronon3d_benchmarks PRIVATE ${CMAKE_SOURCE_DIR})
endif()

add_dependencies(chronon3d_tests chronon3d_video_tests)
