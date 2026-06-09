# ── Core Tests (Math, Geometry, Animation, Timeline, Cache, SIMD, Assets, Text, Media, Extension, Architecture) ──

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
    cache/test_persistent_bake_cache.cpp
    core/test_sharded_telemetry_store.cpp
    core/math/test_camera_pose.cpp
    core/math/test_camera_2_5d_projection.cpp
    core/math/test_projector_2_5d.cpp
    core/math/test_2_5d_roadmap.cpp
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
    core/animation/test_wiggle.cpp
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
    text/test_text_layout.cpp
    text/test_text_cache_key.cpp
    text/test_text_animator.cpp
    text/test_text_material.cpp
    text/test_text_style_presets.cpp
    text/test_font_engine.cpp
    media/test_media_placement.cpp
    scene_presets/test_scene_presets.cpp
    extension/test_extension_loader.cpp
    extension/test_extension_registry.cpp
    extension/test_graph_node_registry.cpp
    architecture/test_protected_core_contracts.cpp
    core/test_scene_hasher_camera.cpp
    render_graph/executor/test_cache_key_contract.cpp
    render_graph/executor/test_framebuffer_lifetime.cpp
    render_graph/builder/test_graph_build_pass_order.cpp
)
target_link_libraries(chronon3d_core_tests PRIVATE chronon3d_pipeline chronon3d_extension doctest::doctest)

# Build a real mini shared library for ExtensionLoader real plugin tests
add_library(chronon3d_test_plugin MODULE
    extension/test_plugin.cpp
)
target_include_directories(chronon3d_test_plugin PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/deps/include)
target_link_libraries(chronon3d_test_plugin PRIVATE chronon3d_extension)
# Disable PCH for the test plugin to avoid PCH path issues
set_target_properties(chronon3d_test_plugin PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_target_properties(chronon3d_test_plugin PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins"
        BUILD_RPATH ""
    )
endif()
# Make CMAKE_CURRENT_BINARY_DIR available to the test executable for finding the .so
target_compile_definitions(chronon3d_core_tests PRIVATE
    CMAKE_CURRENT_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
add_dependencies(chronon3d_core_tests chronon3d_test_plugin)
target_include_directories(chronon3d_core_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_core_tests)
add_test(NAME chronon3d_core_tests COMMAND chronon3d_core_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
