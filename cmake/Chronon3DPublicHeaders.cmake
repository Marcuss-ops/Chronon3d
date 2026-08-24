# ============================================================================
# cmake/Chronon3DPublicHeaders.cmake
#
# Single source of truth for the V0.1 SDK public-header manifest.
# NO GLOB. Every header is enumerated explicitly.
# ============================================================================

# Supported API surface. These headers are the only headers that consumers
# should include directly. The install closure below exists solely because
# CMake FILE_SET requires every textual include to be present in the package.
set(CHRONON3D_SDK_API_HEADERS
    # ── canonical sdk::* surface (V0.1 MVP) ──────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_engine.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_file_request.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/c_api/chronon3d.h"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_output.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_error.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_request.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_settings.hpp"

    # ── Verification (canonical render receipt) ───────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/verification/render_receipt.hpp"

    # ── Composition type ─────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/timeline/composition.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/timeline/compile_evaluate.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/timeline/compiled_composition.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/timeline/evaluated_composition_frame.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/timeline/composition_definition.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/timeline/composition_props.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/text.hpp"
)

# Headers required to compile the supported API. They are package closure,
# not supported API: consumers must not depend on these paths directly.
set(CHRONON3D_SDK_REQUIRED_TRANSITIVE_HEADERS
    # ── Transitive closure ───────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/animated_value.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/animation_track.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/detail/animated_value_bezier.inl"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/detail/animated_value_evaluation.inl"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/detail/animated_value_expressions.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/detail/animated_value_roving.inl"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/keyframe.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/quaternion_track.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/easing/easing.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/easing/interpolate.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/effects/animated_transform.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/effects/stagger.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/api/composition.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/api/scene.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/asset_metadata.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/asset_manifest.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/prepared_asset_manifest.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/asset_ref.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/mesh_loader.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/asset_resolver.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/image/image_writer.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/software/render_settings.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/render_plan/render_budget.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/software/sampling/edge_mode.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/video/video_source.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/hash/hash_builder.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/compositor/alpha.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/compositor/blend_mode.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/compositor/composite_operator.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/composition/composition_registry.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/dirty_fallback_reason.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/enum_utils.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/memory/arena.hpp"
    # detail/ framebuffer_impl.hpp transitively required by core/memory/framebuffer.hpp:27
    # (relative `#include "detail/framebuffer_impl.hpp"`); de-facto public by
    # directory convention (under include/chronon3d/, NOT under internal/).
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/memory/detail/framebuffer_impl.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/memory/framebuffer.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/memory/framebuffer_handle.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/runtime/render_surface_handle.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/memory/framebuffer_slot_view.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/memory/memory_utils.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/profiling/counters.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/profiling/render_counter_macros.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/profiling/render_counter_types.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/profiling/profiling.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/profiling/timing.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/profiling/profiling_context.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/tracing/tracing.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/tracing/tracing_categories.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/tracing/trace_options.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/tracing/trace_session.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/tracing/trace_ids.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/scheduler/scheduler_mode.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/frame.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/frame_context.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/result.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/sample_time.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/time.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/types.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/effects/effect_catalog_data.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/effects/effect_catalog.def"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/effects/effect_category.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/effects/effect_descriptor.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/effects/effect_execution_context.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/effects/effect_ids.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/effects/effect_instance.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/effects/effect_params.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/effects/effect_stage.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/effects/effect_traits.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/effects/effect_type.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/effects/presets/glow_presets.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/geometry/bounds.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/geometry/mesh.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/geometry/vertex.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/graphics/gradient.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/graphics/shape_style/fill_style.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/graphics/shape_style/fill_style_lerp.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/graphics/shape_style/stroke_style.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/layout/layout_flow_grid.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/layout/layout_rules.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/camera_2_5d_projection.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/camera_pose.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/camera_projection_contract.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/camera_projection_clip.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/camera_projection_frustum.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/camera_projection_matrix.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/camera_projection_resolver.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/color.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/expression.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/expression_builtins.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/expression_types.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/glm_types.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/near_plane_clip.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/projection_context.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/projector_2_5d.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/raster_utils.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/transform.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/presets/motion_animation.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/presets/motion_object.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/presets/motion_state.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/registry/shape_ids.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/registry/shape_params.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/registry/shape_registry.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/media/media_placement.hpp"
    # P3-H + feat(api) public camera facade — RenderGraph moved to
    # include/chronon3d/internal/ (TICKET-CAMERA-FULL-LINUX sub-ticket B).
    # External consumers MUST NOT see this type.  RenderGraph is the
    # internal pipeline topology used by the OPP renderer; the public
    # surface exposes only `chronon3d::sdk::RenderEngine::render(...)`.
    # "${CMAKE_SOURCE_DIR}/include/chronon3d/render_graph/render_graph.hpp"  # HIDDEN
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/depth_grade.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/light_context.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/lighting_rig.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/projected_card.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/shadow_settings.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/builder_params.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/scene_builder.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/sequence_builder.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/layer_builder.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/detail/layer_builder_text.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/detail/layer_builder_inline.inl"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/detail/scene_builder_inline.inl"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/detail/scene_builder_layers.inl"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/detail/scene_builder_sequences.inl"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/camera_api.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/null_builder.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/text_run_builder.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/arc_length_table.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_catalog.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_constraint.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_descriptor.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_presets.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_preset_params.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animations/camera_motion_params.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_descriptor_fingerprint.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_framing_solver.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_projection.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_motion_context.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_program.hpp"
    # P3-H + feat(api) public camera facade — CameraSession moved to
    # include/chronon3d/internal/ (TICKET-CAMERA-FULL-LINUX sub-ticket B).
    # External consumers MUST NOT see this type.  The session lives in the
    # per-frame evaluation hot path and is owned by the OPP renderer.
    # "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_session.hpp"  # HIDDEN
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_trajectory.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/evaluated_projection.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/camera/camera.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/camera/camera_2_5d.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/camera/camera_common_types.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/camera/camera_projection_source.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/camera/dof.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/camera/lens_model.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/core/depth_role.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/core/card3d_material.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/core/clip_transition.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/core/hierarchy_resolver.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/core/effect_stack.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/core/mask_utils.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/core/scene.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/core/transition.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/layer/layer.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/layer/layer_hierarchy.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/layer/mask.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/layer/time_remap.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/layer/layer_time_resolver.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/layer/track_matte.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/render/render_node.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/render/render_node_params.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/render/resolved_types.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/circle_shape.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/fill.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/line_shape.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/material_2_5d.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/path.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/rect_shape.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/rounded_rect_shape.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/shape.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/shape_stroke.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/transform_3d.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/simd/kernels.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/animated_text_document.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/animation/glyph_instance_state.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/animation/text_animator_evaluator.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/animation/text_animator_properties.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/animation/text_animator_spec.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/animation/text_animator_stack.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_document.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_definition.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_appearance_spec.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_content.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_layout_spec.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_defaults.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_span_override.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_span.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_run_definition.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/prepared_text.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_shaping_options.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/timed_text_document.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_layout_cache.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_layout_identity.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/font_engine.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/glyph_selector.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/glyph_selector_spec.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/paragraph_style.hpp"
    # ── Phase A6 close-out (2026-07-10): canonical text placement resolver ──
    #   ADR-019 Decision 3 establishes `resolve_text_placement()` as the
    #   SINGLE canonical surface for converting high-level placement
    #   semantics into the `ResolvedTextPlacement` the renderer consumes.
    #   Phase A6 removed the parallel `class TextPlacementResolver` shim
    #   that previously co-resident with the free function in the
    #   retired `text_placement_resolver.hpp`.  That wrapper header is now
    #   BANNED from `include/chronon3d/` by gate #23 in
    #   `tools/check_architecture_boundaries.sh`.  If you want to re-expose
    #   the wrapper class you MUST update ADR + open a new ticket.
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/resolve_text_placement.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_animator_property.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_direction.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_material.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_run.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_run_layout.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_run_hash.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_run_shape.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/vector/path_factories.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/vector/shape_style.hpp"
    # ── Phase A2 #3/3 (2026-07-10): Asset Readiness V2 POD types INLINE ──
    #   Previously `asset_readiness_v2.hpp` (M1.7 Step 1) lived here with
    #   3 v2::Asset{Preflight*}-related symbols. Phase A1 removed the
    #   always-green preflight stubs (gate #18). Phase A2 #1 collapsed
    #   the v2 namespace into the flat assets:: namespace + renamed
    #   AssetRef POD -> InternalAssetRef + added migration gate #21.
    #   Phase A2 #2 promoted AssetManifest out (canonical body now in
    #   asset_manifest.hpp). Phase A2 #3 INLINED the remaining two
    #   PODs (AssetKind enum + InternalAssetRef struct) into
    #   asset_manifest.hpp (single canonical home) and DELETED this
    #   filename. Real canonical preflight: chronon3d::AssetPreflightResolver
    #   in asset_preflight_resolver.hpp (namespace chronon3d::).

    # ── Transitive closure additions (2026-07-11) ───────────────────────
    # P3-H + feat(api) public camera facade — NEW public headers.
    #   scene_camera_facade.hpp     → `scene.camera().descriptor/program/timeline/preset`
    #                                  chainable setters (back-references Scene)
    #   camera_descriptor_builder.hpp → `chronon3d::camera()` fluent builder
    #                                   for `CameraDescriptor` (with `PhysicalLens`
    #                                   convenience struct for the spec example)
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/scene_camera_facade.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_descriptor_builder.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/animator.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/basic_registry.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/material.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/motion_registry.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/resolution_outcome.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/selector.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/style_registry.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/text.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/text_span_builder.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/assets/image_cache.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/image/image_backend.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/image/image_decode_options.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/text/text_render_resources.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/effects/effect_catalog.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/presets/motion_parameters.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/extension/extension_catalog.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/extension/extension_module.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/node_handle.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_placement.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/timeline/composition_descriptor.hpp"

    # Non-internal closure for the remaining supported value types.
    "${CMAKE_SOURCE_DIR}/include/chronon3d/cache/lru_cache.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/render_graph/core/node_identity.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/render_graph/processor_handle.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/scheduler/for_each_tile.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/scheduler/tile_size.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/parallel_tracked.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/random/deterministic_random.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/raster/bbox.hpp"

    # Header-only authoring closure required by the supported API.
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_content_font.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_placement_layout.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_appearance_animation.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_registry_access.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/authoring/detail/text_private.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/params/shape_params.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/params/media_params.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/params/text_params.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/params/three_d_params.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/pending_text_run.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/text_run_materialization.hpp"
)

# Internal build headers are intentionally not listed here. In particular,
# include/chronon3d/internal/*, runtime adapters, graph executors, caches and
# render sessions stay private to the implementation target.
set(CHRONON3D_INTERNAL_BUILD_HEADERS
    "${CMAKE_SOURCE_DIR}/include/chronon3d/internal"
)

# Backward-compatible aggregate consumed by the install FILE_SET and by the
# standalone public-header checker. De-duplication keeps an API header from
# being exported twice when it is also needed by the transitive closure.
set(CHRONON3D_PUBLIC_HEADERS
    ${CHRONON3D_SDK_API_HEADERS}
    ${CHRONON3D_SDK_REQUIRED_TRANSITIVE_HEADERS}
)
list(REMOVE_DUPLICATES CHRONON3D_PUBLIC_HEADERS)
