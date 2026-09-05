# Renderer integration test suites.
if(NOT (CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT))
    return()
endif()

set(_RENDERER_LINK_TARGETS
    chronon3d_sdk
    chronon3d_sdk_impl
    chronon3d_pipeline
    chronon3d_scene
)

macro(_chronon3d_renderer_target_finalize _target)
    target_include_directories(${_target} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${Stb_INCLUDE_DIR}
    )
    target_compile_definitions(${_target} PRIVATE
        CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
endmacro()

chronon3d_add_test_suite(
    NAME chronon3d_renderer_core_tests
    TIER INTEGRATION
    LINK_TARGETS ${_RENDERER_LINK_TARGETS}
    SOURCES
        backends/software/sampling/test_sampler2d.cpp
        backends/software/text_run_processor_tests.cpp
        backends/software/utils/test_projection_utils.cpp
        cache/test_cache_sharding.cpp
        cache/test_tile_cache.cpp
        registry/test_registries.cpp
        renderer/helpers/test_stroke_gradient_helpers.cpp
        renderer/lighting/test_depth_aware_shadows.cpp
        renderer/lighting/test_directional_lights.cpp
        renderer/lighting/test_light_context.cpp
        renderer/lighting/test_lighting_rig.cpp
        renderer/lighting/test_shadows.cpp
        renderer/perf/test_motion_blur_integration.cpp
        runtime/test_telemetry.cpp
        runtime/test_telemetry_report.cpp
        runtime/test_telemetry_semantic.cpp
        runtime/test_media_session_pool.cpp
        scene/shapes/mask_tests.cpp
)
_chronon3d_renderer_target_finalize(chronon3d_renderer_core_tests)
if(CHRONON3D_ENABLE_TELEMETRY)
    target_compile_definitions(chronon3d_renderer_core_tests PRIVATE
        CHRONON3D_ENABLE_SQLITE_TELEMETRY
    )
endif()
if(CHRONON3D_USE_BLEND2D)
    target_sources(chronon3d_renderer_core_tests PRIVATE
        renderer/test_blend_pixel_nan.cpp
    )
endif()
if(TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_renderer_core_tests PRIVATE
        chronon3d_backend_text
    )
endif()

chronon3d_add_test_suite(
    NAME chronon3d_render_graph_tests
    TIER INTEGRATION
    LINK_TARGETS ${_RENDERER_LINK_TARGETS}
    SOURCES
        render_graph/cache/test_scene_program_store.cpp
        render_graph/compiler/test_frame_graph_compiler_fixtures.cpp
        render_graph/compiler/test_frame_graph_compiler_reuse.cpp
        render_graph/compiler/test_frame_graph_compiler_runtime.cpp
        render_graph/compiler/test_frame_graph_compiler_golden.cpp
        render_graph/core/test_node_identity.cpp
        render_graph/features/test_transition.cpp
        render_graph/features/test_clip_transition.cpp
        render_graph/features/test_overlay_cert_fixture.cpp
        render_graph/features/test_transition_certification.cpp
        render_graph/features/test_unified_transform_path.cpp
        render_graph/nodes/test_mask_node_rg_integration.cpp
        render_graph/nodes/test_mask_node_unit.cpp
        render_graph/nodes/test_multi_source_text_run.cpp
        render_graph/nodes/test_text_run_node_execute_error.cpp
        render_graph/nodes/test_text_run_node_return_channel.cpp
        render_graph/nodes/test_text_run_predicted_bbox.cpp
        ${CMAKE_SOURCE_DIR}/src/render_graph/nodes/detail/raster_surface.hpp
        render_graph/nodes/test_raster_surface_geometry.cpp
        render_graph/nodes/test_producer_surface_bounds.cpp
        render_graph/nodes/test_per_pixel_dof_node_rg_integration.cpp
        render_graph/nodes/test_per_pixel_dof_node_unit.cpp
        render_graph/nodes/test_shadow_node_rg_integration.cpp
        render_graph/nodes/test_shadow_node_unit.cpp
        render_graph/pipeline/test_dirty_rect_contract.cpp
        render_graph/pipeline/test_dirty_rects.cpp
        render_graph/pipeline/test_dirty_rects_v2.cpp
        render_graph/pipeline/test_dirty_tiles_output.cpp
        render_graph/pipeline/test_frame_delta_compiler.cpp
        render_graph/pipeline/test_graph_health.cpp
        render_graph/pipeline/test_graph_preflight_diagnostics.cpp
        render_graph/pipeline/test_grid_math.cpp
        render_graph/pipeline/test_line_grid.cpp
        render_graph/pipeline/test_pipeline_robustness_coordinate.cpp
        render_graph/pipeline/test_pipeline_robustness_diagnostics.cpp
        render_graph/pipeline/test_pipeline_robustness_placement.cpp
        render_graph/pipeline/test_render_backend.cpp
        render_graph/pipeline/test_clip_transition_scene_integration.cpp
        render_graph/pipeline/test_render_pipeline.cpp
        render_graph/pipeline/test_tile_grid.cpp
        render_graph/pipeline/test_tile_parallel.cpp
        render_graph/test_post_processing_system.cpp
        render_graph/test_velocity_buffer_motion_blur.cpp
)
_chronon3d_renderer_target_finalize(chronon3d_render_graph_tests)
if(TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_render_graph_tests PRIVATE
        chronon3d_backend_text
    )
endif()

chronon3d_add_test_suite(
    NAME chronon3d_effects_tests
    TIER INTEGRATION
    LINK_TARGETS ${_RENDERER_LINK_TARGETS}
    SOURCES
        effects/effect_graph_tests.cpp
        effects/test_compose_color_op.cpp
        effects/test_curves.cpp
        effects/test_directional_blur.cpp
        effects/test_effect_catalog.cpp
        effects/test_effect_catalog_data.cpp
        effects/test_effect_execution_context.cpp
        effects/test_exposure_levels.cpp
        effects/test_fill_noise_offset.cpp
        effects/test_levels.cpp
        effects/test_radial_blur.cpp
        effects/test_stroke.cpp
        renderer/effects/test_adjustment_layer.cpp
        renderer/effects/test_advanced_effects.cpp
        renderer/effects/test_effect_stack.cpp
        renderer/effects/test_glow_pipeline_rg_integration.cpp
        renderer/effects/test_glow_pipeline_unit.cpp
        renderer/effects/test_glow_torture.cpp
        renderer/effects/test_invariants.cpp
)
_chronon3d_renderer_target_finalize(chronon3d_effects_tests)

chronon3d_add_test_suite(
    NAME chronon3d_camera_tests
    TIER INTEGRATION
    LINK_TARGETS ${_RENDERER_LINK_TARGETS}
    SOURCES
        renderer/camera/test_camera_motion.cpp
        renderer/camera/test_dof.cpp
        renderer/camera/test_per_pixel_dof.cpp
        renderer/2d5/test_card3d_material.cpp
        renderer/2d5/test_card3d_rasterizer.cpp
        renderer/2d5/test_depth_grade.cpp
        renderer/media/test_video_card.cpp
)
_chronon3d_renderer_target_finalize(chronon3d_camera_tests)

add_custom_target(chronon3d_renderer_tests
    DEPENDS
        chronon3d_renderer_core_tests
        chronon3d_render_graph_tests
        chronon3d_effects_tests
        chronon3d_camera_tests
        chronon3d_graphics_tests
        chronon3d_precomp_tests
)
