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

# === INTERNAL (NOT in CHRONON3D_PUBLIC_HEADERS - OPP-only) ===
# The OPP internal architecture (runtime/render_session, render_graph/core/scene_hasher,
# render_graph/cache/scene_program_store, runtime/session_services, etc) lives under
# include/chronon3d/internal/. These are NOT installed and are deliberately absent from
# this manifest. OPP-side callers reach them via the OPP INCLUDE build path.
# Re-introducing these into CHRONON3D_PUBLIC_HEADERS is the canonical SDK surface-area
# leak (per ANTI_DUPLICATION_RULES.md SDK section 1).
# Relative paths inside `cmake/*.cmake` resolve against
# `CMAKE_CURRENT_SOURCE_DIR = <src>/cmake` and would force a fragile
# "`../include`" workaround — absolute is unambiguous and identical
# between the manifest and the consuming `target_sources()` call site.
# ============================================================================

set(CHRONON3D_PUBLIC_HEADERS
    # ── Umbrella ────────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/chronon3d.hpp"

    # ── canonical sdk::* surface (V0.1 MVP) ──────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_engine.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_output.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_error.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_request.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_settings.hpp"

    # ── Composition type (canonical timeline leaf, shared by OPP + SDK) ─
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/timeline/composition.hpp"

    # ══════════════════════════════════════════════════════════════════════
    # Transitive closure — headers reachable from the 7 entry points above
    # (audit P0 #3).  Sorted by directory for readability.
    # ══════════════════════════════════════════════════════════════════════

    # ── animation/core ──────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/animated_value.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/animation_track.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/detail/animated_value_bezier.inl"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/detail/animated_value_evaluation.inl"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/detail/animated_value_expressions.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/detail/animated_value_roving.inl"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/keyframe.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/core/quaternion_track.hpp"

    # ── animation/easing ────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/easing/easing.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/easing/interpolate.hpp"

    # ── animation/effects ───────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/animation/effects/animated_transform.hpp"

    # ── assets ──────────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/asset_metadata.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/asset_registry.hpp"

    # ── api ────────────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/api/render_engine.hpp"

    # ── backends/image ──────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/image/image_writer.hpp"

    # ── backends/software ───────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/software/render_settings.hpp"

    # ── core/composition ────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/composition/composition_registry.hpp"

    # ── core/config ─────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/config.hpp"

    # ── core/memory ─────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/memory/framebuffer.hpp"

    # ── runtime ─────────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/runtime/render_session.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/runtime/session_services.hpp"

    # ── timeline ──────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/timeline/composition_props.hpp"

    # ── core/scheduler ─────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/scheduler/scheduler_mode.hpp"

    # ── core/profiling ────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/profiling/profiling.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/profiling/trace_categories.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/profiling/counters.hpp"

    # ── core/memory transitive ───────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/memory/memory_utils.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/memory/arena.hpp"

    # ── simd ─────────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/simd/kernels.hpp"

    # ── math ────────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/renderer_state.hpp"

    # ── render_graph ───────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/render_graph/cache/scene_program_store.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/render_graph/core/scene_hasher.hpp"

    # ── backends/software/sampling ────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/software/sampling/edge_mode.hpp"

    # ── compositor ──────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/compositor/alpha.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/compositor/blend_mode.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/compositor/composite_operator.hpp"

    # ── core ────────────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/enum_utils.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/frame.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/frame_context.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/result.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/sample_time.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/time.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/types/types.hpp"

    # ── effects ─────────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/effects/effect_params.hpp"

    # ── geometry ────────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/geometry/bounds.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/geometry/mesh.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/geometry/vertex.hpp"

    # ── graphics ────────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/graphics/gradient.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/graphics/shape_style/fill_style.hpp"

    # ── layout ──────────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/layout/layout_flow_grid.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/layout/layout_rules.hpp"

    # ── math ────────────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/camera_2_5d_projection.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/camera_pose.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/camera_projection_contract.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/camera_projection_resolver.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/color.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/expression.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/expression_types.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/glm_types.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/near_plane_clip.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/projection_context.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/projector_2_5d.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/raster_utils.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/math/transform.hpp"

    # ── media ───────────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/media/media_placement.hpp"

    # ── rendering ───────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/depth_grade.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/light_context.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/lighting_rig.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/projected_card.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/shadow_settings.hpp"

    # ── scene/camera/camera_v1 ──────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/arc_length_table.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_descriptor.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_motion_context.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_program.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/camera/camera_v1/camera_trajectory.hpp"

    # ── scene/model/camera ──────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/camera/camera.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/camera/camera_2_5d.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/camera/camera_common_types.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/camera/camera_projection_source.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/camera/lens_model.hpp"

    # ── scene/model/core ────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/core/depth_role.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/core/mask_utils.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/core/scene.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/core/transition.hpp"

    # ── scene/model/layer ───────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/layer/layer.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/layer/layer_hierarchy.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/layer/mask.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/layer/track_matte.hpp"

    # ── scene/model/render ──────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/render/render_node.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/render/render_runtime.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/render/resolved_types.hpp"

    # ── scene/model/shape ───────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/fill.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/material_2_5d.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/path.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/model/shape/shape.hpp"

    # ── text ────────────────────────────────────────────────────────────
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/font_engine.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_direction.hpp"
nclude/chronon3d/runtime/render_session.hpp"/d
nclude/chronon3d/runtime/session_services.hpp"/d
nclude/chronon3d/render_graph/cache/scene_program_store.hpp"/d
nclude/chronon3d/render_graph/core/scene_hasher.hpp"/d
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_material.hpp"
)
