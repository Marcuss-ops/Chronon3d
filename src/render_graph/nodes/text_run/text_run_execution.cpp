// SPDX-License-Identifier: MIT
//
// M1.5#1 — implementation for TextRunNode execution-stage helpers.
// See text_run_execution.hpp for the contract.

#include "text_run_execution.hpp"
#include "text_run_transform.hpp"
#include "gpu_text_run.hpp"
#include "../native_surface.hpp"

#include <chronon3d/core/profiling/profiling_context.hpp>
#include <chronon3d/text/text_run_driver.hpp>   // update_text_run_shape_per_frame
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace chronon3d::graph::text_run {

namespace {

// Promote a CPU-rasterized text fallback without uploading an entire canvas.
// TextRun producer framebuffers are cleared before rasterization, therefore
// pixels outside actual_ink_bbox are known-transparent.  We materialize that
// invariant on the GPU with one transparent clear and upload only the ink ROI.
// If the backend cannot satisfy any step, the caller keeps the historical
// full-surface promotion as its correctness fallback.
[[nodiscard]] bool promote_text_fallback_region(
    RenderGraphContext& ctx,
    RenderBackend& backend,
    Framebuffer& framebuffer,
    const std::optional<raster::BBox>& ink_bbox) {
    if (!ink_bbox || !ctx.services.surface_registry ||
        !backend.supports_native_surfaces()) {
        return false;
    }

    const auto& bbox = *ink_bbox;
    const int local_x0 = std::clamp(
        bbox.x0 - framebuffer.origin_x(), 0, framebuffer.width());
    const int local_y0 = std::clamp(
        bbox.y0 - framebuffer.origin_y(), 0, framebuffer.height());
    const int local_x1 = std::clamp(
        bbox.x1 - framebuffer.origin_x(), 0, framebuffer.width());
    const int local_y1 = std::clamp(
        bbox.y1 - framebuffer.origin_y(), 0, framebuffer.height());

    if (!ensure_empty_native_surface(ctx, framebuffer)) {
        return false;
    }

    // A region-only upload must still leave a semantically complete source
    // surface because later native consumers are allowed to sample outside the
    // current composite clip.  The CPU producer starts transparent, so mirror
    // that state once on-device instead of transferring the full framebuffer.
    const auto cleared = backend.fill_rect_surface(
        framebuffer.surface_handle(),
        0, 0, framebuffer.width(), framebuffer.height(),
        Color{0.0f, 0.0f, 0.0f, 0.0f});
    if (!cleared.ok()) {
        release_native_surface(ctx, framebuffer);
        return false;
    }

    if (local_x1 <= local_x0 || local_y1 <= local_y0) {
        return true;  // no visible ink: transparent GPU surface is complete
    }

    const auto width = static_cast<std::uint32_t>(local_x1 - local_x0);
    const auto height = static_cast<std::uint32_t>(local_y1 - local_y0);
    std::vector<float> rgba(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    static_assert(sizeof(Color) == sizeof(float) * 4);
    for (std::uint32_t y = 0; y < height; ++y) {
        const Color* row = framebuffer.pixels_row(
            local_y0 + static_cast<int>(y)) + local_x0;
        std::memcpy(
            rgba.data() + static_cast<std::size_t>(y) * width * 4,
            row,
            static_cast<std::size_t>(width) * sizeof(Color));
    }

    profiling::GpuUploadProducerScope upload_scope(
        profiling::GpuUploadProducer::Text);
    const auto uploaded = backend.upload_surface_region(
        framebuffer.surface_handle(),
        native_surface_desc(framebuffer.width(), framebuffer.height()),
        local_x0, local_y0, width, height, rgba);
    if (!uploaded.ok()) {
        release_native_surface(ctx, framebuffer);
        return false;
    }
    return true;
}

}  // namespace

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
    if (fallback.ok() && fb.surface_handle() == runtime::kInvalidRenderSurfaceHandle &&
        backend.supports_native_surfaces()) {
        // The fallback framebuffer is transparent outside its actual ink.
        // Promote only that region whenever the backend reports it.  This
        // avoids 1280x720/1080p RGBA32F full-surface uploads for small text
        // overlays while retaining ensure_native_surface() as the exact
        // historical correctness fallback when the ROI path is unavailable.
        if (!promote_text_fallback_region(
                mutable_ctx, backend, fb, fallback.value().actual_ink_bbox) &&
            !ensure_native_surface(mutable_ctx, fb)) {
            return graph::RenderOpResult(graph::RenderBackendError{
                RenderBackendErrorCode::ExecutionFailure,
                "TextRun fallback rendered but could not be uploaded to the native surface"});
        }
    }
    return fallback;
}

#endif // CHRONON3D_ENABLE_TEXT

}  // namespace chronon3d::graph::text_run
