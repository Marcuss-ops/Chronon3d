# ── Core Tests (Math, Geometry, Animation, Timeline, Cache, SIMD, Assets, Text, Media, Extension, Architecture) ──

# Blend2D/Text-dependent test sources (only compiled when both are available)
set(CORE_BLEND2D_TESTS "")
if(CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT)
    list(APPEND CORE_BLEND2D_TESTS
        text/test_text_layout.cpp
        text/test_text_bounds.cpp
        text/test_text_cache_key.cpp
        text/test_text_animator.cpp
        text/test_text_material.cpp
        text/test_text_style_presets.cpp
        text/test_font_engine.cpp
        text/test_text_quality_suite.cpp
        text/test_text_bidi.cpp
        text/glyph_selector_tests.cpp
        text/text_animator_property_tests.cpp
    )
endif()

add_executable(chronon3d_core_tests
    ${TEST_MAIN}
    core/test_frame_context.cpp
    core/math/test_math.cpp
    core/math/test_output_transform.cpp
    simd/test_simd_kernels.cpp
    cache/test_lru_weight.cpp
    cache/test_lru_cache.cpp
    cache/test_framebuffer_pool.cpp
    cache/test_video_frame_cache.cpp
    cache/test_persistent_framebuffer_store.cpp
    core/test_sharded_telemetry_store.cpp
    core/test_render_counters.cpp
    core/test_cache_eval_dirty_counters.cpp
    core/math/test_camera_pose.cpp
    core/math/test_camera_2_5d_projection.cpp
    core/math/test_projector_2_5d.cpp
    core/math/test_2_5d_roadmap.cpp
    core/math/test_camera_projection_resolver.cpp
    core/math/test_camera_projection_geometry_safety.cpp
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
    core/animation/test_stagger.cpp
    core/animation/test_animation_curve.cpp
    core/animation/test_animation_track.cpp
    core/animation/test_wiggle.cpp
    core/animation/test_sample_time.cpp
    core/animation/test_temporal_spatial_curve.cpp
    core/animation/path/test_catmull_rom_path.cpp
    core/animation/path/test_spatial_bezier_path.cpp
    core/animation/test_quaternion_track.cpp
    core/timeline/test_timeline.cpp
    core/timeline/test_timeline_builder.cpp
    core/timeline/test_code_first_composition.cpp
    core/timeline/test_sequence.cpp
    assets/test_svg_path_loader.cpp
    assets/test_render_preflight.cpp
    assets/test_asset_registry.cpp
    core/math/test_color_space.cpp
    core/math/test_nan_guard.cpp
    core/test_system_metrics_parse.cpp
    core/math/test_expression.cpp
    core/math/test_expression_extended.cpp
    ${CORE_BLEND2D_TESTS}
    media/test_media_placement.cpp
    core/test_result.cpp
    core/test_scene_hasher_camera.cpp
    render_graph/executor/test_cache_key_contract.cpp
    render_graph/executor/test_framebuffer_lifetime.cpp
    render_graph/builder/test_graph_build_pass_order.cpp
    text/test_text_document.cpp
    text/test_single_line_composer.cpp
    text/test_every_line_composer.cpp
    text/test_path_sampler.cpp
    text/test_text_path_composer.cpp
    text/test_animated_text_document.cpp
    text/test_text_resolver.cpp
    text/test_text_run_builder.cpp
    text/test_text_run_driver.cpp
)
target_link_libraries(chronon3d_core_tests PRIVATE chronon3d_pipeline doctest::doctest)

# Make CMAKE_CURRENT_BINARY_DIR available for ExtensionLoader plugin tests (finds .so files)
target_compile_definitions(chronon3d_core_tests PRIVATE
    CMAKE_CURRENT_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
target_include_directories(chronon3d_core_tests PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests)
chronon3d_enable_test_pch(chronon3d_core_tests)
add_test(NAME chronon3d_core_tests COMMAND chronon3d_core_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
