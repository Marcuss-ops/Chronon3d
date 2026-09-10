// node_runner_fused_batch.cpp — fused layer-batch execution (P1.5).
//
// Implementation extracted from node_runner_fused_batch_detail.hpp so the
// executor's hot TU stops dragging the text/transform/source node headers
// and their include webs through a detail fragment.

#include "node_runner_fused_batch_detail.hpp"

#include "../nodes/native_surface.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/media/media_placement.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/backends/text/mtsdf_generator.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <array>
#include <vector>

#if defined(CHRONON3D_ENABLE_TEXT)
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include "../nodes/text_run/text_run_execution.hpp"
#include <chronon3d/text/glyph_atlas.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#endif

namespace chronon3d::graph::detail {
namespace {

// HOT-PATH TAX (TICKET-HOTPATH-ZERO-ALLOC-P1) — fused-batch scratch.
// execute_fused_batch_impl runs once per fused batch per frame and previously
// rebuilt std::vector<GlyphStatic> + std::vector<TextRunDynamic> +
// std::vector<RenderSurfaceHandle> + std::unordered_map (text path) and an
// unordered_map (image path) from scratch every call. The containers are
// thread_local so parallel level execution never shares state (same pattern
// as the plan dispatcher's thread-local instance storage); clear() keeps
// steady-state frames pay zero allocations.
struct FusedWorkerScratch {
    struct PreparedTextRun {
        runtime::RuntimeContext* runtime{nullptr};
        const TextRunShape* shape{nullptr};
        bool ready{false};
        std::vector<runtime::GlyphStatic> glyphs;
        std::vector<runtime::RenderSurfaceHandle> atlas_pages;
    };

    std::vector<runtime::GlyphStatic> glyphs;
    std::vector<runtime::TextRunDynamic> runs;
    std::vector<runtime::RenderSurfaceHandle> atlas_pages;
#if defined(CHRONON3D_ENABLE_TEXT)
    TextRunFrameState text_frame;
    std::vector<PreparedTextRun> prepared_text_runs;
#endif
    std::vector<runtime::RenderSurfaceHandle> resources;
    std::vector<Vec2> resource_sizes;
    std::vector<runtime::LayerInstance> layer_instances;
    // Dense resource ids are bounded by the compiled batch's node domain.
    // Generation tags avoid clearing a potentially large table per batch.
    std::vector<std::uint32_t> resource_slots;
    std::vector<std::uint32_t> resource_generation;
    std::uint32_t generation{0};

    void begin_batch(std::size_t max_resource_id) {
        // AUDIT-18: max_resource_id is a capacity hint, not a trusted bound.
        // The compiler enforces dense resource_index < instances.size(), but
        // the executor must fail closed on a sparse/pathological id instead
        // of indexing past the table (unbounded pinned growth per worker).
        if (max_resource_id > kMaxResourceId)
            max_resource_id = kMaxResourceId;
        ++generation;
        if (generation == 0) {
            std::fill(resource_generation.begin(), resource_generation.end(), 0);
            generation = 1;
        }
        if (resource_slots.size() < max_resource_id) resource_slots.resize(max_resource_id);
        if (resource_generation.size() < max_resource_id) resource_generation.resize(max_resource_id, 0);
        glyphs.clear();
        runs.clear();
        atlas_pages.clear();
        resources.clear();
        resource_sizes.clear();
        layer_instances.clear();
    }

    /// AUDIT-18: hard ceiling on the scratch resource table. A compiled id
    /// at or beyond this value is rejected by the caller (fail closed), not
    /// silently served from an ever-growing thread-local allocation.
    static constexpr std::size_t kMaxResourceId = 1u << 20;
};

thread_local FusedWorkerScratch tls_fused_scratch;

#if defined(CHRONON3D_ENABLE_TEXT)

[[nodiscard]] bool can_use_mtsdf_gpu_text(const TextRunShape& shape) noexcept {
    if (!shape.layout || shape.layout->font_spans.size() > 1 ||
        !shape.animators.empty() || shape.animated_doc ||
        shape.paint.fill_style || shape.paint.stroke_style ||
        shape.material.enabled || shape.shadows.size() > 1) {
        return false;
    }
    if (shape.paint.stroke_enabled &&
        (shape.paint.stroke_width < 0.0f || shape.paint.stroke_width > 8.0f)) {
        return false;
    }
    for (const auto& shadow : shape.shadows) {
        if (!shadow.enabled) continue;
        if (shadow.blur < 0.0f || shadow.blur > 8.0f ||
            std::abs(shadow.offset.x) + shadow.blur > 9.0f ||
            std::abs(shadow.offset.y) + shadow.blur > 9.0f) {
            return false;
        }
    }
    return shape.glyphs.size() == shape.layout->placed.glyphs.size();
}

[[nodiscard]] std::uint32_t pack_text_color(const Color& color) noexcept {
    const auto to_byte = [](float value) {
        return static_cast<std::uint32_t>(std::clamp(
            std::lround(value * 255.0f), 0L, 255L));
    };
    return to_byte(color.r) |
           (to_byte(color.g) << 8) |
           (to_byte(color.b) << 16) |
           (to_byte(color.a) << 24);
}

void fill_mtsdf_effects(const TextRunShape& shape,
                        runtime::TextRunDynamic& run) noexcept {
    constexpr std::uint32_t kStroke = 1u << 0;
    constexpr std::uint32_t kShadow = 1u << 1;
    constexpr std::uint32_t kGlow = 1u << 2;

    if (shape.paint.stroke_enabled && shape.paint.stroke_width > 0.0f) {
        run.stroke_width = shape.paint.stroke_width;
        run.stroke_color = pack_text_color(shape.paint.stroke_color);
        run.effect_flags |= kStroke;
    }

    for (const auto& shadow : shape.shadows) {
        if (!shadow.enabled) continue;
        Color shadow_color = shadow.color;
        shadow_color.a = std::clamp(shadow_color.a * shadow.opacity, 0.0f, 1.0f);
        run.shadow_color = pack_text_color(shadow_color);
        run.shadow_dx = shadow.offset.x;
        run.shadow_dy = shadow.offset.y;
        run.shadow_blur = shadow.blur;
        run.effect_flags |= (std::abs(shadow.offset.x) < 1e-4f &&
                             std::abs(shadow.offset.y) < 1e-4f)
            ? kGlow : kShadow;
        break;
    }
}

FusedWorkerScratch::PreparedTextRun* find_prepared_text_run(
    FusedWorkerScratch& scratch, runtime::RuntimeContext& runtime,
    TextRenderResources* text_resources, assets::AssetResolver* resolver,
    const TextRunShape& shape) {
    for (auto& prepared : scratch.prepared_text_runs) {
        if (prepared.runtime == &runtime && prepared.shape == &shape) {
            return &prepared;
        }
    }

    FusedWorkerScratch::PreparedTextRun prepared;
    prepared.runtime = &runtime;
    prepared.shape = &shape;
    if (!can_use_mtsdf_gpu_text(shape) || !shape.layout) {
        scratch.prepared_text_runs.push_back(std::move(prepared));
        return &scratch.prepared_text_runs.back();
    }

    const auto& layout_glyphs = shape.layout->placed.glyphs;
    constexpr std::uint16_t kMtsdfEmPx = 64;
    constexpr std::uint16_t kMtsdfDistanceRangePx = 8;
    constexpr std::uint16_t kMtsdfPaddingPx = 10;
    constexpr std::uint16_t kMtsdfProfileId = 1;
    const float font_size = std::max(1.0f, shape.layout->font_size);
    const auto font_path = shape.layout->font.font_path;
    const runtime::DistanceFieldProfile profile{
        runtime::GlyphRepresentation::Mtsdf, kMtsdfEmPx,
        kMtsdfDistanceRangePx, kMtsdfPaddingPx};

    // The first implementation intentionally fails closed for mixed-font
    // runs. The layout still carries spans, but the fused dispatch currently
    // binds one atlas page and one font preparation handle per run.
    if (!resolver || shape.layout->font_spans.size() > 1) {
        scratch.prepared_text_runs.push_back(std::move(prepared));
        return &scratch.prepared_text_runs.back();
    }
    FontFaceHandle font_handle;
    try {
        font_handle = text_resources->resolve_handle(
            font_path, static_cast<float>(kMtsdfEmPx), *resolver);
    } catch (...) {
        scratch.prepared_text_runs.push_back(std::move(prepared));
        return &scratch.prepared_text_runs.back();
    }
    if (!font_handle.ft_face) {
        scratch.prepared_text_runs.push_back(std::move(prepared));
        return &scratch.prepared_text_runs.back();
    }

    std::vector<runtime::GpuGlyphKey> keys;
    std::vector<std::uint32_t> widths;
    std::vector<std::uint32_t> heights;
    std::vector<runtime::GpuGlyphMetrics> metrics;
    std::vector<runtime::GpuGlyphKey> pending_keys;
    std::vector<std::uint32_t> pending_widths;
    std::vector<std::uint32_t> pending_heights;
    std::vector<runtime::GpuGlyphMetrics> pending_metrics;
    std::vector<float> pending_pixels;
    keys.reserve(layout_glyphs.size());
    widths.reserve(layout_glyphs.size());
    heights.reserve(layout_glyphs.size());
    metrics.reserve(layout_glyphs.size());
    pending_keys.reserve(layout_glyphs.size());
    pending_widths.reserve(layout_glyphs.size());
    pending_heights.reserve(layout_glyphs.size());
    pending_metrics.reserve(layout_glyphs.size());

    for (std::size_t gi = 0; gi < layout_glyphs.size(); ++gi) {
        const auto& state = shape.glyphs[gi];
        const auto& placed = layout_glyphs[gi];
        if (state.glyph_id == 0 || placed.bbox_x1 <= placed.bbox_x0 ||
            !text_resources) {
            scratch.prepared_text_runs.push_back(std::move(prepared));
            return &scratch.prepared_text_runs.back();
        }
        const auto key = runtime::GpuGlyphKey::make(
            font_path, state.glyph_id, 0,
            runtime::GlyphRepresentation::Mtsdf, kMtsdfProfileId, 0);
        keys.push_back(key);

        if (const auto resident = runtime.gpu_glyph_atlas().location(key)) {
            widths.push_back(resident->width);
            heights.push_back(resident->height);
            metrics.push_back(runtime.gpu_glyph_atlas().metrics(key).value_or(
                runtime::GpuGlyphMetrics{0, 0, placed.advance_x}));
            continue;
        }

        const auto generated = generate_mtsdf_glyph(
            font_handle.ft_face, state.glyph_id, profile.em_px,
            profile.distance_range_px, profile.padding_px);
        if (!generated || generated->rgba8.empty()) {
            scratch.prepared_text_runs.push_back(std::move(prepared));
            return &scratch.prepared_text_runs.back();
        }
        const runtime::GpuGlyphMetrics glyph_metrics{
            generated->x_offset, generated->y_offset, generated->advance_x};
        widths.push_back(generated->width);
        heights.push_back(generated->height);
        metrics.push_back(glyph_metrics);
        pending_keys.push_back(key);
        pending_widths.push_back(generated->width);
        pending_heights.push_back(generated->height);
        pending_metrics.push_back(glyph_metrics);
        pending_pixels.insert(pending_pixels.end(), generated->rgba8.begin(),
                              generated->rgba8.end());
    }

    if (!pending_keys.empty()) {
        const auto uploaded = runtime.gpu_glyph_atlas().prepare_batch(
            pending_keys, pending_widths, pending_heights, pending_pixels,
            pending_metrics, profile);
        if (!uploaded.has_value()) {
            scratch.prepared_text_runs.push_back(std::move(prepared));
            return &scratch.prepared_text_runs.back();
        }
    }

    std::vector<std::uint16_t> page_ids;
    page_ids.reserve(1);
    prepared.glyphs.reserve(layout_glyphs.size());
    for (std::size_t gi = 0; gi < layout_glyphs.size(); ++gi) {
        const auto location = runtime.gpu_glyph_atlas().acquire(
            keys[gi], widths[gi], heights[gi], {}, metrics[gi], profile);
        if (!location.has_value()) {
            prepared.glyphs.clear();
            scratch.prepared_text_runs.push_back(std::move(prepared));
            return &scratch.prepared_text_runs.back();
        }
        const auto loc = location.value();
        auto page_it = std::find(page_ids.begin(), page_ids.end(), loc.atlas_page);
        if (page_it == page_ids.end()) {
            page_ids.push_back(loc.atlas_page);
            page_it = std::prev(page_ids.end());
        }
        if (page_ids.size() != 1) {
            // The backend currently binds one atlas image per dispatch. Do
            // not silently render a multi-page run with the wrong page.
            prepared.glyphs.clear();
            scratch.prepared_text_runs.push_back(std::move(prepared));
            return &scratch.prepared_text_runs.back();
        }
        const auto page = runtime.gpu_glyph_atlas().page_handle(loc.atlas_page);
        if (!page) {
            prepared.glyphs.clear();
            scratch.prepared_text_runs.push_back(std::move(prepared));
            return &scratch.prepared_text_runs.back();
        }
        if (prepared.atlas_pages.empty()) prepared.atlas_pages.push_back(*page);

        const auto& placed = layout_glyphs[gi];
        runtime::GlyphStatic glyph{};
        glyph.run_index = 0;
        glyph.atlas_page = 0;
        glyph.flags = 0;
        glyph.atlas_x = loc.atlas_x;
        glyph.atlas_y = loc.atlas_y;
        glyph.atlas_w = loc.width;
        glyph.atlas_h = loc.height;
        const float scale = font_size / static_cast<float>(profile.em_px);
        const float pad = static_cast<float>(profile.padding_px) * scale;
        glyph.plane_left = placed.x + placed.bbox_x0 * scale - pad;
        glyph.plane_top = placed.y - placed.bbox_y0 * scale - pad;
        glyph.plane_right = glyph.plane_left +
            static_cast<float>(loc.width) * scale;
        glyph.plane_bottom = glyph.plane_top +
            static_cast<float>(loc.height) * scale;
        glyph.flags = 1u; // MTSDF atlas sample
        glyph.draw_order = static_cast<std::uint32_t>(gi);
        glyph.pad0 = static_cast<std::uint32_t>(gi);
        prepared.glyphs.push_back(glyph);
    }
    prepared.ready = !prepared.glyphs.empty() && prepared.atlas_pages.size() == 1;
    scratch.prepared_text_runs.push_back(std::move(prepared));
    return &scratch.prepared_text_runs.back();
}

#endif

void report_fused_batch_error(
    RenderGraphContext& ctx,
    RenderBackendErrorCode code,
    std::string message) {
    if (ctx.frame_error && !ctx.frame_error->has_value()) {
        *ctx.frame_error = NodeExecutionError{
            code, "fused-layer-batch", std::move(message)};
    }
}

void execute_fused_batch_impl(
    ExecutionState& state,
    RenderGraph& graph,
    RenderGraphContext& ctx,
    const CompiledLayerBatch& batch,
    GraphNodeId id,
    FramebufferPool* /*parent_pool*/,
    RenderCounters* parent_counters,
    const CompiledFrameGraph& compiled,
    std::uint64_t* local_nodes_executed) {
    if (!ctx.execution_backend() || batch.instances.empty()) return;

    bool is_text_batch = false;
    for (const auto& inst : batch.instances) {
        if (inst.node < compiled.nodes.size() && compiled.nodes[inst.node].kind == RenderGraphNodeKind::TextRun) {
            is_text_batch = true;
            break;
        }
    }

    if (is_text_batch) {
#if defined(CHRONON3D_ENABLE_TEXT)
        auto& scratch = tls_fused_scratch;
        auto& all_glyphs = scratch.glyphs;
        auto& all_runs = scratch.runs;
        auto& atlas_pages = scratch.atlas_pages;
        scratch.begin_batch(batch.instances.size());
        all_glyphs.clear();
        all_runs.clear();
        atlas_pages.clear();
        for (const auto& inst : batch.instances) {
            if (inst.node >= graph.size() || !graph.has_node(inst.node)) continue;
            const auto& node = graph.node(inst.node);
            if (node.kind() != RenderGraphNodeKind::TextRun) continue;
            const auto& tr_node = static_cast<const TextRunNode&>(node);
            if (!tr_node.shape()) continue;

            float tx = 0.0f, ty = 0.0f, sx = 1.0f, sy = 1.0f;
            float opacity = inst.opacity;
            if (inst.transform_index != 0 && inst.transform_index < graph.size()) {
                const auto& xform = static_cast<const TransformNode&>(graph.node(inst.transform_index));
                const auto m = xform.matrix();
                tx = m[3][0];
                ty = m[3][1];
                sx = m[0][0];
                sy = m[1][1];
                opacity *= xform.opacity();
            }

            // Static runs are already compiled at scene preparation time and
            // must not be copied/reset/evaluated once per frame. Dynamic runs
            // retain the worker-local adapter until PreparedTextRun replaces
            // this compatibility branch.
            const TextRunShape* render_shape = tr_node.shape().get();
            if (!render_shape->animators.empty() || render_shape->animated_doc) {
                text_run::prepare_per_frame_shape(
                    *render_shape, ctx.frame_input.sample_time, scratch.text_frame);
                render_shape = &scratch.text_frame.render_shape;
            }
            if (!render_shape->layout) continue;

            const auto& layout = *render_shape->layout;

            // Prepared path: FreeType outlines are promoted to the
            // persistent GpuGlyphAtlas once per (runtime, shape, layout).
            // Steady-state frames only append already-compiled metadata and
            // update the small dynamic run record.
            if (ctx.runtime && can_use_mtsdf_gpu_text(*render_shape)) {
                auto* prepared = find_prepared_text_run(
                    scratch, *ctx.runtime, ctx.text_render_resources,
                    ctx.asset_resolver, *render_shape);
                if (prepared->ready && prepared->atlas_pages.size() == 1) {
                    const auto page = prepared->atlas_pages.front();
                    if (atlas_pages.empty() || atlas_pages.front() == page) {
                        if (atlas_pages.empty()) atlas_pages.push_back(page);
                        runtime::TextRunDynamic run_dyn{};
                        run_dyn.tx = tx;
                        run_dyn.ty = ty;
                        run_dyn.sx = sx;
                        run_dyn.sy = sy;
                        run_dyn.opacity = opacity;
                        run_dyn.color = pack_text_color(render_shape->paint.fill);
                        run_dyn.visible_glyph_count =
                            std::numeric_limits<std::uint32_t>::max();
                        run_dyn.flags = 0u;
                        fill_mtsdf_effects(*render_shape, run_dyn);
                        const auto run_index = static_cast<std::uint32_t>(all_runs.size());
                        all_runs.push_back(run_dyn);
                        const auto first = all_glyphs.size();
                        all_glyphs.insert(all_glyphs.end(),
                                          prepared->glyphs.begin(),
                                          prepared->glyphs.end());
                        for (std::size_t i = first; i < all_glyphs.size(); ++i) {
                            all_glyphs[i].run_index = run_index;
                        }
                        continue;
                    }
                }
            }

            const int font_size = std::max(1, static_cast<int>(std::lround(layout.font_size)));
            const auto font_id = glyph_font_id(layout.font.font_path);

            runtime::TextRunDynamic run_dyn{};
            run_dyn.tx = tx;
            run_dyn.ty = ty;
            run_dyn.sx = sx;
            run_dyn.sy = sy;
            run_dyn.opacity = opacity;
            run_dyn.color = 0xFFFFFFFF;
            run_dyn.visible_glyph_count =
                std::numeric_limits<std::uint32_t>::max();
            all_runs.push_back(run_dyn);

            // The current fused text backend exposes one atlas page per
            // dispatch. Resolve that page once for the run; looking it up in
            // `atlas_pages` for every glyph was both O(n²) and ineffective
            // while the old page handle was constant.
            constexpr runtime::RenderSurfaceHandle kFusedTextAtlasPage = 1;
            constexpr std::uint32_t run_page_idx = 0;
            if (atlas_pages.empty()) atlas_pages.push_back(kFusedTextAtlasPage);

            for (std::size_t gi = 0; gi < layout.placed.glyphs.size(); ++gi) {
                const auto& gstate = render_shape->glyphs[gi];
                if (gstate.glyph_id == 0) continue;
                const auto& placed = layout.placed.glyphs[gi];
                if (placed.bbox_x1 <= placed.bbox_x0) continue;

                if (!ctx.text_render_resources) continue;
                auto entry = ctx.text_render_resources->lookup_glyph_atlas(
                    font_id, gstate.glyph_id, static_cast<u32>(font_size));
                if (!entry || !entry->image) continue;

                runtime::GlyphStatic g_stat{};
                g_stat.run_index = static_cast<uint32_t>(all_runs.size() - 1);
                g_stat.atlas_page = static_cast<uint16_t>(run_page_idx);
                g_stat.flags = 0;
                g_stat.atlas_x = static_cast<uint16_t>(entry->x_offset >= 0 ? entry->x_offset : 0);
                g_stat.atlas_y = static_cast<uint16_t>(entry->y_offset >= 0 ? entry->y_offset : 0);
                g_stat.atlas_w = static_cast<uint16_t>(entry->image->width());
                g_stat.atlas_h = static_cast<uint16_t>(entry->image->height());
                g_stat.plane_left = static_cast<float>(placed.x);
                g_stat.plane_top = static_cast<float>(placed.y);
                g_stat.plane_right = static_cast<float>(placed.x + entry->image->width());
                g_stat.plane_bottom = static_cast<float>(placed.y + entry->image->height());
                g_stat.draw_order = static_cast<uint32_t>(all_glyphs.size());
                g_stat.pad0 = static_cast<uint32_t>(gi);
                all_glyphs.push_back(g_stat);
            }
        }

        auto dest_fb = ctx.acquire_framebuffer(ctx.frame_input.width, ctx.frame_input.height, false);
        if (!ensure_empty_native_surface(ctx, *dest_fb)) {
            report_fused_batch_error(
                ctx, RenderBackendErrorCode::ExecutionFailure,
                "failed to materialize fused text batch destination");
            return;
        }
        dest_fb->set_opaque(false);
        const auto dispatch = ctx.execution_backend()->draw_text_batch(
            dest_fb->surface_handle(), all_glyphs, all_runs, atlas_pages);
        if (!dispatch.ok()) {
            report_fused_batch_error(
                ctx, dispatch.error().code,
                "fused text batch dispatch failed: " + dispatch.error().message);
            return;
        }

        state.temp[id] = dest_fb;
        state.resolved_bboxes[id] = raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
        if (local_nodes_executed) {
            ++(*local_nodes_executed);
        } else if (parent_counters) {
            parent_counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
        }
#endif
        return;
    }

    auto& scratch = tls_fused_scratch;
    scratch.begin_batch(batch.instances.size());
    runtime::GpuLayerBatch gpu_batch;
    gpu_batch.output_physical_slot = batch.output_physical_slot;
    gpu_batch.is_gpu_fused = batch.is_gpu_fused;
    auto& instances = scratch.layer_instances;
    auto& resources = scratch.resources;
    // Use the compiler-provided resource index as the dense key. The fused
    // materializer only deduplicates resolved handles, never hashes them.
    std::optional<raster::BBox> image_bbox;

    for (const auto& inst : batch.instances) {
        if (inst.node >= graph.size() || !graph.has_node(inst.node)) {
            report_fused_batch_error(
                ctx, RenderBackendErrorCode::InvalidInput,
                "image batch references an invalid graph node");
            return;
        }
        const auto& node = graph.node(inst.node);
        if (node.kind() != RenderGraphNodeKind::Source) {
            report_fused_batch_error(
                ctx, RenderBackendErrorCode::InvalidInput,
                "image batch contains a non-source node");
            return;
        }
        const auto& src_node = static_cast<const SourceNode&>(node);
        if (src_node.render_node().shape.type() != ShapeType::Image) {
            report_fused_batch_error(
                ctx, RenderBackendErrorCode::InvalidInput,
                "image batch contains a non-image source");
            return;
        }
        const auto& img = src_node.render_node().shape.image();
        if (img.path.empty()) {
            report_fused_batch_error(
                ctx, RenderBackendErrorCode::InvalidInput,
                "image batch source has an empty asset path");
            return;
        }
        if (!ctx.runtime) {
            report_fused_batch_error(
                ctx, RenderBackendErrorCode::InvalidInput,
                "image batch requires RuntimeContext");
            return;
        }

        const auto compiled_resource_id = inst.resource_index;
        // AUDIT-18: the compiled id indexes a worker-local scratch table.
        // A sparse/pathological id must fail closed here rather than resize
        // the thread-local table to it (GB of pinned memory per worker).
        if (compiled_resource_id >= FusedWorkerScratch::kMaxResourceId) {
            report_fused_batch_error(
                ctx, RenderBackendErrorCode::InvalidInput,
                "image batch compiled resource id exceeds the fused scratch bound");
            return;
        }
        if (compiled_resource_id >= scratch.resource_slots.size()) {
            report_fused_batch_error(
                ctx, RenderBackendErrorCode::InvalidInput,
                "image batch compiled resource index is out of range");
            return;
        }
        std::uint32_t res_idx = 0;
        Vec2 original_source_size{0.0f, 0.0f};
        if (scratch.resource_generation[compiled_resource_id] == scratch.generation) {
            res_idx = scratch.resource_slots[compiled_resource_id];
            original_source_size = scratch.resource_sizes[res_idx];
        } else {
            const auto cached = ctx.runtime->image_cache().find(img.path, img.decode_options);
            if (!cached || !cached->valid() || cached->gpu_rgba.empty() || !cached->fb_img) {
                report_fused_batch_error(
                    ctx, RenderBackendErrorCode::ExecutionFailure,
                    "image asset unavailable for GPU batch: " + img.path);
                return;
            }

            const auto& key = cached->gpu_key;
            const runtime::SurfaceDesc desc{
                key.width, key.height, key.format,
                runtime::ResourceUsage::Storage,
                runtime::LifetimeClass::JobPersistent,
                cached->gpu_rgba.size() * sizeof(float)};
            const auto acquired = ctx.runtime->gpu_asset_cache().acquire(key, desc, cached->gpu_rgba);
            if (!acquired.ok()) {
                report_fused_batch_error(
                    ctx, RenderBackendErrorCode::ExecutionFailure,
                    "unable to acquire GPU image surface '" + img.path +
                        "': " + acquired.error);
                return;
            }

            runtime::RenderSurfaceHandle handle = acquired.handle;
            auto it = std::find(resources.begin(), resources.end(), handle);
            if (it != resources.end()) {
                res_idx = static_cast<std::uint32_t>(std::distance(resources.begin(), it));
            } else {
                res_idx = static_cast<std::uint32_t>(resources.size());
                resources.push_back(handle);
                scratch.resource_sizes.push_back(Vec2{
                    static_cast<float>(cached->fb_img->width()),
                    static_cast<float>(cached->fb_img->height())});
            }
            scratch.resource_generation[compiled_resource_id] = scratch.generation;
            scratch.resource_slots[compiled_resource_id] = res_idx;
            original_source_size = scratch.resource_sizes[res_idx];
        }

        float tx = 0.0f, ty = 0.0f;
        float opacity = inst.opacity;
        if (inst.transform_index != 0 && inst.transform_index < graph.size()) {
            const auto& xform = static_cast<const TransformNode&>(graph.node(inst.transform_index));
            const auto m = xform.matrix();
            tx = m[3][0];
            ty = m[3][1];
            opacity *= xform.opacity();
        }

        const auto placement = compute_media_placement(original_source_size, img.size, img.fit, img.focal_point);

        const float world_x0 = tx - img.size.x * 0.5f + placement.dst_rect.origin.x + (ctx.frame_input.width * 0.5f);
        const float world_y0 = ty - img.size.y * 0.5f + placement.dst_rect.origin.y + (ctx.frame_input.height * 0.5f);
        const float world_x1 = world_x0 + placement.dst_rect.size.x;
        const float world_y1 = world_y0 + placement.dst_rect.size.y;

        runtime::LayerInstance gpu_inst{};
        gpu_inst.resource_index = res_idx;
        gpu_inst.dst_x0 = world_x0;
        gpu_inst.dst_y0 = world_y0;
        gpu_inst.dst_x1 = world_x1;
        gpu_inst.dst_y1 = world_y1;
        // layer_batch.comp samples src_rect as normalized coordinates (0..1),
        // while media placement is expressed in source pixels. Convert at
        // this boundary so the Vulkan fused image path samples the authored
        // image instead of clamping every lookup to an edge texel.
        const float source_width = original_source_size.x;
        const float source_height = original_source_size.y;
        gpu_inst.src_x0 = placement.src_rect.origin.x / source_width;
        gpu_inst.src_y0 = placement.src_rect.origin.y / source_height;
        gpu_inst.src_x1 = (placement.src_rect.origin.x + placement.src_rect.size.x) /
                          source_width;
        gpu_inst.src_y1 = (placement.src_rect.origin.y + placement.src_rect.size.y) /
                          source_height;
        gpu_inst.opacity = opacity;
        gpu_inst.blend = BlendMode::Normal;
        gpu_inst.kind = runtime::PrimitiveKind::Image;
        instances.push_back(gpu_inst);

        raster::BBox bbox{
            static_cast<i32>(std::floor(world_x0)),
            static_cast<i32>(std::floor(world_y0)),
            static_cast<i32>(std::ceil(world_x1)),
            static_cast<i32>(std::ceil(world_y1))};
        bbox.clip_to(ctx.frame_input.width, ctx.frame_input.height);
        if (!image_bbox) image_bbox = bbox;
        else {
            image_bbox->x0 = std::min(image_bbox->x0, bbox.x0);
            image_bbox->y0 = std::min(image_bbox->y0, bbox.y0);
            image_bbox->x1 = std::max(image_bbox->x1, bbox.x1);
            image_bbox->y1 = std::max(image_bbox->y1, bbox.y1);
        }
    }

    if (instances.empty()) {
        report_fused_batch_error(
            ctx, RenderBackendErrorCode::ExecutionFailure,
            "image batch materialized zero GPU instances");
        return;
    }

    auto dest_fb = ctx.acquire_framebuffer(ctx.frame_input.width, ctx.frame_input.height, false);
    if (!ensure_empty_native_surface(ctx, *dest_fb)) {
        report_fused_batch_error(
            ctx, RenderBackendErrorCode::ExecutionFailure,
            "failed to materialize fused image batch destination");
        return;
    }
    dest_fb->set_opaque(false);
    gpu_batch.instances.swap(instances);
    gpu_batch.resources.swap(resources);
    const auto dispatch = ctx.execution_backend()->execute_layer_batch(
        dest_fb->surface_handle(), gpu_batch, gpu_batch.resources, {}, {});
    gpu_batch.resources.swap(resources);
    gpu_batch.instances.swap(instances);
    if (!dispatch.ok()) {
        report_fused_batch_error(
            ctx, dispatch.error().code,
            "fused image batch dispatch failed: " + dispatch.error().message);
        return;
    }
    dest_fb->mark_gpu_authoritative();

    state.temp[id] = dest_fb;
    state.resolved_bboxes[id] = image_bbox;
    if (local_nodes_executed) {
        ++(*local_nodes_executed);
    } else if (parent_counters) {
        parent_counters->nodes_executed.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace

void execute_fused_batch(
    ExecutionState& state,
    RenderGraph& graph,
    RenderGraphContext& ctx,
    const CompiledLayerBatch& batch,
    GraphNodeId id,
    FramebufferPool* parent_pool,
    RenderCounters* parent_counters,
    const CompiledFrameGraph& compiled,
    std::uint64_t* local_nodes_executed) {
    execute_fused_batch_impl(state, graph, ctx, batch, id, parent_pool,
                             parent_counters, compiled, local_nodes_executed);
}

} // namespace chronon3d::graph::detail
