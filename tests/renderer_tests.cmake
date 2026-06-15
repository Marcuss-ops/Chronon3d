# ── Renderer & Graph Tests ──

add_executable(chronon3d_renderer_tests
    ${TEST_MAIN}
    renderer/camera/test_camera_motion.cpp
    renderer/camera/test_animated_camera.cpp
    renderer/camera/test_camera_rig.cpp
    renderer/test_blend_pixel_nan.cpp
    renderer/camera/test_dof.cpp
    renderer/camera/test_lens_model.cpp
    renderer/camera/test_per_pixel_dof.cpp
    renderer/effects/test_adjustment_layer.cpp
    renderer/effects/test_adjustment_layer_ae5.cpp
    renderer/effects/test_time_remap.cpp
    renderer/effects/test_precomp.cpp
    renderer/effects/test_advanced_effects.cpp
    renderer/effects/test_effect_stack.cpp
    renderer/effects/test_glow_torture.cpp
    renderer/effects/test_invariants.cpp
    renderer/media/test_video_card.cpp
    stabilization/test_stabilization.cpp
    renderer/perf/test_render_perf.cpp
    renderer/perf/test_motion_blur_integration.cpp
    renderer/lighting/test_light_context.cpp
    renderer/lighting/test_directional_lights.cpp
    renderer/lighting/test_shadows.cpp
    renderer/lighting/test_lighting_rig.cpp
    renderer/lighting/test_depth_aware_shadows.cpp
    renderer/2d5/test_card3d_rasterizer.cpp
    renderer/2d5/test_depth_grade.cpp
    render_graph/features/test_unified_transform_path.cpp
    renderer/dispatch/test_software_node_dispatcher.cpp
    backends/software/sampling/test_sampler2d.cpp
    render_graph/features/test_transition.cpp
    registry/test_registries.cpp
    cache/test_cache_sharding.cpp
    cache/test_tile_cache.cpp
    effects/test_effect_registry.cpp
    effects/test_effect_catalog.cpp
    effects/test_exposure_levels.cpp
    effects/test_levels.cpp
    effects/test_fill_noise_offset.cpp
    effects/test_directional_blur.cpp
    effects/test_radial_blur.cpp
    effects/test_stroke.cpp
    effects/test_curves.cpp
    effects/test_compose_color_op.cpp
    effects/test_effect_execution_context.cpp
    deterministic/test_deterministic.cpp
    render_graph/pipeline/test_render_pipeline.cpp
    render_graph/pipeline/test_line_grid.cpp
    render_graph/pipeline/test_pipeline_robustness.cpp
    render_graph/pipeline/test_graph_health.cpp
    render_graph/pipeline/test_grid_math.cpp
    render_graph/pipeline/test_render_backend.cpp
    render_graph/pipeline/test_dirty_rects.cpp
    render_graph/pipeline/test_dirty_rect_contract.cpp
    render_graph/optimizer/test_graph_optimizer.cpp
    render_graph/compiler/test_frame_graph_compiler.cpp
    render_graph/pipeline/test_dirty_rects_v2.cpp
    render_graph/pipeline/test_tile_grid.cpp
    render_graph/pipeline/test_dirty_tiles_output.cpp
    render_graph/pipeline/test_tile_parallel.cpp
    render_graph/pipeline/test_graph_preflight_diagnostics.cpp
    scene/camera/camera_25d_tests.cpp
    renderer/2d5/test_card3d_material.cpp
    renderer/helpers/test_stroke_gradient_helpers.cpp
    effects/effect_graph_tests.cpp
    scene/shapes/mask_tests.cpp
    specscene/specscene_boundary_tests.cpp
    golden/golden_render_tests.cpp
    golden/glow_golden_tests.cpp
    golden/stroked_shape_golden_tests.cpp
    golden/suite_chronon3d_tests.cpp
    golden/roadmap_2_5d_suite.cpp
    runtime/test_telemetry.cpp
    runtime/test_telemetry_semantic.cpp
    runtime/test_telemetry_report.cpp
    render_graph/test_velocity_buffer_motion_blur.cpp
    render_graph/test_post_processing_system.cpp
    render_graph/nodes/test_precomp_node_cache.cpp
)
target_link_libraries(chronon3d_renderer_tests
    PRIVATE
        chronon3d_pipeline
        chronon3d_scene
        doctest::doctest
)
# WHOLE_ARCHIVE removed — content uses explicit ExtensionRegistry registration
if(CHRONON3D_BUILD_CONTENT)
    if(TARGET chronon3d_content_shapes)
        target_link_libraries(chronon3d_renderer_tests PRIVATE chronon3d_content_shapes)
    endif()
    if(TARGET chronon3d_content_images)
        target_link_libraries(chronon3d_renderer_tests PRIVATE chronon3d_content_images)
    endif()
    if(TARGET chronon3d_content_anims)
        target_link_libraries(chronon3d_renderer_tests PRIVATE chronon3d_content_anims)
    endif()
    if(TARGET chronon3d_content_minimalist)
        target_link_libraries(chronon3d_renderer_tests PRIVATE chronon3d_content_minimalist)
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
    target_sources(chronon3d_renderer_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/content/register_minimalist_content.cpp
        ${CMAKE_SOURCE_DIR}/content/register_2d5_content.cpp
    )
    target_include_directories(chronon3d_renderer_tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(chronon3d_renderer_tests PRIVATE CHRONON3D_HAS_CONTENT_MINIMALIST CHRONON3D_HAS_CONTENT_2D5)
endif()
target_compile_definitions(chronon3d_renderer_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(chronon3d_renderer_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(chronon3d_renderer_tests PRIVATE ${Stb_INCLUDE_DIR})
# Disable unity build to prevent anonymous namespace collisions (e.g. colors_near)
set_target_properties(chronon3d_renderer_tests PROPERTIES UNITY_BUILD OFF)
chronon3d_enable_test_pch(chronon3d_renderer_tests)
add_test(NAME chronon3d_renderer_tests COMMAND chronon3d_renderer_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
