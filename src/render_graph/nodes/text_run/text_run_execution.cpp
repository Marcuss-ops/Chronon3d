// SPDX-License-Identifier: MIT
//
// M1.5#1 — implementation for TextRunNode execution-stage helpers.
// See text_run_execution.hpp for the contract.

#include "text_run_execution.hpp"
#include "text_run_transform.hpp"
#include "gpu_text_run.hpp"
#include "../native_surface.hpp"

#include <chronon3d/text/text_run_driver.hpp>   // update_text_run_shape_per_frame
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::graph::text_run {

std::optional<NodeExecutionError> validate_execution(
    const RenderBackend* backend,
    std::string_view node_name
) {
    if (!backend) {
        // P0-3: surface via NodeExecutionError(InvalidInput), not a
        // cleared fb.  The diagnostic signature is preserved verbatim
        // for the regression lock in test_text_run_node_execute_error.cpp
        // ("backend is null" + node name).
        spdlog::error(
            "[text-run] node='{}' cannot render: backend is null; "
            "returning NodeExecutionError(InvalidInput) via execute().",
            node_name);
        return NodeExecutionError{
            RenderBackendErrorCode::InvalidInput,
            std::string(node_name),
            "backend is null"
        };
    }
    if (!backend->capabilities().text_run) {
        // PR2 — capability gate short-circuits before any
        // backend->draw_text_run() call so the slow path never fires.
        spdlog::error(
            "[text-run] node='{}' backend does not support "
            "draw_text_run; returning "
            "NodeExecutionError(UnsupportedCapability) via execute().",
            node_name);
        return NodeExecutionError{
            RenderBackendErrorCode::UnsupportedCapability,
            std::string(node_name),
            "backend does not support draw_text_run"
        };
    }
    return std::nullopt;
}

#ifdef CHRONON3D_ENABLE_TEXT

TextRunShape prepare_per_frame_shape(
    const TextRunShape& source,
    chronon3d::SampleTime sample_time
) {
    // A6 immutability: shallow-copy the underlying layout (shared_ptr)
    // and deep-copy the mutable per-glyph vector.  Evaluating the
    // animator stack into the clone keeps `m_shape` (the compiled
    // immutable node input) read-only across calls.
    TextRunShape local = source;
    chronon3d::update_text_run_shape_per_frame(local, sample_time);

    return local;
}

// ═══════════════════════════════════════════════════════════════════════════
// render_text_run_item — unified TextRun rendering (TextRunNode + MultiSourceNode)
// ═══════════════════════════════════════════════════════════════════════════

graph::RenderOpResult render_text_run_item(
    const RenderGraphContext& ctx,
    RenderBackend& backend,
    Framebuffer& fb,
    const TextRunShape& source_shape,
    const TextRunPlacement& placement,
    float opacity
) {
    // A6 immutability: clone the mutable per-glyph vector, evaluate
    // animators into the clone.
    TextRunShape local_shape = source_shape;
    chronon3d::update_text_run_shape_per_frame(local_shape, ctx.frame_input.sample_time);

    glm::mat4 world_matrix = build_world_matrix(ctx, placement);
    if (placement.tight_surface) {
        // The producer framebuffer is a local [0,size) raster surface. Its
        // rasterizer already applies the run-local offset while composing the
        // glyph image, so the producer must only translate the authored local
        // origin into that surface. Applying node.world_transform here would
        // transform the text once in the producer and again in TransformNode.
        world_matrix = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(-placement.surface_origin.x,
                      -placement.surface_origin.y,
                      0.0f));
    }
    auto& mutable_ctx = const_cast<RenderGraphContext&>(ctx);
    auto native = draw_cached_text_run(
        mutable_ctx, fb, local_shape, world_matrix, opacity);
    if (native.ok()) {
        return native;
    }
    if (ctx.policy.diagnostics_enabled) {
        spdlog::debug(
            "[text-run] native surface path fell back: {}",
            native.error().message);
    }
    // The GPU atlas path is an optimization, not a correctness boundary.
    // Stale atlas/surface state must fall back to Chronon's canonical text
    // renderer instead of aborting the complete video frame.  The backend
    // still owns the final native surface and the caller keeps the failure
    // visible in its telemetry/logs.
    auto fallback = backend.draw_text_run(fb, local_shape, world_matrix, opacity);
    if (fallback.ok() && fb.surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
        if (!ensure_native_surface(mutable_ctx, fb)) {
            return graph::RenderOpResult(graph::RenderBackendError{
                RenderBackendErrorCode::ExecutionFailure,
                "TextRun fallback rendered but could not be uploaded to the native surface"});
        }
    }
    return fallback;
}

#endif // CHRONON3D_ENABLE_TEXT

}  // namespace chronon3d::graph::text_run
