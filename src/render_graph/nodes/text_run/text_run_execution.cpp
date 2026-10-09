// SPDX-License-Identifier: MIT
//
// M1.5#1 — implementation for TextRunNode execution-stage helpers.
// See text_run_execution.hpp for the contract.

#include "text_run_execution.hpp"
#include "text_run_transform.hpp"
#include "gpu_text_run.hpp"
#include "../native_surface.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/profiling_context.hpp>
#include <chronon3d/text/text_run_driver.hpp>   // update_text_run_shape_per_frame
#include <chronon3d/compiler/frame_parameters.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <optional>
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
    if (!ink_bbox || !ctx.surface_registry ||
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

    if (local_x1 <= local_x0 || local_y1 <= local_y0) {
        return true;  // no visible ink: transparent GPU surface is complete
    }

    const auto width = static_cast<std::uint32_t>(local_x1 - local_x0);
    const auto height = static_cast<std::uint32_t>(local_y1 - local_y0);
    auto& rgba = ctx.node_exec.text_upload_scratch;
    rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
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

void prepare_per_frame_shape(
    const TextRunShape& source,
    chronon3d::SampleTime sample_time,
    TextRunFrameState& frame,
    const compiler::SampledLayerParameters* sampled_layer
) {
    const auto timing_start = profiling::g_current_counters
        ? std::optional{profiling::now()} : std::nullopt;
    // A6 immutability: keep the compiled shape read-only. Only the mutable
    // glyph state is written into worker-owned scratch; its capacity survives
    // across invocations to avoid per-item heap reallocations.
    auto& view = frame.render_shape;
    view.layout = source.layout;
    view.dissolve_layout = source.dissolve_layout;
    view.material = source.material;
    view.paint = source.paint;
    view.shadows = source.shadows;
    view.placement_kind = source.placement_kind;
    view.animators = source.animators;
    view.animated_doc = source.animated_doc;
    view.engine = source.engine;
    view.layout_spec = source.layout_spec;
    view.cache = source.cache;
    view.dissolve_mix = source.dissolve_mix;

    if (view.layout) reset_glyph_states_in_place(view.glyphs, view.layout->placed);
    else view.glyphs.clear();
    if (view.dissolve_layout) {
        reset_glyph_states_in_place(view.dissolve_glyphs, view.dissolve_layout->placed);
    } else {
        view.dissolve_glyphs.clear();
    }
    chronon3d::update_text_run_shape_per_frame(view, sample_time);

    // ABI element ids are cluster ids, not glyph ordinals. Apply the external
    // motion override after the Chronon-owned animator stack so authoring can
    // move stable clusters without replacing shaping or rasterization. A
    // cluster may contain multiple glyphs (ligatures/combining marks), and all
    // glyphs in that cluster receive the same override.
    if (sampled_layer && view.layout) {
        for (std::size_t glyph_index = 0; glyph_index < view.glyphs.size(); ++glyph_index) {
            auto& glyph = view.glyphs[glyph_index];
            if (glyph_index >= view.layout->placed.glyphs.size()) continue;
            // ABI ids reserve zero; cluster 0 is therefore transported as
            // element id 1, preserving a deterministic one-to-one mapping.
            const auto cluster_id = view.layout->placed.glyphs[glyph_index].cluster;
            const auto element = std::find_if(
                sampled_layer->elements.begin(), sampled_layer->elements.end(),
                [&](const auto& candidate) {
                    return candidate.id == static_cast<std::uint64_t>(cluster_id) + 1u;
                });
            if (element == sampled_layer->elements.end()) continue;
            glyph.position = element->position;
            glyph.rotation = element->rotation;
            glyph.scale = element->scale;
            glyph.opacity = element->opacity;
            glyph.blur = element->blur;
            if (!element->active) glyph.opacity = 0.0f;
        }
    }
    // The document is only the source of this frame's sampled layout/state.
    // Keep the sampled scratch shape eligible for the GPU lowering checks;
    // retaining the marker would make an otherwise lowerable frame look like
    // an unresolved animated document and force a false capability failure.
    view.animated_doc.reset();
    if (timing_start && profiling::g_current_counters) {
        profiling::g_current_counters->text_prepare_per_frame_wall_us.fetch_add(
            static_cast<std::uint64_t>(profiling::elapsed_us(*timing_start)),
            std::memory_order_relaxed);
    }
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
    float opacity,
    const compiler::SampledLayerParameters* sampled_layer
) {
    // A6 immutability: dynamic text is evaluated into worker-owned frame
    // scratch. A fully static run is already compiled and immutable, so it
    // bypasses the adapter entirely; this removes the per-frame shape copy,
    // glyph reset and driver call from the common typewriter/export path.
    static thread_local TextRunFrameState frame;
    const TextRunShape* render_shape = &source_shape;
    // Evaluate animator stacks once through Chronon's canonical evaluator.
    // The GPU route consumes those resolved per-glyph states, so selector
    // semantics and property blending stay identical to the software route.
    sampled_layer = sampled_layer ? sampled_layer : ctx.node_exec.sampled_layer;
    const bool lowerable_gpu_animation =
        !source_shape.animators.empty() && source_shape.layout &&
        can_lower_gpu_text_animation(source_shape);
    const bool has_element_overrides = sampled_layer && !sampled_layer->elements.empty();
    if (!source_shape.animators.empty() ||
        source_shape.animated_doc || has_element_overrides) {
        prepare_per_frame_shape(source_shape, ctx.frame_input.sample_time, frame,
                                sampled_layer);
        render_shape = &frame.render_shape;
    }
    const auto& local_shape = *render_shape;

    // ── Per-frame fail-closed invariants ──────────────────────────────
    // An internally inconsistent shape (missing layout, or per-glyph
    // animator states that do not line up with the shaped/placed run)
    // must surface as an explicit error instead of a raw out-of-bounds
    // access in the GPU or software renderers below.
    static const bool text_debug_env =
        std::getenv("CHRONON3D_TEXT_DEBUG") != nullptr;
    const bool text_debug = ctx.policy.diagnostics_enabled || text_debug_env;
    const auto shaped_glyph_count = local_shape.layout
        ? local_shape.layout->placed.glyphs.size() : std::size_t{0};
    const auto drawable_glyph_count = local_shape.layout
        ? std::count_if(
              local_shape.glyphs.begin(), local_shape.glyphs.end(),
              [&local_shape](const auto& glyph) {
                  const auto index = static_cast<std::size_t>(
                      &glyph - local_shape.glyphs.data());
                  // A selector is allowed to hide every glyph during an
                  // intentional reveal frame. Only count drawable glyphs
                  // that the evaluated shape expects to contribute.
                  return glyph.glyph_id != 0 && glyph.opacity > 1e-5f &&
                      index < local_shape.layout->placed.glyphs.size() &&
                      local_shape.layout->placed.glyphs[index].bbox_x1 >
                          local_shape.layout->placed.glyphs[index].bbox_x0;
              })
        : std::size_t{0};
    const auto zero_visible_error = [&]() {
        if (profiling::g_current_counters) {
            profiling::g_current_counters->text_run_zero_visible_frames.fetch_add(
                1, std::memory_order_relaxed);
        }
        spdlog::error(
            "[text-run] frame={} FAIL_CLOSED zero_visible_glyphs drawable_glyphs={}",
            ctx.frame_input.frame.integral(), drawable_glyph_count);
        return graph::RenderOpResult(graph::make_render_backend_error(
            RenderBackendErrorCode::ExecutionFailure,
            "render_text_run_item: non-empty TextRun produced zero visible glyphs"));
    };
    if (text_debug) {
        spdlog::info(
            "[text-frame] frame={} phase=invariants shape_glyphs={} "
            "placed_glyphs={} unit_glyphs={} font='{}' font_size={}",
            ctx.frame_input.frame.integral(),
            local_shape.glyphs.size(),
            shaped_glyph_count,
            local_shape.layout
                ? local_shape.layout->units.glyph_to_word.size() : 0u,
            local_shape.layout ? local_shape.layout->font.font_path : "",
            local_shape.layout ? local_shape.layout->font_size : 0.0f);
        // TEMP-DIAGNOSTIC: per-glyph resolved opacity/fill alpha.
        {
            std::string ops;
            for (const auto& g : local_shape.glyphs)
                ops += std::to_string(g.opacity) + "/"
                     + std::to_string(g.fill.a) + " ";
            spdlog::info("[text-diag] frame={} glyph_op/fill_a: {}",
                ctx.frame_input.frame.integral(), ops);
        }
    }
    if (!local_shape.layout) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            RenderBackendErrorCode::InvalidInput,
            "render_text_run_item: missing layout"));
    }
    if (local_shape.glyphs.size() != shaped_glyph_count) {
        return graph::RenderOpResult(graph::make_render_backend_error(
            RenderBackendErrorCode::InvalidInput,
            "render_text_run_item: shape/layout glyph-count mismatch"));
    }

    // Single matrix authority: identical input to predicted_bbox().
    // The tight-surface local basis (T(-surface_origin)) is owned by
    // build_world_matrix itself so rasterization and bbox sampling can
    // never diverge.
    glm::mat4 world_matrix = build_world_matrix(ctx, placement);
    auto& mutable_ctx = const_cast<RenderGraphContext&>(ctx);
    const auto frame_number = ctx.frame_input.frame.integral();
    if (text_debug) {
        spdlog::info(
            "[text-debug] frame={} phase=gpu.begin", frame_number);
    }
    auto native = draw_cached_text_run(
        mutable_ctx, fb, local_shape, world_matrix, opacity);
    if (text_debug) {
        spdlog::info(
            "[text-debug] frame={} phase=gpu.end ok={} err='{}'",
            frame_number, native.ok(),
            native.ok() ? "" : native.error().message);
    }
    if (native.ok()) {
        if (drawable_glyph_count > 0 && native.value().items_drawn == 0) {
            return zero_visible_error();
        }
        if (profiling::g_current_counters) {
            profiling::g_current_counters->gpu_text_fast_path_success.fetch_add(
                1, std::memory_order_relaxed);
        }
        return native;
    }
    if (profiling::g_current_counters) {
        profiling::g_current_counters->gpu_text_fallback_count.fetch_add(
            1, std::memory_order_relaxed);
    }
    if (native.error().message.find("native_surfaces_unavailable") !=
        std::string::npos) {
        spdlog::debug(
            "[text-run] native surface path unavailable; using software text: {}",
            native.error().message);
    } else {
        spdlog::warn(
            "[text-run] native surface path fell back: {}",
            native.error().message);
    }
    if (text_debug) {
        spdlog::debug(
            "[text-run] native surface path fell back: {}",
            native.error().message);
    }
    if (ctx.policy.is_gpu_native_required()) {
        // REQUIRE_GPU_NATIVE is a correctness contract.  Returning the
        // native-path error is preferable to silently rendering text through
        // the software backend and invalidating the zero-readback/GPU-only
        // receipt for the whole export.
        return native;
    }
    // The GPU atlas path is an optimization, not a correctness boundary.
    // Stale atlas/surface state must fall back to Chronon's canonical text
    // renderer instead of aborting the complete video frame.  The backend
    // still owns the final native surface and the caller keeps the failure
    // visible in its telemetry/logs.
    if (text_debug) {
        spdlog::info(
            "[text-debug] frame={} phase=software.begin", frame_number);
    }
    // A lowerable selector is normally sampled by the native MTSDF shader,
    // but the software/compatibility fallback still needs the same per-glyph
    // state.  Prepare it only after native rendering has declined, so the
    // GPU path keeps its compact run-level contract without paying the CPU
    // glyph walk on successful frames.
    const TextRunShape* software_shape = render_shape;
    if (lowerable_gpu_animation && render_shape == &source_shape) {
        prepare_per_frame_shape(source_shape, ctx.frame_input.sample_time, frame,
                                sampled_layer);
        software_shape = &frame.render_shape;
    }
    auto fallback = backend.draw_text_run(fb, *software_shape, world_matrix, opacity);
    if (text_debug) {
        spdlog::info(
            "[text-debug] frame={} phase=software.end ok={}",
            frame_number, fallback.ok());
    }
    if (fallback.ok() && fb.surface_handle() == runtime::kInvalidRenderSurfaceHandle &&
        backend.supports_native_surfaces()) {
        // The fallback framebuffer is transparent outside its actual ink.
        // Promote only that region whenever the backend reports it.  This
        // avoids 1280x720/1080p RGBA32F full-surface uploads for small text
        // overlays while retaining ensure_native_surface() as the exact
        // historical correctness fallback when the ROI path is unavailable.
        if (text_debug) {
            spdlog::info(
                "[text-debug] frame={} phase=promotion.begin", frame_number);
        }
        const bool promoted = promote_text_fallback_region(
            mutable_ctx, backend, fb, fallback.value().actual_ink_bbox);
        if (text_debug) {
            spdlog::info(
                "[text-debug] frame={} phase=promotion.end ok={}",
                frame_number, promoted);
        }
        if (!promoted &&
            !ensure_native_surface(mutable_ctx, fb, "TextRun.execution.fallback")) {
            return graph::RenderOpResult(graph::make_render_backend_error(
                RenderBackendErrorCode::ExecutionFailure,
                "TextRun fallback rendered but could not be uploaded to the native surface"));
        }
    }
    if (fallback.ok() && drawable_glyph_count > 0 &&
        fallback.value().items_drawn == 0) {
        return zero_visible_error();
    }
    return fallback;
}

#endif // CHRONON3D_ENABLE_TEXT

}  // namespace chronon3d::graph::text_run
