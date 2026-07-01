# ============================================================================
# cmake/Chronon3DPublicHeaders.cmake
#
# Single source of truth for the V0.1 SDK public-header manifest.
#
# NO GLOB (`file(GLOB ...)` is the OPP-side anti-pattern: the SDK
# interface is curated by humans, not derived from the on-disk layout).
# Every header that ships inside `libchronon3d_sdk`'s installed payload
# is enumerated here explicitly — adding a header to this list is the
# act of growing the SDK public surface and must be paired with an ADR.
#
# Contract (TICKET-011 cmake-boundary — PUBLIC SURFACE):
#   The OPP-side internals live under `src/*/include_private/` and are
#   NEVER installed.  Only the headers in this list travel with the
#   SDK install payload (delivered to downstream consumers through
#   the `chronon3d_sdk` INTERFACE target's FILE_SET, see
#   cmake/Chronon3DSdkInstall.cmake).
#
# TRANSITIVE CLOSURE (audit P0 #3 — 2026-07-01):
#   The original 7-header manifest did not represent the actual
#   transitive #include closure reachable from those 7 entry points.
#   A consumer installing only the 7 headers would fail during
#   preprocessing because `sdk/render_engine.hpp` includes
#   `core/types/result.hpp` and `timeline/composition.hpp` includes
#   7 additional headers that pull in a deep chain of 70+ more.
#   This manifest now enumerates the full 85-header transitive closure
#   computed via BFS from the original 7 entry points.  Long-term the
#   SDK headers should be made more opaque (forward-declare types
#   instead of including) to shrink this list back to a curated
#   surface — tracked as post-baseline cleanup.
#
# Path style: absolute paths to `${CMAKE_SOURCE_DIR}/include/...`.
# Relative paths inside `cmake/*.cmake` resolve against
# `CMAKE_CURRENT_SOURCE_DIR = <src>/cmake` and would force a fragile
# "`../include`" workaround — absolute is unambiguous and identical
# between the manifest and the consuming `target_sources()` call site.
# ============================================================================

set(CHRONON3D_PUBLIC_HEADERS
    # ── Umbrella ────────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/chronon3d.hpp"

    # ── canonical sdk::* surface (V0.1 MVP) ──────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_engine.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_output.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_error.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_request.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_settings.hpp"

    # ── Composition type (canonical timeline leaf, shared by OPP + SDK) ─
    "${CMAKE_SOURCE_DIR}/include/chronon3d/timeline/composition.hpp"

    # ══════════════════════════════════════════════════════════════════════
    # Transitive closure — headers reachable from the 7 entry points above
    # (audit P0 #3).  Sorted by directory for readability.
    # ══════════════════════════════════════════════════════════════════════

    # ── animation/core ──────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/animated_value.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/animation_track.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/detail/animated_value_bezier.inl"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/detail/animated_value_evaluation.inl"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/detail/animated_value_expressions.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/detail/animated_value_roving.inl"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/keyframe.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/quaternion_track.hpp"

    # ── animation/easing ────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/easing/easing.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/easing/interpolate.hpp"

    # ── animation/effects ───────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/effects/animated_transform.hpp"

    # ── assets ──────────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/asset_metadata.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/asset_registry.hpp"

    # ── api ────────────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/api/render_engine.hpp"

    # ── backends/image ──────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/image/image_writer.hpp"

    # ── backends/software ───────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/software/render_settings.hpp"

    # ── core/composition ────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/composition/composition_registry.hpp"

    # ── core/config ─────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/config.hpp"

    # ── core/memory ─────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/memory/framebuffer.hpp"

    # ── runtime ─────────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/runtime/render_session.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/runtime/session_services.hpp"

    # ── timeline ──────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/timeline/composition_props.hpp"

    # ── core/scheduler ─────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/scheduler/scheduler_mode.hpp"

    # ── core/profiling ────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/profiling/profiling.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/profiling/counters.hpp"

    # ── core/memory transitive ───────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/memory/memory_utils.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/memory/arena.hpp"

    # ── simd ─────────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/simd/kernels.hpp"

    # ── math ────────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/renderer_state.hpp"

    # ── render_graph ───────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/render_graph/cache/scene_program_store.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/render_graph/core/scene_hasher.hpp"

    # ── backends/software/sampling ────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/software/sampling/edge_mode.hpp"

    # ── compositor ──────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/compositor/alpha.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/compositor/blend_mode.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/compositor/composite_operator.hpp"

    # ── core ────────────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/enum_utils.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/frame.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/frame_context.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/result.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/sample_time.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/time.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/types.hpp"

    # ── effects ─────────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/effects/effect_params.hpp"

    # ── geometry ────────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/geometry/bounds.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/geometry/mesh.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/geometry/vertex.hpp"

    # ── graphics ────────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/graphics/gradient.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/graphics/shape_style/fill_style.hpp"

    # ── layout ──────────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/layout/layout_flow_grid.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/layout/layout_rules.hpp"

    # ── math ────────────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/camera_2_5d_projection.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/camera_pose.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/camera_projection_contract.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/camera_projection_resolver.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/color.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/expression.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/expression_types.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/glm_types.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/near_plane_clip.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/projection_context.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/projector_2_5d.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/raster_utils.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/transform.hpp"

    # ── media ───────────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/media/media_placement.hpp"

    # ── rendering ───────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/depth_grade.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/light_context.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/lighting_rig.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/projected_card.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/shadow_settings.hpp"

    # ── scene/camera/camera_v1 ──────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/arc_length_table.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_descriptor.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_motion_context.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_program.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_trajectory.hpp"

    # ── scene/model/camera ──────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/camera/camera.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/camera/camera_2_5d.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/camera/camera_common_types.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/camera/camera_projection_source.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/camera/lens_model.hpp"

    # ── scene/model/core ────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/core/depth_role.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/core/mask_utils.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/core/scene.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/core/transition.hpp"

    # ── scene/model/layer ───────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/layer/layer.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/layer/layer_hierarchy.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/layer/mask.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/layer/track_matte.hpp"

    # ── scene/model/render ──────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/render/render_node.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/render/render_runtime.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/render/resolved_types.hpp"

    # ── scene/model/shape ───────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/fill.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/material_2_5d.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/path.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/shape.hpp"

    # ── text ────────────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/font_engine.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_direction.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_material.hpp"
)
