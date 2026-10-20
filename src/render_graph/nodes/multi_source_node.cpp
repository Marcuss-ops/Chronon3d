#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/cache/node_cache_identity_builder.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/detail/bbox_projection.hpp>
#include <chronon3d/render_graph/nodes/detail/projection_helpers.hpp>
#include "../builder/evaluated_layer_placement.hpp"
#include "detail/preflight_bbox.hpp"
#include "native_surface.hpp"
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#ifdef CHRONON3D_ENABLE_TEXT
#include <chronon3d/text/text_run_geometry.hpp>
#include <chronon3d/text/text_run.hpp>
#include "text_run/text_run_execution.hpp"
#include "text_run/text_run_transform.hpp"
#endif
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <chronon3d/core/profiling/profiling.hpp>
#include <limits>

namespace chronon3d::graph {

MultiSourceNode::MultiSourceNode(
    std::string name, std::vector<MultiSourceItem> items, const cache::NodeCacheKey& key,
    RenderNodeCachePolicy policy
) : RenderGraphNode(policy), m_name(std::move(name)), m_items(std::move(items)), m_key(key) {
    normalize_items(m_items);
}

std::optional<raster::BBox> MultiSourceNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>>
) const {
    i32 x0 = std::numeric_limits<i32>::max();
    i32 y0 = std::numeric_limits<i32>::max();
    i32 x1 = std::numeric_limits<i32>::min();
    i32 y1 = std::numeric_limits<i32>::min();
    bool has_any = false;

    // CHRONON3D_PROJ_DIAG: indexed-for loop so item index reflects actual vector
    // position even when null items are skipped (range-for + manual counter was
    // bug-prone: counter incremented only in null-item-skip branch, leaving
    // bbox_i stale at 0 for non-null iterations where the diagnostic fires).
    for (std::size_t bbox_i = 0; bbox_i < m_items.size(); ++bbox_i) {
        const auto& item = m_items[bbox_i];
        if (!item.node) continue;
        const auto placement = detail::evaluate_source_payload_placement(
            item.matrix,
            item.opacity,
            ctx,
            item.apply_camera_projection,
            item.defer_camera_projection,
            item.native_3d,
            m_name,
            "predicted_bbox",
            bbox_i);
        if (!placement) {
            continue;
        }
        const Mat4 matrix = placement->render_matrix;

        f32 spread = 0.0f;
        spread += 8.0f;

        raster::BBox bbox;
#ifdef CHRONON3D_ENABLE_TEXT
        if (item.node->shape.type() == ShapeType::TextRun && item.node->shape.text_run_shape_handle().value) {
            bbox = renderer::compute_text_run_world_bbox(
                *item.node->shape.text_run_shape_handle().value, matrix, spread);
        } else
#endif
        // TICKET-122 FASE 3: GridPlane now uses 2.5D projection,
        // standard compute_world_bbox path (not native 3D).
        if (item.node->shape.type() == ShapeType::Mesh ||
            (ctx.frame_input.has_camera_2_5d &&
             item.node->shape.type() == ShapeType::FakeBox3D)) {
            if (auto proj_bbox = detail::projected_native_3d_bbox(
                    ctx, *item.node, matrix, spread)) {
                bbox = *proj_bbox;
            } else {
                bbox = raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
            }
        } else {
            bbox = renderer::compute_world_bbox(item.node->shape, matrix, spread);
        }

        // Keep the diagnostic/world-space bounds separate from the execution
        // bounds. Diagnostics may inspect the unclipped item bbox, but the
        // aggregate consumed by culling, tile pruning, dirty clipping, and
        // cache state must always be canvas-clipped.
        const auto diagnostic_bbox = bbox;
        const auto execution = detail::resolve_execution_bbox(
            *placement, diagnostic_bbox, ctx);
        if (!execution) {
            continue;
        }
        const auto execution_bbox = *execution;

        if (ctx.policy.diagnostics_enabled) {
            spdlog::debug(
                "[multi-source-bbox] node='{}' item#{} diagnostic=[{},{},{},{}] execution=[{},{},{},{}]",
                m_name,
                bbox_i,
                diagnostic_bbox.x0, diagnostic_bbox.y0,
                diagnostic_bbox.x1, diagnostic_bbox.y1,
                execution_bbox.x0, execution_bbox.y0,
                execution_bbox.x1, execution_bbox.y1);
        }

        if (!execution_bbox.is_empty()) {
            x0 = std::min(x0, execution_bbox.x0);
            y0 = std::min(y0, execution_bbox.y0);
            x1 = std::max(x1, execution_bbox.x1);
            y1 = std::max(y1, execution_bbox.y1);
            has_any = true;
        }
    }

    if (!has_any) {
        return raster::BBox{0, 0, 0, 0};
    }
    return raster::BBox{x0, y0, x1, y1};
}

std::optional<raster::BBox> detail::preflight_diagnostic_bbox(
    const MultiSourceNode& node,
    const RenderGraphContext& ctx) {
    i32 x0 = std::numeric_limits<i32>::max();
    i32 y0 = std::numeric_limits<i32>::max();
    i32 x1 = std::numeric_limits<i32>::min();
    i32 y1 = std::numeric_limits<i32>::min();
    bool has_any = false;

    for (std::size_t item_index = 0; item_index < node.m_items.size(); ++item_index) {
        const auto& item = node.m_items[item_index];
        if (!item.node) continue;
        const auto placement = detail::evaluate_source_payload_placement(
            item.matrix, item.opacity, ctx, item.apply_camera_projection,
            item.defer_camera_projection, item.native_3d, node.m_name,
            "diagnostic_bbox", item_index);
        if (!placement) continue;

        raster::BBox bbox;
#ifdef CHRONON3D_ENABLE_TEXT
        if (item.node->shape.type() == ShapeType::TextRun &&
            item.node->shape.text_run_shape_handle().value) {
            bbox = renderer::compute_text_run_world_bbox(
                *item.node->shape.text_run_shape_handle().value,
                placement->render_matrix, 8.0f);
        } else
#endif
        if (item.node->shape.type() == ShapeType::Mesh ||
            (ctx.frame_input.has_camera_2_5d &&
             item.node->shape.type() == ShapeType::FakeBox3D)) {
            const auto projected = detail::projected_native_3d_bbox(
                ctx, *item.node, placement->render_matrix, 8.0f);
            if (!projected) continue;
            bbox = *projected;
        } else {
            bbox = renderer::compute_world_bbox(
                item.node->shape, placement->render_matrix, 8.0f);
        }

        x0 = std::min(x0, bbox.x0);
        y0 = std::min(y0, bbox.y0);
        x1 = std::max(x1, bbox.x1);
        y1 = std::max(y1, bbox.y1);
        has_any = true;
    }

    if (!has_any) return raster::BBox{0, 0, 0, 0};
    return raster::BBox{x0, y0, x1, y1};
}

cache::NodeCacheKey MultiSourceNode::cache_key(const RenderGraphContext& ctx) const {
    auto key = m_key;
    // Fase A6 — frame discriminator for animator-driven text changes.
    // With glyphs no longer mutated in-place, the text_run hash fold
    // below only captures doc-driven changes (Scramble/Morph/Crossfade).
    // Animator-driven changes (position/opacity/scale/blur per-frame)
    // need the frame number to invalidate the cache.  Fold the integral
    // frame so consecutive frames with different animator states don't
    // share a stale entry.
    key.params_hash = hash_combine(key.params_hash, hash_value(static_cast<u64>(ctx.frame_input.sample_time.integral_frame())));
    key.frame = cache_frame_for_policy(cache_policy(), ctx.frame_input.frame);
    key.params_hash = hash_combine(key.params_hash, static_cast<u64>(ctx.policy.modular_coordinates));

    // Hash every item's full world matrix and opacity so the cache key
    // changes when the layer-level animation (e.g. tracking_breathing)
    // produces a different transform.
    for (const auto& item : m_items) {
        key.params_hash = hash_combine(key.params_hash, hash_value(item.matrix));
        key.params_hash = hash_combine(key.params_hash, hash_value(item.opacity));
        const auto groups = item.groups.empty() && item.group
            ? std::vector<MultiSourceItem::GroupIsolationSpec>{*item.group}
            : item.groups;
        for (const auto& group : groups) {
            key.params_hash = hash_combine(
                key.params_hash, hash_value(static_cast<std::uint64_t>(group.id)));
            key.params_hash = hash_combine(key.params_hash, hash_value(group.opacity));
            if (group.opacity_animation) {
                const auto& animation = *group.opacity_animation;
                key.params_hash = hash_combine(key.params_hash,
                    hash_value(static_cast<u64>(animation.start_frame)));
                key.params_hash = hash_combine(key.params_hash,
                    hash_value(static_cast<u64>(animation.end_frame)));
                key.params_hash = hash_combine(key.params_hash, hash_value(animation.from));
                key.params_hash = hash_combine(key.params_hash, hash_value(animation.to));
                key.params_hash = hash_combine(key.params_hash, hash_value(animation.discrete));
            }
        }
#ifdef CHRONON3D_ENABLE_TEXT
        // text_run items also fold the per-glyph animated state of
        // the underlying TextRunShape so the cache key invalidates when
        // `evaluate_animator_stack` mutates glyph state.  Without this
        // fold two animated frames with identical geometry would hit a
        // stale cache entry.  PR 10: use the frame overload so
        // Scramble / Morph / DissolveLayouts / font-swap Cut frames
        // driven by an AnimatedTextDocument also invalidate correctly.
        if (item.node && item.node->shape.type() == ShapeType::TextRun && item.node->shape.text_run_shape_handle().value) {
            key.params_hash = hash_combine(
                key.params_hash,
                chronon3d::hash_text_run_shape(
                    *item.node->shape.text_run_shape_handle().value,
                    ctx.frame_input.sample_time.integral_frame()));
        }
#endif
    }

    // 2.5D camera fingerprint (TICKET-ae-cam-hash-collision Soluzione B):
    // canonical conditional fold — single implementation for the invariant.
    cache::fold_camera_if(
        key, ctx.frame_input.has_camera_2_5d, ctx.frame_input.camera_2_5d);

    return key;
}

NodeExecResult MultiSourceNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef>,
    std::span<const std::optional<raster::BBox>>
) {
    CHRONON_TRACE_SCOPE("chronon.node", "multi_source_render");

    // Fase A4 — null backend is a hard error (matches TextRunNode contract).
    if (!ctx.backend) {
        spdlog::error(
            "[multi-source] node='{}' cannot render: backend is null; "
            "aborting frame.", m_name);
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::InvalidInput,
            m_name,
            "backend is null"
        }};
    }

    auto fb = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height, /*clear=*/true);
    // A MultiSourceNode shares one framebuffer across its regular and text-run
    // items. In strict GPU mode this framebuffer must be materialized before
    // the first draw: otherwise native path/shape dispatch rejects the empty
    // handle, the legacy path leaves a CPU-only result, and the following
    // CompositeNode cannot preserve GPU residency.
    if (ctx.backend->supports_native_surfaces() && ctx.surface_registry &&
        !ensure_empty_native_surface(ctx, *fb)) {
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::ExecutionFailure,
            m_name,
            "failed to materialize native multi-source surface"}};
    }

    // ── text_run items are dispatched to `RenderBackend::draw_text_run`
    // instead of the generic `RenderBackend::draw_node` because the
    // former routes through the dedicated text-run processor with the
    // per-glyph transform stack.  The text is rasterized directly onto
    // the SHARED `*fb` so it composites SRC_OVER any earlier non-text
    // items in the same layer (vector order).
    //
    // Fase A4 — error propagation unified with TextRunNode:
    //   - draw_text_run failure → NodeExecutionError (immediate return)
    //   - unsupported capability → NodeExecutionError
    //   - null shape → skip item (not an error — empty data)
    //
    // Regular items use draw_node() which returns void — tracked for
    // Phase C (requires coordinated API/ABI change).

    //
    // ADR-035 — SVG isolated group stage.
    //
    // Items tagged with `group` draw at FULL paint alpha into an own
    // intermediate framebuffer (cleared per group). When the group ends
    // (different group id, a non-grouped item, or the layer end) the
    // surface composites once onto the shared layer framebuffer with the
    // group alpha scaled by the item's placement alpha:
    //
    //   out.rgb   = src.rgb * g + dst.rgb * (1 - src.a * g)
    //   out.a     = src.a * g + dst.a   * (1 - src.a * g)
    //
    // (both surfaces premultiplied; g = group_opacity * item alpha).
    // Overlapping children therefore never darken each other — the group
    // fades as one unit, matching SVG/AE isolated-group compositing.
    //
    // Non-grouped items keep the historical per-item draw-time alpha fold,
    // so text and legacy shape layers are untouched by this branch.
    //
    bool has_group_items = false;
    for (const auto& item : m_items)
        if (item.group || !item.groups.empty()) { has_group_items = true; break; }
    const bool native_group_surfaces = has_group_items &&
        ctx.backend->supports_native_surfaces() && ctx.surface_registry;
    if (has_group_items && ctx.policy.is_gpu_native_required() &&
        !native_group_surfaces) {
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::UnsupportedCapability, m_name,
            "svg_group_native_unsupported: native surface registry unavailable"}};
    }

    struct ActiveGroup {
        MultiSourceItem::GroupIsolationSpec spec;
        OwnedFB framebuffer;
    };
    std::vector<ActiveGroup> active_groups;

    std::string group_failure;
    auto blend_group_surface = [&](Framebuffer& destination, Framebuffer& surface,
                                   f32 group_alpha) -> bool {
        if (native_group_surfaces) {
            const auto destination_handle = destination.surface_handle();
            const auto source = surface.surface_handle();
            if (destination_handle == runtime::kInvalidRenderSurfaceHandle ||
                source == runtime::kInvalidRenderSurfaceHandle ||
                !ctx.backend->is_native_surface_valid(destination_handle) ||
                !ctx.backend->is_native_surface_valid(source)) {
                group_failure = "svg_group_native_unsupported: group surface is not GPU resident";
                return false;
            }
            const auto result = ctx.backend->composite_surface_weighted(
                destination_handle, source, group_alpha);
            if (!result.ok()) {
                group_failure = "SVG isolated group native composite failed: " +
                    result.error().message;
                return false;
            }
            destination.mark_gpu_authoritative();
            return true;
        }
        const i32 width = ctx.frame_input.width;
        const i32 height = ctx.frame_input.height;
        for (i32 y = 0; y < height; ++y) {
            for (i32 x = 0; x < width; ++x) {
                const Color src = surface.get_pixel(x, y);
                if (src.a <= 0.0f && group_alpha >= 1.0f) continue;
                const Color dst = destination.get_pixel(x, y);
                const f32 scaled_a = src.a * group_alpha;
                if (scaled_a <= 0.0f) continue;
                const f32 keep = 1.0f - scaled_a;
                const f32 out_a = scaled_a + dst.a * keep;
                if (out_a <= 0.0f) {
                    destination.set_pixel(x, y, Color::transparent());
                    continue;
                }
                Color out;
                out.r = src.r * group_alpha + dst.r * keep;
                out.g = src.g * group_alpha + dst.g * keep;
                out.b = src.b * group_alpha + dst.b * keep;
                out.a = out_a;
                destination.set_pixel(x, y, out);
            }
        }
        return true;
    };
    auto flush_groups_to = [&](std::size_t depth) -> bool {
        while (active_groups.size() > depth) {
            ActiveGroup closing = std::move(active_groups.back());
            active_groups.pop_back();
            Framebuffer& destination = active_groups.empty()
                ? *fb : *active_groups.back().framebuffer;
            if (!blend_group_surface(destination, *closing.framebuffer,
                                     closing.spec.opacity)) return false;
        }
        return true;
    };

    for (std::size_t i = 0; i < m_items.size(); ++i) {
            const auto& item = m_items[i];
            if (!item.node) continue;

            auto item_groups = item.groups;
            if (item_groups.empty() && item.group) item_groups.push_back(*item.group);
            const double group_frame = static_cast<double>(
                ctx.frame_input.sample_time.integral_frame());
            for (auto& group : item_groups) {
                if (!group.opacity_animation) continue;
                const auto& animation = *group.opacity_animation;
                if (animation.end_frame <= animation.start_frame ||
                    !std::isfinite(animation.from) || !std::isfinite(animation.to))
                    return NodeExecResult{NodeExecutionError{
                        RenderBackendErrorCode::InvalidInput, m_name,
                        "svg_group_opacity_animation_invalid"}};
                if (group_frame < static_cast<double>(animation.start_frame)) continue;
                if (group_frame >= static_cast<double>(animation.end_frame)) {
                    group.opacity = animation.to;
                } else if (animation.discrete) {
                    group.opacity = animation.from;
                } else {
                    const double progress = (group_frame - animation.start_frame) /
                        static_cast<double>(animation.end_frame - animation.start_frame);
                    group.opacity = static_cast<float>(animation.from +
                        (animation.to - animation.from) * progress);
                }
            }
            if (item_groups.size() > 16)
                return NodeExecResult{NodeExecutionError{
                    RenderBackendErrorCode::UnsupportedCapability, m_name,
                    "svg_group_depth_unsupported: nesting exceeds 16"}};
            const std::size_t shared_depth = [&] {
                std::size_t depth = 0;
                while (depth < active_groups.size() && depth < item_groups.size() &&
                    active_groups[depth].spec.id == item_groups[depth].id &&
                    std::abs(active_groups[depth].spec.opacity - item_groups[depth].opacity) < 1e-6f)
                    ++depth;
                return depth;
            }();
            if (!flush_groups_to(shared_depth))
                return NodeExecResult{NodeExecutionError{
                    RenderBackendErrorCode::ExecutionFailure, m_name,
                    group_failure.empty() ? "svg_group_stage_failure" : group_failure}};
            for (std::size_t depth = shared_depth; depth < item_groups.size(); ++depth) {
                const auto& group = item_groups[depth];
                if (group.id == 0 || !std::isfinite(group.opacity) ||
                    group.opacity < 0.0f || group.opacity > 1.0f)
                    return NodeExecResult{NodeExecutionError{
                        RenderBackendErrorCode::InvalidInput, m_name,
                        "svg_group_invalid: id and opacity are outside supported bounds"}};
                if (!native_group_surfaces && fb->data() == nullptr) {
                    spdlog::error(
                        "[multi-source] node='{}': isolated SVG groups need CPU pixel access",
                        m_name);
                    return NodeExecResult{NodeExecutionError{
                        RenderBackendErrorCode::UnsupportedCapability, m_name,
                        "svg_group_native_unsupported"}};
                }
                auto surface = ctx.acquire_owned_fb(
                    ctx.frame_input.width, ctx.frame_input.height, /*clear=*/true);
                if (native_group_surfaces && !ensure_empty_native_surface(ctx, *surface))
                    return NodeExecResult{NodeExecutionError{
                        RenderBackendErrorCode::ExecutionFailure, m_name,
                        "failed to create native SVG group surface"}};
                active_groups.push_back(ActiveGroup{group, std::move(surface)});
                if (ctx.policy.diagnostics_enabled)
                    spdlog::info("[multi-source] node='{}' group={} depth={} alpha={:.4f} begin",
                        m_name, group.id, depth, group.opacity);
            }
            const bool item_is_grouped = !item_groups.empty();
            if (item_is_grouped) {
                if (item.node->shape.type() == ShapeType::TextRun) {
                    // Text runs and isolated SVG groups would need a
                    // different draw route; fail closed until certified.
                    spdlog::error(
                        "[multi-source] node='{}': text run items cannot join "
                        "an isolated SVG group (svg_group_text_run_unsupported)",
                        m_name);
                    return NodeExecResult{NodeExecutionError{
                        RenderBackendErrorCode::UnsupportedCapability,
                        m_name,
                        "svg_group_text_run_unsupported"
                    }};
                }
            }

#ifdef CHRONON3D_ENABLE_TEXT
            // ── text_run branch — unified via render_text_run_item ────
            if (item.node->shape.type() == ShapeType::TextRun) {
                auto run_shape = item.node->shape.text_run_shape_handle().value;
                if (!run_shape) {
                    continue;
                }

                if (!ctx.backend->capabilities().text_run) {
                    spdlog::error(
                        "[multi-source] node='{}' contains text run items "
                        "but active backend does not support draw_text_run; "
                        "aborting frame.", m_name);
                    return NodeExecResult{NodeExecutionError{
                        RenderBackendErrorCode::UnsupportedCapability,
                        m_name,
                        "backend does not support draw_text_run"
                    }};
                }
    if (std::getenv("CHRONON3D_PATH_DIAG"))
        spdlog::info("[native-multisource-diag] node={} surface_support={} registry={} surface={} cpu={} items={}",
            m_name, ctx.backend->supports_native_surfaces(), ctx.surface_registry != nullptr,
            fb->surface_handle(), fb->data() != nullptr, m_items.size());

                // 2.5D-aware placement: if camera is active, project
                // the item matrix; otherwise the source pass already
                // resolved the final matrix.
                const auto placement = detail::evaluate_source_payload_placement(
                    item.matrix,
                    item.opacity,
                    ctx,
                    true,
                    item.defer_camera_projection,
                    false,
                    m_name,
                    "text_run_execute",
                    i);
                if (!placement) {
                    continue;
                }
                const Mat4 resolved_matrix = placement->render_matrix;

                auto result = text_run::render_text_run_item(
                    ctx, *ctx.backend, *fb, *run_shape,
                    TextRunPlacement{resolved_matrix}, item.opacity);

                if (!result) {
                    spdlog::error(
                        "[multi-source] node='{}' text_run failed: [{}] {}",
                        m_name,
                        chronon3d::graph::render_backend_error_code_name(result.error().code),
                        result.error().message);
                    return NodeExecResult{NodeExecutionError{
                        result.error().code,
                        m_name,
                        result.error().message
                    }};
                }

                if (result.value().actual_ink_bbox) {
                    ctx.node_exec.actual_ink_bbox = *result.value().actual_ink_bbox;
                }

                if (ctx.policy.diagnostics_enabled) {
                    spdlog::info(
                        "[AE_CAM] frame={} node='{}' item#{} world=({},{},{}) opacity={:.3f}",
                        ctx.frame_input.sample_time.integral_frame(),
                        m_name, i,
                        item.matrix[3][0], item.matrix[3][1], item.matrix[3][2],
                        item.opacity
                    );
                }
                continue;
            }
#endif

            // ── regular (non-text-run) item ───────────────────────
            RenderState state;
            state.frame_number = static_cast<int>(ctx.frame_input.frame);
            state.ssaa_factor = ctx.policy.ssaa_factor;
            state.light_context = ctx.frame_input.light_context;
            state.rim_light = ctx.frame_input.rim_light;
            const auto placement = detail::evaluate_source_payload_placement(
                item.matrix,
                item.opacity,
                ctx,
                true,                    item.defer_camera_projection,
                    item.native_3d,
                    m_name,
                    "regular_execute",

                i);
            if (!placement) {
                continue;
            }
            state.matrix = placement->render_matrix;

            state.opacity = item_is_grouped ? 1.0f : item.opacity;
            if (i < ctx.node_exec.current_shape_processors.size()) {
                state.shape_processor = ctx.node_exec.current_shape_processors[i];
                state.processor_snapshot = ctx.node_exec.processor_snapshot;
            } else {
                // Direct node tests may execute outside GraphExecutor; the
                // production compiled path always supplies the indexed
                // binding above. Keep the fallback explicit and non-registry
                // based so this path never performs per-frame resolution.
                state.shape_processor = ctx.node_exec.current_shape_processor;
                state.processor_snapshot = ctx.node_exec.processor_snapshot;
            }
            state.world_matrix = item.matrix;
            state.clip_rect = ctx.node_exec.clip_rect;
            state.diagnostics_enabled = ctx.policy.diagnostics_enabled;

            if (ctx.frame_input.has_camera_2_5d) {
                state.projection  = ctx.frame_input.projection_ctx;
            }

        Framebuffer* draw_target = item_is_grouped
            ? active_groups.back().framebuffer.get() : fb.get();
        if (!item.node->svg_local_masks.empty()) {
            if (item.node->shape.type() != ShapeType::Path ||
                native_group_surfaces || ctx.policy.is_gpu_native_required())
                return NodeExecResult{NodeExecutionError{
                    RenderBackendErrorCode::UnsupportedCapability, m_name,
                    "svg_element_mask_native_unsupported"}};
            auto source_surface = ctx.acquire_owned_fb(
                ctx.frame_input.width, ctx.frame_input.height, /*clear=*/true);
            if (source_surface->data() == nullptr)
                return NodeExecResult{NodeExecutionError{
                    RenderBackendErrorCode::UnsupportedCapability, m_name,
                    "svg_element_mask_requires_software_surface"}};
            const auto source_draw = ctx.backend->draw_node(
                *source_surface, *item.node, state, ctx.frame_input.camera,
                ctx.frame_input.width, ctx.frame_input.height);
            if (!source_draw.ok())
                return NodeExecResult{NodeExecutionError{
                    source_draw.error().code, m_name, source_draw.error().message}};

            auto mask_surface = ctx.acquire_owned_fb(
                ctx.frame_input.width, ctx.frame_input.height, /*clear=*/true);
            if (mask_surface->data() == nullptr)
                return NodeExecResult{NodeExecutionError{
                    RenderBackendErrorCode::UnsupportedCapability, m_name,
                    "svg_element_mask_requires_software_surface"}};
            RenderState mask_state = state;
            mask_state.opacity = 1.0f;
            for (const auto& mask_path : item.node->svg_local_masks) {
                mask_surface->clear(Color::transparent());
                RenderNode mask_node;
                mask_node.name = "svg_element_mask";
                mask_node.shape = Shape{mask_path};
                const auto mask_draw = ctx.backend->draw_node(
                    *mask_surface, mask_node, mask_state, ctx.frame_input.camera,
                    ctx.frame_input.width, ctx.frame_input.height);
                if (!mask_draw.ok())
                    return NodeExecResult{NodeExecutionError{
                        mask_draw.error().code, m_name, mask_draw.error().message}};
                for (i32 y = 0; y < ctx.frame_input.height; ++y) {
                    for (i32 x = 0; x < ctx.frame_input.width; ++x) {
                        const f32 coverage = std::clamp(mask_surface->get_pixel(x, y).a, 0.0f, 1.0f);
                        if (coverage >= 1.0f) continue;
                        Color pixel = source_surface->get_pixel(x, y);
                        pixel.r *= coverage;
                        pixel.g *= coverage;
                        pixel.b *= coverage;
                        pixel.a *= coverage;
                        source_surface->set_pixel(x, y, pixel);
                    }
                }
            }
            for (i32 y = 0; y < ctx.frame_input.height; ++y) {
                for (i32 x = 0; x < ctx.frame_input.width; ++x) {
                    const Color src = source_surface->get_pixel(x, y);
                    if (src.a <= 0.0f) continue;
                    const Color dst = draw_target->get_pixel(x, y);
                    const f32 keep = 1.0f - src.a;
                    draw_target->set_pixel(x, y, Color{
                        src.r + dst.r * keep,
                        src.g + dst.g * keep,
                        src.b + dst.b * keep,
                        src.a + dst.a * keep});
                }
            }
        } else {
            const bool native_image = item.node->shape.type() == ShapeType::Image &&
                detail::try_native_image(ctx, *draw_target, *item.node, state);
            if (!native_image) {
                const auto draw_result = ctx.backend->draw_node(
                    *draw_target, *item.node, state, ctx.frame_input.camera,
                    ctx.frame_input.width, ctx.frame_input.height);
                if (!draw_result.ok()) {
                    return NodeExecResult{NodeExecutionError{
                        draw_result.error().code,
                        m_name,
                        draw_result.error().message}};
                }
            }
        }

        if (ctx.policy.diagnostics_enabled) {
            spdlog::info(
                "[AE_CAM] frame={} node='{}' item#{} world=({},{},{}) screen=({},{}) depth={} scale={} visible={}",
                ctx.frame_input.sample_time.integral_frame(),
                m_name,
                i,
                item.matrix[3][0], item.matrix[3][1], item.matrix[3][2],
                state.matrix[3][0], state.matrix[3][1],
                state.matrix[3][2],
                glm::length(Vec3(item.matrix[0])),
                true
            );
        }
    }

    if (!flush_groups_to(0))
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::ExecutionFailure,
            m_name,
            group_failure.empty() ? "svg_group_stage_failure" : group_failure
        }};

    fb->set_opaque(false);
    return NodeExecResult{std::move(fb)};
}

} // namespace chronon3d::graph
