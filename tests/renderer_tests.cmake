# ── Renderer & Graph Tests ──

# Blend2D-dependent test sources (only compiled when Blend2D is available)
set(RENDERER_BLEND2D_TESTS "")
if(CHRONON3D_USE_BLEND2D)
    list(APPEND RENDERER_BLEND2D_TESTS
        renderer/test_blend_pixel_nan.cpp
    )
endif()

add_executable(chronon3d_renderer_tests
    ${TEST_MAIN}
    renderer/camera/test_camera_motion.cpp
    renderer/camera/test_animated_camera.cpp
    renderer/camera/test_camera_rig.cpp
    ${RENDERER_BLEND2D_TESTS}
    renderer/camera/test_dof.cpp
    renderer/camera/test_lens_model.cpp
    renderer/camera/test_per_pixel_dof.cpp
    renderer/effects/test_adjustment_layer.cpp
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
    backends/software/sampling/test_sampler2d.cpp
    backends/software/text_run_processor_tests.cpp
    backends/software/utils/test_projection_utils.cpp
    render_graph/features/test_transition.cpp
    registry/test_registries.cpp
    cache/test_cache_sharding.cpp
    cache/test_tile_cache.cpp
    effects/test_effect_catalog.cpp
    effects/test_effect_catalog_data.cpp
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
    renderer/2d5/test_card3d_material.cpp
    renderer/helpers/test_stroke_gradient_helpers.cpp
    effects/effect_graph_tests.cpp
    scene/shapes/mask_tests.cpp
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
    render_graph/core/test_node_identity.cpp
    render_graph/cache/test_scene_program_store.cpp
    render_graph/nodes/test_multi_source_text_run.cpp
    # PR2 — node coverage (4 categories × 2 levels: ShadowNode, PerPixelDofNode,
    # MaskNode, GlowPipeline) − 8 sources total below
    render_graph/nodes/test_shadow_node_unit.cpp
    render_graph/nodes/test_shadow_node_rg_integration.cpp
    render_graph/nodes/test_per_pixel_dof_node_unit.cpp
    render_graph/nodes/test_per_pixel_dof_node_rg_integration.cpp
    render_graph/nodes/test_mask_node_unit.cpp
    render_graph/nodes/test_mask_node_rg_integration.cpp
    renderer/effects/test_glow_pipeline_unit.cpp
    renderer/effects/test_glow_pipeline_rg_integration.cpp
)
target_link_libraries(chronon3d_renderer_tests
    PRIVATE
        chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline
        chronon3d_scene
        doctest::doctest
)
if(CHRONON3D_BUILD_CONTENT)
    target_link_libraries(chronon3d_renderer_tests PRIVATE chronon3d_content)
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
