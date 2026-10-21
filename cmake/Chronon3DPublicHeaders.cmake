Warning: truncated output (original token count: 5763)
Total output lines: 322

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
    "${CMAKE_SOURCE_DIR}/include/chronon/abi.h"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/motion/contract_bridge.hpp"
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
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/asset_metadata.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/asset_manifest.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/prepared_asset_manifest.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/asset_ref.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/mesh_loader.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/assets/asset_resolver.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/image/image_writer.hpp"
  …3763 tokens truncated…fest.hpp (single canonical home) and DELETED this
    #   filename. Real canonical preflight: chronon3d::AssetPreflightResolver
    #   in asset_preflight_resolver.hpp (namespace chronon3d::).

    # ── Transitive closure additions (2026-07-11) ───────────────────────
    # CameraDescriptorBuilder was removed; callers provide resolved camera state.
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/assets/image_cache.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/image/image_backend.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/image/image_decode_options.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/backends/text/text_render_resources.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/effects/effect_catalog.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/extension/extension_catalog.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/extension/extension_module.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/scene/builders/node_handle.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/text/text_placement.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/timeline/composition_descriptor.hpp"

    # Non-internal closure for the remaining supported value types.
    "${CMAKE_SOURCE_DIR}/include/chronon3d/cache/lru_cache.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/render_graph/core/node_identity.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/render_graph/pipeline/frame_parameter_table.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/render_graph/pipeline/execution_decision.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/render_graph/processor_handle.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/depth_grade.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/light_context.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/lighting_rig.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/rendering/shadow_settings.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/scheduler/for_each_tile.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/scheduler/tile_size.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/parallel_tracked.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/runtime/frame_format.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/runtime/gpu_command_plan.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/runtime/render_surface.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/runtime/resource_plan.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/runtime/resource_transition.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/runtime/resource_desc.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/runtime/resource_state.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/runtime/resource_residency.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/core/random/deterministic_random.hpp"
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
