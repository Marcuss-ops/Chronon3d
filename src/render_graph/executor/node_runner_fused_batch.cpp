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
#include <chronon3d/compiler/frame_parameters.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/backends/text/mtsdf_generator.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>

#include <cmath>
#include <bit>
#include <limits>
#include <optional>
#include <string>
#include <array>
#include <span>
#include <unordered_map>
#include <vector>

#if defined(CHRONON3D_ENABLE_TEXT)
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include "../nodes/text_run/text_run_execution.hpp"
#include "../nodes/text_run/gpu_text_run.hpp"
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
    struct PreparedTextBatch {
        std::vector<runtime::GlyphStatic> glyphs;
        std::vector<runtime::RenderSurfaceHandle> atlas_pages;
    };

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
    std::unordered_map<const CompiledLayerBatch*, PreparedTextBatch>
        prepared_text_batches;
    GraphInstanceId prepared_graph_owner{kInvalidGraphInstanceId};
    std::uint32_t generation{0};

    void begin_batch(std::size_t max_resource_id,
                     GraphInstanceId graph_owner = kInvalidGraphInstanceId) {
        // AUDIT-18: max_resource_id is a capacity hint, not a trusted bound.
        // The compiler enforces dense resource_index < instances.size(), but
        // the executor must fail closed on a sparse/pathological id instead
        // of indexing past the table (unbounded pinned growth per worker).
        if (max_resource_id > kMaxResourceId)
            max_resource_id = kMaxResourceId;
        if (prepared_graph_owner != graph_owner) {
            prepared_text_batches.clear();
            prepared_graph_owner = graph_owner;
        }
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

runtime::TextRunDynamic build_prepared_mtsdf_run(
    const RenderGraphContext& ctx,
    const TextRunShape& shape,
    float tx, float ty, float sx, float sy, float opacity) {
    runtime::TextRunDynamic run{};
    run.tx = tx;
    run.ty = ty;
    run.sx = sx;
    run.sy = sy;
    run.opacity = opacity;
    run.color = pack_text_color(shape.paint.fill);
    run.distance_range_bits = std::bit_cast<std::uint32_t>(8.0f);
    run.visible_glyph_count = std::numeric_limits<std::uint32_t>::max();
    if (shape.layout && !shape.animators.empty()) {
        // The prepared selector is compiled and owned by the runtime cache;
        // this helper only samples its immutable program for the current
        // frame. Non-lowerable animation is rejected by the reuse preflight.
        auto prepared = ctx.runtime
            ? ctx.runtime->gpu_text_prepared_cache().find(*shape.layout)
            : nullptr;
        // Lowerable glyph selectors are evaluated by the GPU text shader from
        // the compact dynamic run record.  Do not walk every glyph here: that
        // both defeats the hot path and can dereference an incomplete
        // compatibility selector sample (notably for Tracking).
        //
        // DEMOLITION DEBT (ACTIVE) — TICKET-GPU-TEXT-CPU-SELECTOR-COMPAT.
        // The branch below is the CPU per-glyph compatibility path for
        // animation shapes the GPU lowering cannot express. It is O(glyphs)
        // per frame and contradicts the prepared-GPU-text direction: the
        // end-state is "unsupported GPU animation fails or falls back AT
        // PREPARE time", never a per-frame CPU glyph walk. Its two counters
        // exist so the official corpus can prove the exit condition
        // (0 hits) before the branch is deleted.
        if (prepared && prepared->reveal_selector &&
            !text_run::can_lower_gpu_text_animation(shape)) {
            std::uint32_t visible_limit = 0;
            std::uint64_t glyph_evals = 0;
            const auto sample_time = ctx.frame_input.sample_time;
            for (std::size_t i = 0; i < shape.layout->placed.glyphs.size(); ++i) {
                if (shape.glyphs[i].glyph_id == 0 ||
                    shape.layout->placed.glyphs[i].bbox_x1 <=
                        shape.layout->placed.glyphs[i].bbox_x0) {
                    continue;
                }
                const auto weight = evaluate_compiled_selector(
                    *prepared->reveal_selector, shape.layout->units,
                    static_cast<std::uint32_t>(i), sample_time);
                ++glyph_evals;
                if (weight <= 1e-6f) visible_limit = static_cast<std::uint32_t>(i + 1);
            }
            run.visible_glyph_count = visible_limit;
            if (profiling::g_current_counters) {
                profiling::g_current_counters->gpu_text_cpu_selector_compat_frames
                    .fetch_add(1, std::memory_order_relaxed);
                profiling::g_current_counters->gpu_text_cpu_selector_compat_glyph_evals
                    .fetch_add(glyph_evals, std::memory_order_relaxed);
            }
        }
    }
    run.flags = 0u;
    fill_mtsdf_effects(shape, run);
    text_run::sample_gpu_text_animation(
        shape, ctx.frame_input.sample_time, run);
    return run;
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
        scratch.begin_batch(batch.instances.size(), compiled.graph_instance_id);
        all_glyphs.clear();
        all_runs.clear();
        atlas_pages.clear();
        std::span<const runtime::GlyphStatic> dispatch_glyphs{};
        std::span<const runtime::RenderSurfaceHandle> dispatch_pages{};
        bool reused_prepared_batch = false;

        // Once a compiled batch has proved that every member lowers to one
        // persistent MTSDF page, its GlyphStatic[] and page list are immutable
        // for the lifetime of that compiled graph.  Preflight the topology
        // before using the cache so a later style/animation incompatibility
        // cannot silently mix cached and legacy metadata.
        if (ctx.runtime) {
            const auto cached = scratch.prepared_text_batches.find(&batch);
            if (cached != scratch.prepared_text_batches.end() &&
                !cached->second.glyphs.empty() &&
                cached->second.atlas_pages.size() == 1) {
                bool can_reuse = true;
                for (const auto& inst : batch.instances) {
                    if (inst.node >= graph.size() || !graph.has_node(inst.node) ||
                        graph.node(inst.node).kind() != RenderGraphNodeKind::TextRun) {
                        can_reuse = false;
                        break;
                    }
                    const auto& shape = static_cast<const TextRunNode&>(
                        graph.node(inst.node)).shape();
                    if (!shape || !shape->layout || shape->animated_doc ||
                        (!shape->animators.empty() &&
                         !text_run::can_lower_gpu_text_animation(*shape))) {
                        can_reuse = false;
                        break;
                    }
                    const auto prepared = ctx.runtime->gpu_text_prepared_cache().find(
                        *shape->layout);
                    if (!prepared || !prepared->ready ||
                        prepared->batches.size() != 1 ||
                        prepared->batches.front().page !=
                            cached->second.atlas_pages.front()) {
                        can_reuse = false;
                        break;
                    }
                }
                if (can_reuse) {
                    for (const auto& inst : batch.instances) {
                        const auto& tr_node = static_cast<const TextRunNode&>(
                            graph.node(inst.node));
                        const auto& shape = *tr_node.shape();
                        float tx = 0.0f, ty = 0.0f, sx = 1.0f, sy = 1.0f;
                        float opacity = inst.opacity;
                        if (inst.transform_index != 0 &&
                            inst.transform_index < graph.size()) {
                            const auto& xform = static_cast<const TransformNode&>(
                                graph.node(inst.transform_index));
                            const auto m = xform.matrix();
                            tx = m[3][0];
                            ty = m[3][1];
                            sx = m[0][0];
                            sy = m[1][1];
                            opacity *= xform.opacity();
                        } else {
                            const auto m = tr_node.world_matrix(ctx);
                            tx = m[3][0];
                            ty = m[3][1];
                            sx = m[0][0];
                            sy = m[1][1];
                        }
                        all_runs.push_back(build_prepared_mtsdf_run(
                            ctx, shape, tx, ty, sx, sy, opacity));
                    }
                    dispatch_glyphs = cached->second.glyphs;
                    dispatch_pages = cached->second.atlas_pages;
                    reused_prepared_batch = true;
                    if (profiling::g_current_counters) {
                        profiling::g_current_counters->gpu_text_prepared_batch_reuse_count
                            .fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        }

        bool current_all_prepared = ctx.runtime != nullptr;
        bool current_saw_text = false;
        if (!reused_prepared_batch) for (const auto& inst : batch.instances) {
            if (inst.node >= graph.size() || !graph.has_node(inst.node)) {
                current_all_prepared = false;
                continue;
            }
            const auto& node = graph.node(inst.node);
            if (node.kind() != RenderGraphNodeKind::TextRun) {
                current_all_prepared = false;
                continue;
            }
            const auto& tr_node = static_cast<const TextRunNode&>(node);
            if (!tr_node.shape()) {
                current_all_prepared = false;
                continue;
            }
            current_saw_text = true;

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
            } else {
                const auto m = tr_node.world_matrix(ctx);
                tx = m[3][0];
                ty = m[3][1];
                sx = m[0][0];
                sy = m[1][1];
            }

            // Static runs are already compiled at scene preparation time and
            // must not be copied/reset/evaluated once per frame. Dynamic runs
            // retain the worker-local adapter until PreparedTextRun replaces
            // this compatibility branch.
            const TextRunShape* render_shape = tr_node.shape().get();
            const bool lowered_gpu_animation =
                render_shape &&
                !render_shape->animators.empty() &&
                text_run::can_lower_gpu_text_animation(*render_shape);
            if (render_shape &&
                (!render_shape->animators.empty() || render_shape->animated_doc) &&
                !lowered_gpu_animation) {
                text_run::prepare_per_frame_shape(
                    *render_shape, ctx.frame_input.sample_time, scratch.text_frame);
                render_shape = &scratch.text_frame.render_shape;
            }
            if (!render_shape || !render_shape->layout) {
                current_all_prepared = false;
                continue;
            }

            const auto& layout = *render_shape->layout;

            // Prepared path: FreeType outlines are promoted to the
            // persistent GpuGlyphAtlas once per (runtime, shape, layout).
            // Steady-state frames only append already-compiled metadata and
            // update the small dynamic run record.
            if (ctx.runtime) {
                auto prepared = ctx.runtime->gpu_text_prepared_cache().find(layout);
                if (!prepared) {
                    prepared = text_run::prepare_mtsdf_text_run(
                        *ctx.runtime, ctx.text_render_resources,
                        ctx.asset_resolver, *render_shape);
                    if (prepared && prepared->ready) {
                        ctx.runtime->gpu_text_prepared_cache().store(layout, prepared);
                    }
                }
                if (prepared && prepared->ready && prepared->batches.size() == 1) {
                    const auto& batch = prepared->batches.front();
                    const auto page = batch.page;
                    if (atlas_pages.empty() || atlas_pages.front() == page) {
                        if (atlas_pages.empty()) atlas_pages.push_back(page);
                        const bool shader_animation =
                            text_run::can_lower_gpu_text_animation(*render_shape);
                        if (!shader_animation) {
                            // The atlas and the final composite remain fully
                            // GPU-native. The existing text animator has
                            // already resolved the unsupported selector stack
                            // into GlyphInstanceState; bind those compact
                            // per-glyph values as independent GPU run records
                            // instead of falling back to a CPU framebuffer.
                            current_all_prepared = false;
                            for (const auto& static_glyph : batch.glyphs) {
                                const auto glyph_index =
                                    static_cast<std::size_t>(static_glyph.pad0);
                                if (glyph_index >= render_shape->glyphs.size()) {
                                    report_fused_batch_error(
                                        ctx, RenderBackendErrorCode::InvalidInput,
                                        "GPU-native text glyph state index is out of range");
                                    return;
                                }
                                const auto& glyph = render_shape->glyphs[glyph_index];
                                if (glyph.rotation.x != 0.0f ||
                                    glyph.rotation.y != 0.0f ||
                                    glyph.rotation.z != 0.0f ||
                                    glyph.skew != 0.0f ||
                                    glyph.blur != 0.0f) {
                                    report_fused_batch_error(
                                        ctx, RenderBackendErrorCode::UnsupportedCapability,
                                        "GPU-native text animation requires unsupported "
                                        "rotation, skew, or per-glyph blur");
                                    return;
                                }
                                runtime::TextRunDynamic glyph_run{};
                                glyph_run.tx = tx + glyph.position.x * sx;
                                glyph_run.ty = ty + (glyph.position.y +
                                    glyph.baseline_shift) * sy;
                                glyph_run.sx = sx * glyph.scale.x;
                                glyph_run.sy = sy * glyph.scale.y;
                                glyph_run.opacity = opacity * glyph.opacity;
                                glyph_run.color = pack_text_color(glyph.fill);
                                glyph_run.distance_range_bits =
                                    std::bit_cast<std::uint32_t>(8.0f);
                                glyph_run.visible_glyph_count =
                                    std::numeric_limits<std::uint32_t>::max();
                                glyph_run.flags = 0u;
                                const auto run_index =
                                    static_cast<std::uint32_t>(all_runs.size());
                                all_runs.push_back(glyph_run);
                                auto dynamic_glyph = static_glyph;
                                dynamic_glyph.run_index = run_index;
                                all_glyphs.push_back(dynamic_glyph);
                            }
                            continue;
                        }
                        runtime::TextRunDynamic run_dyn{};
                        run_dyn.tx = tx;
                        run_dyn.ty = ty;
                        run_dyn.sx = sx;
                        run_dyn.sy = sy;
                        run_dyn.opacity = opacity;
                        run_dyn.color = pack_text_color(render_shape->paint.fill);
                        text_run::sample_gpu_run_level_animation(
                            *render_shape, ctx.frame_input.sample_time, run_dyn);
                        run_dyn.distance_range_bits = std::bit_cast<std::uint32_t>(8.0f);
                        run_dyn.visible_glyph_count =
                            std::numeric_limits<std::uint32_t>::max();
                        if (prepared->reveal_selector &&
                            !text_run::can_lower_gpu_text_animation(*render_shape)) {
                            std::uint32_t visible_limit = 0;
                            const auto sample_time = ctx.frame_input.sample_time;
                            for (std::size_t i = 0; i < layout.placed.glyphs.size(); ++i) {
                                if (render_shape->glyphs[i].glyph_id == 0 ||
                                    layout.placed.glyphs[i].bbox_x1 <= layout.placed.glyphs[i].bbox_x0) {
                                    continue;
                                }
                                const auto weight = evaluate_compiled_selector(
                                    *prepared->reveal_selector, layout.units,
                                    static_cast<std::uint32_t>(i), sample_time);
                                if (weight <= 1e-6f) {
                                    visible_limit = static_cast<std::uint32_t>(i + 1);
                                }
                            }
                            run_dyn.visible_glyph_count = visible_limit;
                        }
                        run_dyn.flags = 0u;
                        fill_mtsdf_effects(*render_shape, run_dyn);
                        text_run::sample_gpu_text_animation(
                            *render_shape, ctx.frame_input.sample_time, run_dyn);
                        const auto run_index = static_cast<std::uint32_t>(all_runs.size());
                        all_runs.push_back(run_dyn);
                        const auto first = all_glyphs.size();
                        all_glyphs.insert(all_glyphs.end(),
                                          batch.glyphs.begin(),
                                          batch.glyphs.end());
                        for (std::size_t i = first; i < all_glyphs.size(); ++i) {
                            all_glyphs[i].run_index = run_index;
                        }
                        continue;
                    }
                    if (ctx.policy.is_gpu_native_required()) {
                        report_fused_batch_error(
                            ctx, RenderBackendErrorCode::UnsupportedCapability,
                            "GPU-native fused text batch needs one atlas page; "
                            "prepared text runs resolved to different pages");
                        return;
                    }
                }
            }

            current_all_prepared = false;

            // REQUIRE_GPU_NATIVE is a strict preparation contract.  The
            // fused path used to continue into the compatibility atlas
            // lookup below when a prepared MTSDF run was missing, which made
            // the frame appear GPU-backed while silently selecting a legacy
            // text authority.  Fail before touching that fallback; Auto and
            // compatibility callers retain the explicitly non-native route.
            if (ctx.policy.is_gpu_native_required()) {
                const auto prepared_diag = ctx.runtime
                    ? ctx.runtime->gpu_text_prepared_cache().find(layout)
                    : nullptr;
                std::string detail =
                    " layout_glyphs=" + std::to_string(layout.placed.glyphs.size()) +
                    " shape_glyphs=" + std::to_string(render_shape->glyphs.size()) +
                    " animators=" + std::to_string(render_shape->animators.size()) +
                    " prepared=" + (prepared_diag ? "yes" : "no");
                if (prepared_diag) {
                    detail += " ready=" +
                        std::string(prepared_diag->ready ? "yes" : "no") +
                        " batches=" +
                        std::to_string(prepared_diag->batches.size());
                }
                report_fused_batch_error(
                    ctx, RenderBackendErrorCode::UnsupportedCapability,
                    "GPU-native fused text batch is not fully prepared" + detail);
                return;
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

        if (!reused_prepared_batch) {
            dispatch_glyphs = all_glyphs;
            dispatch_pages = atlas_pages;
            if (current_all_prepared && current_saw_text &&
                !all_glyphs.empty() && atlas_pages.size() == 1) {
                auto& prepared = scratch.prepared_text_batches[&batch];
                prepared.glyphs = all_glyphs;
                prepared.atlas_pages = atlas_pages;
                if (profiling::g_current_counters) {
                    profiling::g_current_counters->gpu_text_prepared_batch_build_count
                        .fetch_add(1, std::memory_order_relaxed);
                }
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
            dest_fb->surface_handle(), dispatch_glyphs, all_runs, dispatch_pages);
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
    scratch.begin_batch(batch.instances.size(), compiled.graph_instance_id);
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

        // Typed programs are sampled per frame before execution. The fused
        // image path must consume the same sampled transform as the ordinary
        // SourceNode path; reading only the immutable TransformNode here
        // freezes entity cards at their authored centre and drops motion.
        const compiler::SampledLayerParameters* sampled_layer = nullptr;
        if (ctx.frame_parameters && inst.node < compiled.nodes.size() &&
            compiled.nodes[inst.node].binding_meta.has_binding()) {
            sampled_layer = compiler::find_layer(
                *ctx.frame_parameters,
                compiled.nodes[inst.node].binding_meta.layer_index);
            if (!sampled_layer || !sampled_layer->active) {
                continue;
            }
        }

        float tx = 0.0f, ty = 0.0f, sx = 1.0f, sy = 1.0f;
        float opacity = inst.opacity;
        if (sampled_layer) {
            const auto& m = sampled_layer->matrix;
            tx = m[3][0];
            ty = m[3][1];
            sx = m[0][0];
            sy = m[1][1];
            opacity *= sampled_layer->opacity;
        } else if (inst.transform_index != 0 && inst.transform_index < graph.size()) {
            const auto& xform = static_cast<const TransformNode&>(graph.node(inst.transform_index));
            const auto m = xform.matrix();
            tx = m[3][0];
            ty = m[3][1];
            sx = m[0][0];
            sy = m[1][1];
            opacity *= xform.opacity();
        }

        const auto placement = compute_media_placement(original_source_size, img.size, img.fit, img.focal_point);

        // Placement is authored around the layer centre. Apply the sampled
        // scale around that centre so scale-in/focus presets are visible in
        // the GPU batch just as they are in the canonical source path.
        const float centre_x = tx + (ctx.frame_input.width * 0.5f);
        const float centre_y = ty + (ctx.frame_input.height * 0.5f);
        const float local_x0 = -img.size.x * 0.5f + placement.dst_rect.origin.x;
        const float local_y0 = -img.size.y * 0.5f + placement.dst_rect.origin.y;
        const float local_x1 = local_x0 + placement.dst_rect.size.x;
        const float local_y1 = local_y0 + placement.dst_rect.size.y;
        const float world_x0 = centre_x + local_x0 * sx;
        const float world_y0 = centre_y + local_y0 * sy;
        const float world_x1 = centre_x + local_x1 * sx;
        const float world_y1 = centre_y + local_y1 * sy;

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
