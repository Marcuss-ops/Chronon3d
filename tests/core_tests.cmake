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
        text/test_text_quality_glyph.cpp
        text/test_text_quality_shaping.cpp
        text/test_text_quality_tracking.cpp
        text/test_text_quality_arabic.cpp
        text/test_text_bidi.cpp
        text/test_text_unit_map.cpp
        text/test_selector_shapes.cpp
        text/test_selector_evaluate.cpp
        text/test_selector_combine.cpp
    )
endif()

add_executable(chronon3d_core_tests
    ${TEST_MAIN}
    core/test_frame_context.cpp
    core/memory/test_huge_page_allocator.cpp
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
    assets/test_asset_resolver.cpp
    core/math/test_color_space.cpp
    core/math/test_nan_guard.cpp
    core/test_system_metrics_parse.cpp
    # WP-6 PR 6.0 — ExecutionScope (root/tile/precomp) construction
    # invariants + parent chain + recursion guard + child-arena
    # independence.  Header-only type under test: no .cpp pairing.
    core/test_execution_scope.cpp
    core/math/test_expression.cpp
    core/math/test_expression_extended.cpp
    test_text_preset_registry.cpp
    ${CORE_BLEND2D_TESTS}
    media/test_media_placement.cpp
    core/test_result.cpp
    core/test_scene_hasher_camera.cpp
    render_graph/executor/test_cache_key_contract.cpp
    render_graph/executor/test_framebuffer_lifetime.cpp
    render_graph/builder/test_graph_build_pass_order.cpp
    # WP-3 PR 3.0 + PR 3.3 — render-session noexcept-throw guards and
    # multi-session isolation tests live in tests/runtime/ alongside the
    # other session/services tests.
    runtime/test_render_session_reset_and_isolation.cpp
)
target_link_libraries(chronon3d_core_tests PRIVATE chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline doctest::doctest)
# TICKET-006: CORE_BLEND2D_TESTS exercise symbols that only resolve when
# chronon3d_backend_text is linked (bidi_segmenter, font_engine, glyph_atlas,
# shared_font_engine, rasterize_text_to_bl_image, etc.). Without this guard
# in non-Blend2D / non-text presets (e.g. linux-core-dev), the build reports:
#   'undefined symbol: chronon3d::shared_font_engine()'
#   'undefined symbol: chronon3d::glyph_atlas_lookup(...)'
#   'undefined symbol: chronon3d::segment_bidi_runs(...)'
# The duplication with tests/scene_tests.cmake is intentional: core-tests
# pull in text_layout / text_bounds / text_quality_*, scene-tests pull in
# layer_design_kit / layer_builder / text_run_builder — both end up calling
# into chronon3d_backend_text. Linking indiscrimate would be a regression.
if(CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D AND TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_core_tests PRIVATE chronon3d_backend_text)
endif()

# Make CMAKE_CURRENT_BINARY_DIR available for ExtensionLoader plugin tests (finds .so files)
target_compile_definitions(chronon3d_core_tests PRIVATE
    CMAKE_CURRENT_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
target_include_directories(chronon3d_core_tests PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests)
chronon3d_enable_test_pch(chronon3d_core_tests)
add_test(NAME chronon3d_core_tests COMMAND chronon3d_core_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
