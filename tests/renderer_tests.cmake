# ============================================================================
# tests/renderer_tests.cmake — Renderer test suite split (§13)
#
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
# The 6 chronon3d_renderer_*_tests executables in this file all transitively
# pull `chronon3d_scene` (which itself requires the Blend2D backend + text
# subsystem for the camera / DOF / 2.5D / video card sources).  Compiled
# only when both Blend2D AND text are on (matches the pre-refactor
# orchestrator's outer `if(CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT)` wrap).
if(NOT (CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT))
    return()
endif()
#
# The monolithic chronon3d_renderer_tests executable (85 sources) is split
# into 6 per-area targets.  The old name becomes a custom target aggregator
# that DEPENDS on all six.  Source inventory is canonically recorded in
# tests/baselines/renderer_before.txt — the §13.13 gate diffs the sorted
# union of the 6 target source lists against that baseline.
#
# Target               | Area                    | Sources
# ---------------------|-------------------------|--------
# renderer_core_tests  | Core / lighting / perf  | 21 + test_main
# render_graph_tests   | Graph compiler/pipeline | 27 + test_main
# effects_tests        | Effect catalog/pipeline | 19 + test_main
# camera_tests         | Camera / DOF / 2.5D     | 10 + test_main
# golden_tests         | Golden snapshot suites  |  5 + test_main
# precomp_tests        | Precomp node cache      |  1 + test_main
#
# NOTE — Pre-existing test compilation errors (NOT caused by this split):
#   * test_mask_node_unit.cpp, test_multi_source_text_run.cpp:
#     RenderGraphContext missing members (frame, options, camera)
#   * test_precomp_node_cache.cpp, test_multi_source_text_run.cpp:
#     SoftwareRenderer* → RenderBackend* conversion failure
#   * test_mask_node_rg_integration.cpp: SoftwareRenderer& vs RenderBackend&
#   These errors exist in the C++ test code and would also fail in the
#     old monolithic target.  Fix in the C++ code, not here.
# ============================================================================

# ── Shared link contract (§11.1 INTEGRATION tier) ────────────────────
# All 6 targets were previously linked into the monolithic
# chronon3d_renderer_tests executable.  We replicate the same link
# closure so each split target compiles independently.
set(_RENDERER_LINK_TARGETS
    chronon3d_sdk
    chronon3d_sdk_impl
    chronon3d_pipeline
    chronon3d_scene
)

# ── Shared post-target properties ────────────────────────────────────
# Every split target needs the same include dirs, compile defs, and
# UNITY_BUILD=OFF setting that the monolithic executable had.
macro(_chronon3d_renderer_target_finalize _target)
    target_include_directories(${_target} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${Stb_INCLUDE_DIR}
    )
    target_compile_definitions(${_target} PRIVATE
        CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    if(CHRONON3D_BUILD_CONTENT)
        target_link_libraries(${_target} PRIVATE chronon3d_content)
        target_compile_definitions(${_target} PRIVATE
            CHRONON3D_HAS_CONTENT_MINIMALIST
            CHRONON3D_HAS_CONTENT_2D5
        )
    endif()
    set_target_properties(${_target} PROPERTIES UNITY_BUILD OFF)
endmacro()

# ── Renderer Core (21 sources) ───────────────────────────────────────
# Lighting, stabilization, telemetry, software backend, perf, shapes.
chronon3d_add_test_suite(
    NAME   chronon3d_renderer_core_tests
    TIER   INTEGRATION
    LINK_TARGETS ${_RENDERER_LINK_TARGETS}
    SOURCES
        backends/software/sampling/test_sampler2d.cpp
        backends/software/text_run_processor_tests.cpp
        backends/software/utils/test_projection_utils.cpp
        cache/test_cache_sharding.cpp
        cache/test_tile_cache.cpp
        deterministic/test_deterministic.cpp
        registry/test_registries.cpp
        renderer/helpers/test_stroke_gradient_helpers.cpp
        renderer/lighting/test_depth_aware_shadows.cpp
        renderer/lighting/test_directional_lights.cpp
        renderer/lighting/test_light_context.cpp
        renderer/lighting/test_lighting_rig.cpp
        renderer/lighting/test_shadows.cpp
        renderer/perf/test_motion_blur_integration.cpp
        renderer/perf/test_render_perf.cpp
        runtime/test_telemetry.cpp
        runtime/test_telemetry_report.cpp
        runtime/test_telemetry_semantic.cpp
        scene/shapes/mask_tests.cpp
        stabilization/test_stabilization.cpp
)
_chronon3d_renderer_target_finalize(chronon3d_renderer_core_tests)

# Blend2D-gated source (text_run_processor_tests transitively needs it)
if(CHRONON3D_USE_BLEND2D)
    target_sources(chronon3d_renderer_core_tests PRIVATE
        renderer/test_blend_pixel_nan.cpp
    )
endif()

# Text backend for text_run_processor_tests + multi_source_text_run
if(TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_renderer_core_tests PRIVATE
        chronon3d_backend_text
    )
endif()

# §12.3 — Blend2D-gated source (only needs separate registration when OFF)
if(CHRONON3D_USE_BLEND2D)
    chronon3d_register_test_source(renderer/test_blend_pixel_nan.cpp)
else()
    chronon3d_register_test_source(
        renderer/test_blend_pixel_nan.cpp
        REQUIRES CHRONON3D_USE_BLEND2D
    )
endif()

# ── Render Graph (27 sources) ────────────────────────────────────────
# Compiler, pipeline, nodes, features, post-processing, motion blur.
chronon3d_add_test_suite(
    NAME   chronon3d_render_graph_tests
    TIER   INTEGRATION
    LINK_TARGETS ${_RENDERER_LINK_TARGETS}
    SOURCES
        render_graph/cache/test_scene_program_store.cpp
        render_graph/compiler/test_frame_graph_compiler.cpp
        render_graph/core/test_node_identity.cpp
        render_graph/features/test_transition.cpp
        render_graph/features/test_clip_transition.cpp
        render_graph/features/test_transition_certification.cpp
        render_graph/features/test_unified_transform_path.cpp
        render_graph/nodes/test_mask_node_rg_integration.cpp
        render_graph/nodes/test_mask_node_unit.cpp
        render_graph/nodes/test_multi_source_text_run.cpp
        # P0-3 — audit hotspot #2 regression: TextRunNode error surfacing.
        # Locks the diagnostic invariant that every backend failure path
        # (ExecutionFailure / no-capability / null-backend) emits a
        # spdlog error with the node name + error context.  Demonstrates
        # the current codebase does NOT silently produce "successful"
        # frames when draw_text_run fails (the structural fix is
        # post-baseline-verde; this lock prevents regression during the
        # freeze).  Gated on CHRONON3D_ENABLE_TEXT.
        render_graph/nodes/test_text_run_node_execute_error.cpp
        # M1.5#1 — return-channel contract for TextRunNode::execute().
        # Locks the canonical `NodeExecResult.ok() == false` + `.error()`
        # surface (NOT just the `ctx.frame_error` sidecar checked by
        # the P0-3 lock above).  4 cases: ExecutionFailure,
        # CapabilitiesOff, NullBackend, Success.
        render_graph/nodes/test_text_run_node_return_channel.cpp
        render_graph/nodes/test_text_run_predicted_bbox.cpp
        render_graph/nodes/test_text_run_predicted_bbox_golden.cpp
        render_graph/nodes/test_per_pixel_dof_node_rg_integration.cpp
        render_graph/nodes/test_per_pixel_dof_node_unit.cpp
        render_graph/nodes/test_shadow_node_rg_integration.cpp
        render_graph/nodes/test_shadow_node_unit.cpp
        render_graph/optimizer/test_graph_optimizer.cpp
        render_graph/pipeline/test_dirty_rect_contract.cpp
        render_graph/pipeline/test_dirty_rects.cpp
        render_graph/pipeline/test_dirty_rects_v2.cpp
        render_graph/pipeline/test_dirty_tiles_output.cpp
        render_graph/pipeline/test_graph_health.cpp
        render_graph/pipeline/test_graph_preflight_diagnostics.cpp
        render_graph/pipeline/test_grid_math.cpp
        render_graph/pipeline/test_line_grid.cpp
        render_graph/pipeline/test_pipeline_robustness.cpp
        render_graph/pipeline/test_render_backend.cpp
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

# §12.3 — register handled by chronon3d_add_test_suite above.

# ── Effects (19 sources) ─────────────────────────────────────────────
# Effect catalog, curves, blur, stroke, glow pipeline.
chronon3d_add_test_suite(
    NAME   chronon3d_effects_tests
    TIER   INTEGRATION
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

# §12.3 — register handled by chronon3d_add_test_suite above.

# ── Camera (10 sources) ──────────────────────────────────────────────
# Camera motion, DOF, lens model, 2.5D, video card.
chronon3d_add_test_suite(
    NAME   chronon3d_camera_tests
    TIER   INTEGRATION
    LINK_TARGETS ${_RENDERER_LINK_TARGETS}
    SOURCES
        renderer/camera/test_animated_camera.cpp
        renderer/camera/test_camera_motion.cpp
        renderer/camera/test_dof.cpp
        renderer/camera/test_lens_model.cpp
        renderer/camera/test_per_pixel_dof.cpp
        renderer/2d5/test_card3d_material.cpp
        renderer/2d5/test_card3d_rasterizer.cpp
        renderer/2d5/test_depth_grade.cpp
        renderer/media/test_video_card.cpp
)
_chronon3d_renderer_target_finalize(chronon3d_camera_tests)

# §12.3 — register handled by chronon3d_add_test_suite above.

# ── Golden (5 sources) ───────────────────────────────────────────────
# Golden snapshot suites — glow, stroked shapes, 2.5D roadmap.
chronon3d_add_test_suite(
    NAME   chronon3d_golden_tests
    TIER   INTEGRATION
    LINK_TARGETS ${_RENDERER_LINK_TARGETS}
    SOURCES
        golden/glow_golden_tests.cpp
        golden/golden_render_tests.cpp
        golden/roadmap_2_5d_suite.cpp
        golden/stroked_shape_golden_tests.cpp
        golden/suite_chronon3d_tests.cpp
        visual/support/golden_test.cpp
        visual/support/image_diff.cpp
)
_chronon3d_renderer_target_finalize(chronon3d_golden_tests)

# §12.3 — register handled by chronon3d_add_test_suite above.

# ── Precomp (1 source) ───────────────────────────────────────────────
# Precomp node cache — isolated so it compiles independently from the
# full render_graph test bag.
chronon3d_add_test_suite(
    NAME   chronon3d_precomp_tests
    TIER   INTEGRATION
    LINK_TARGETS ${_RENDERER_LINK_TARGETS}
    SOURCES
        render_graph/nodes/test_precomp_node_cache.cpp
)
_chronon3d_renderer_target_finalize(chronon3d_precomp_tests)

# §12.3 — register handled by chronon3d_add_test_suite above.

# ── Aggregate custom target (old name → DEPENDS on all 6) ────────────
add_custom_target(chronon3d_renderer_tests
    DEPENDS
        chronon3d_renderer_core_tests
        chronon3d_render_graph_tests
        chronon3d_effects_tests
        chronon3d_camera_tests
        chronon3d_golden_tests
        chronon3d_precomp_tests
)
