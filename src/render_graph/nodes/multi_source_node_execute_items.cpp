#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/cache/node_cache_identity_builder.hpp>
#include <chronon3d/core/parallel_tracked.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
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
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace chronon3d::graph::detail {

NodeExecResult execute_multi_source_items(
    RenderGraphContext& ctx, const std::string& node_name,
    const std::vector<MultiSourceItem>& items, OwnedFB fb,
    bool native_group_surfaces) {
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
                !ctx.backend_surface_api()->is_native_surface_valid(destination_handle) ||
                !ctx.backend_surface_api()->is_native_surface_valid(source)) {
                group_failure = "svg_group_native_unsupported: group surface is not GPU resident";
                return false;
            }
            const auto result = ctx.backend_surface_api()->composite_surface_weighted(
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
        // C-2/P4: rows are disjoint — each pixel touches only its own (x,y)
        // on both surfaces — so the blend runs through the canonical
        // parallel wrapper with unchanged per-pixel arithmetic.
        const auto blend_rows = [&](i32 y_begin, i32 y_end) {
            for (i32 y = y_begin; y < y_end; ++y) {
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
        };
        if (height >= 16) {
            parallel_for_tracked(tbb::blocked_range<i32>(0, height),
                [&](const tbb::blocked_range<i32>& range) {
                    blend_rows(range.begin(), range.end());
                });
        } else {
            blend_rows(0, height);
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

    for (std::size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            if (!item.node) continue;

            auto item_groups = item.groups;
            if (item_groups.empty() && item.group) item_groups.push_back(*item.group);
            // SVG and Lottie group opacity keyframes are lowered to the
            // composition's frame coordinates. Sample the evaluated output
            // frame here; sample_time may refer to an underlying media source
            // and can remain at its first frame for generated vector layers.
            const double group_frame = static_cast<double>(
                ctx.frame_input.frame.integral());
            for (auto& group : item_groups) {
                if (!group.opacity_animation) continue;
                const auto& animation = *group.opacity_animation;
                if (animation.end_frame <= animation.start_frame ||
                    !std::isfinite(animation.from) || !std::isfinite(animation.to))
                    return NodeExecResult{NodeExecutionError{
                        RenderBackendErrorCode::InvalidInput, node_name,
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
                    RenderBackendErrorCode::UnsupportedCapability, node_name,
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
                    RenderBackendErrorCode::ExecutionFailure, node_name,
                    group_failure.empty() ? "svg_group_stage_failure" : group_failure}};
            for (std::size_t depth = shared_depth; depth < item_groups.size(); ++depth) {
                const auto& group = item_groups[depth];
                if (group.id == 0 || !std::isfinite(group.opacity) ||
                    group.opacity < 0.0f || group.opacity > 1.0f)
                    return NodeExecResult{NodeExecutionError{
                        RenderBackendErrorCode::InvalidInput, node_name,
                        "svg_group_invalid: id and opacity are outside supported bounds"}};
                if (!native_group_surfaces && fb->data() == nullptr) {
                    spdlog::error(
                        "[multi-source] node='{}': isolated SVG groups need CPU pixel access",
                        node_name);
                    return NodeExecResult{NodeExecutionError{
                        RenderBackendErrorCode::UnsupportedCapability, node_name,
                        "svg_group_native_unsupported"}};
                }
                auto surface = ctx.acquire_owned_fb(
                    ctx.frame_input.width, ctx.frame_input.height, /*clear=*/true);
                if (native_group_surfaces && !ensure_empty_native_surface(ctx, *surface))
                    return NodeExecResult{NodeExecutionError{
                        RenderBackendErrorCode::ExecutionFailure, node_name,
                        "failed to create native SVG group surface"}};
                active_groups.push_back(ActiveGroup{group, std::move(surface)});
                if (ctx.policy.diagnostics_enabled)
                    spdlog::info("[multi-source] node='{}' group={} depth={} alpha={:.4f} begin",
                        node_name, group.id, depth, group.opacity);
            }
            const bool item_is_grouped = !item_groups.empty();
            if (item_is_grouped) {
                if (item.node->shape.type() == ShapeType::TextRun) {
                    // Text runs and isolated SVG groups would need a
                    // different draw route; fail closed until certified.
                    spdlog::error(
                        "[multi-source] node='{}': text run items cannot join "
                        "an isolated SVG group (svg_group_text_run_unsupported)",
                        node_name);
                    return NodeExecResult{NodeExecutionError{
                        RenderBackendErrorCode::UnsupportedCapability,
                        node_name,
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

                if (!ctx.backend_stats_api()->capabilities().text_run) {
                    spdlog::error(
                        "[multi-source] node='{}' contains text run items "
                        "but active backend does not support draw_text_run; "
                        "aborting frame.", node_name);
                    return NodeExecResult{NodeExecutionError{
                        RenderBackendErrorCode::UnsupportedCapability,
                        node_name,
                        "backend does not support draw_text_run"
                    }};
                }
    if (std::getenv("CHRONON3D_PATH_DIAG"))
        spdlog::info("[native-multisource-diag] node={} surface_support={} registry={} surface={} cpu={} items={}",
            node_name, ctx.backend_stats_api()->supports_native_surfaces(), ctx.surface_registry != nullptr,
            fb->surface_handle(), fb->data() != nullptr, items.size());

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
                    node_name,
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
                        node_name,
                        chronon3d::graph::render_backend_error_code_name(result.error().code),
                        result.error().message);
                    return NodeExecResult{NodeExecutionError{
                        result.error().code,
                        node_name,
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
                        node_name, i,
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
                    node_name,
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
                    RenderBackendErrorCode::UnsupportedCapability, node_name,
                    "svg_element_mask_native_unsupported"}};
            auto source_surface = ctx.acquire_owned_fb(
                ctx.frame_input.width, ctx.frame_input.height, /*clear=*/true);
            if (source_surface->data() == nullptr)
                return NodeExecResult{NodeExecutionError{
                    RenderBackendErrorCode::UnsupportedCapability, node_name,
                    "svg_element_mask_requires_software_surface"}};
            const auto source_draw = ctx.backend_frame_api()->draw_node(
                *source_surface, *item.node, state, ctx.frame_input.camera,
                ctx.frame_input.width, ctx.frame_input.height);
            if (!source_draw.ok())
                return NodeExecResult{NodeExecutionError{
                    source_draw.error().code, node_name, source_draw.error().message}};

            auto mask_surface = ctx.acquire_owned_fb(
                ctx.frame_input.width, ctx.frame_input.height, /*clear=*/true);
            if (mask_surface->data() == nullptr)
                return NodeExecResult{NodeExecutionError{
                    RenderBackendErrorCode::UnsupportedCapability, node_name,
                    "svg_element_mask_requires_software_surface"}};
            RenderState mask_state = state;
            mask_state.opacity = 1.0f;
            for (const auto& mask_path : item.node->svg_local_masks) {
                mask_surface->clear(Color::transparent());
                RenderNode mask_node;
                mask_node.name = "svg_element_mask";
                mask_node.shape = Shape{mask_path};
                const auto mask_draw = ctx.backend_frame_api()->draw_node(
                    *mask_surface, mask_node, mask_state, ctx.frame_input.camera,
                    ctx.frame_input.width, ctx.frame_input.height);
                if (!mask_draw.ok())
                    return NodeExecResult{NodeExecutionError{
                        mask_draw.error().code, node_name, mask_draw.error().message}};
                const auto mask_rows = [&](i32 y_begin, i32 y_end) {
                    for (i32 y = y_begin; y < y_end; ++y) {
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
                };
                if (ctx.frame_input.height >= 16) {
                    parallel_for_tracked(
                        tbb::blocked_range<i32>(0, ctx.frame_input.height),
                        [&](const tbb::blocked_range<i32>& range) {
                            mask_rows(range.begin(), range.end());
                        });
                } else {
                    mask_rows(0, ctx.frame_input.height);
                }
            }
            const auto composite_rows = [&](i32 y_begin, i32 y_end) {
                for (i32 y = y_begin; y < y_end; ++y) {
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
            };
            if (ctx.frame_input.height >= 16) {
                parallel_for_tracked(
                    tbb::blocked_range<i32>(0, ctx.frame_input.height),
                    [&](const tbb::blocked_range<i32>& range) {
                        composite_rows(range.begin(), range.end());
                    });
            } else {
                composite_rows(0, ctx.frame_input.height);
            }
        } else {
            const bool native_image = item.node->shape.type() == ShapeType::Image &&
                detail::try_native_image(ctx, *draw_target, *item.node, state);
            if (!native_image) {
                const auto draw_result = ctx.backend_frame_api()->draw_node(
                    *draw_target, *item.node, state, ctx.frame_input.camera,
                    ctx.frame_input.width, ctx.frame_input.height);
                if (!draw_result.ok()) {
                    return NodeExecResult{NodeExecutionError{
                        draw_result.error().code,
                        node_name,
                        draw_result.error().message}};
                }
            }
        }

        if (ctx.policy.diagnostics_enabled) {
            spdlog::info(
                "[AE_CAM] frame={} node='{}' item#{} world=({},{},{}) screen=({},{}) depth={} scale={} visible={}",
                ctx.frame_input.sample_time.integral_frame(),
                node_name,
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
            node_name,
            group_failure.empty() ? "svg_group_stage_failure" : group_failure
        }};

    fb->set_opaque(false);
    return NodeExecResult{std::move(fb)};
}

} // namespace chronon3d::graph::detail
