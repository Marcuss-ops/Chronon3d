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
#include <chronon3d/backends/text/text_render_resources.hpp>

#include <cmath>
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
    std::vector<runtime::GlyphStatic> glyphs;
    std::vector<runtime::TextRunDynamic> runs;
    std::vector<runtime::RenderSurfaceHandle> atlas_pages;
#if defined(CHRONON3D_ENABLE_TEXT)
    TextRunFrameState text_frame;
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
};

thread_local FusedWorkerScratch tls_fused_scratch;

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

            text_run::prepare_per_frame_shape(
                *tr_node.shape(), ctx.frame_input.sample_time, scratch.text_frame);
            if (!scratch.text_frame.render_shape.layout) continue;

            const auto& layout = *scratch.text_frame.render_shape.layout;
            const int font_size = std::max(1, static_cast<int>(std::lround(layout.font_size)));
            const auto font_id = glyph_font_id(layout.font.font_path);

            runtime::TextRunDynamic run_dyn{};
            run_dyn.tx = tx;
            run_dyn.ty = ty;
            run_dyn.sx = sx;
            run_dyn.sy = sy;
            run_dyn.opacity = opacity;
            run_dyn.color = 0xFFFFFFFF;
            all_runs.push_back(run_dyn);

            // The current fused text backend exposes one atlas page per
            // dispatch. Resolve that page once for the run; looking it up in
            // `atlas_pages` for every glyph was both O(n²) and ineffective
            // while the old page handle was constant.
            constexpr runtime::RenderSurfaceHandle kFusedTextAtlasPage = 1;
            constexpr std::uint32_t run_page_idx = 0;
            if (atlas_pages.empty()) atlas_pages.push_back(kFusedTextAtlasPage);

            for (std::size_t gi = 0; gi < layout.placed.glyphs.size(); ++gi) {
                const auto& gstate = scratch.text_frame.render_shape.glyphs[gi];
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
