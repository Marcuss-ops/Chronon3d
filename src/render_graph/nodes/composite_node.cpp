// ============================================================================
// composite_node.cpp — CompositeNode::execute() implementation.
//
// Extracted from composite_node.hpp so the public header doesn't need
// to #include <spdlog/spdlog.h>.
// ============================================================================

#include <chronon3d/render_graph/nodes/composite_node.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>
#include <cmath>
#include <limits>

#include "native_surface.hpp"

namespace chronon3d::graph {

namespace {

NodeExecResult execute_backdrop_software(
    RenderGraphContext& ctx, const Framebuffer& bottom, const Framebuffer& top,
    const BackdropBlurParams& settings, BlendMode mode) {
    if (ctx.policy.require_native_gpu)
        return NodeExecutionError{RenderBackendErrorCode::ExecutionFailure,
            "CompositeNode", "backdrop blur requires software composition; strict GPU-native mode rejects this layer"};
    if (!bottom.is_cpu_authoritative() || !top.is_cpu_authoritative() ||
        bottom.width() != ctx.frame_input.width || bottom.height() != ctx.frame_input.height ||
        bottom.origin_x() != 0 || bottom.origin_y() != 0)
        return NodeExecutionError{RenderBackendErrorCode::ExecutionFailure,
            "CompositeNode", "software backdrop blur requires CPU-resident full-canvas inputs"};

    const i32 width = bottom.width();
    const i32 height = bottom.height();
    const std::size_t count = static_cast<std::size_t>(width) * height;
    std::vector<Color> source(count), horizontal(count), blurred(count);
    for (i32 y = 0; y < height; ++y) {
        const auto* row = bottom.pixels_row(y);
        std::copy_n(row, width, source.data() + static_cast<std::size_t>(y) * width);
    }

    const int extent = static_cast<int>(std::ceil(settings.blur));
    if (extent > 0) {
        const float sigma = std::max(settings.blur / 3.0f, 0.5f);
        std::vector<float> kernel(static_cast<std::size_t>(extent * 2 + 1));
        float weight_sum = 0.0f;
        for (int k = -extent; k <= extent; ++k) {
            const float value = std::exp(-0.5f * (static_cast<float>(k) / sigma) *
                                         (static_cast<float>(k) / sigma));
            kernel[static_cast<std::size_t>(k + extent)] = value;
            weight_sum += value;
        }
        for (auto& value : kernel) value /= weight_sum;
        const auto sample = [&](const std::vector<Color>& input, i32 x, i32 y) {
            x = std::clamp(x, 0, width - 1);
            y = std::clamp(y, 0, height - 1);
            return input[static_cast<std::size_t>(y) * width + x];
        };
        for (i32 y = 0; y < height; ++y) for (i32 x = 0; x < width; ++x) {
            Color out{0, 0, 0, 0};
            for (int k = -extent; k <= extent; ++k) {
                const Color c = sample(source, x + k, y);
                const float w = kernel[static_cast<std::size_t>(k + extent)];
                out.r += c.r * w; out.g += c.g * w; out.b += c.b * w; out.a += c.a * w;
            }
            horizontal[static_cast<std::size_t>(y) * width + x] = out;
        }
        for (i32 y = 0; y < height; ++y) for (i32 x = 0; x < width; ++x) {
            Color out{0, 0, 0, 0};
            for (int k = -extent; k <= extent; ++k) {
                const Color c = sample(horizontal, x, y + k);
                const float w = kernel[static_cast<std::size_t>(k + extent)];
                out.r += c.r * w; out.g += c.g * w; out.b += c.b * w; out.a += c.a * w;
            }
            blurred[static_cast<std::size_t>(y) * width + x] = out;
        }
    } else {
        blurred = source;
    }

    float max_alpha = 0.0f;
    for (i32 y = 0; y < top.height(); ++y) {
        const auto* row = top.pixels_row(y);
        for (i32 x = 0; x < top.width(); ++x) max_alpha = std::max(max_alpha, row[x].a);
    }
    auto output = ctx.acquire_owned_fb(width, height, false);
    for (i32 y = 0; y < height; ++y) {
        Color* dst = output->pixels_row(y);
        const Color* top_row = y >= top.origin_y() && y < top.origin_y() + top.height()
            ? top.pixels_row(y - top.origin_y()) : nullptr;
        for (i32 x = 0; x < width; ++x) {
            Color backdrop = source[static_cast<std::size_t>(y) * width + x];
            float coverage = 0.0f;
            if (top_row && x >= top.origin_x() && x < top.origin_x() + top.width() && max_alpha > 0.0f)
                coverage = std::clamp(top_row[x - top.origin_x()].a / max_alpha, 0.0f, 1.0f);
            Color glass = blurred[static_cast<std::size_t>(y) * width + x];
            if (glass.a > 1e-6f) {
                float r = glass.r / glass.a * settings.brightness;
                float g = glass.g / glass.a * settings.brightness;
                float b = glass.b / glass.a * settings.brightness;
                const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                r = luma + (r - luma) * settings.saturation;
                g = luma + (g - luma) * settings.saturation;
                b = luma + (b - luma) * settings.saturation;
                glass.r = r * glass.a; glass.g = g * glass.a; glass.b = b * glass.a;
            }
            backdrop.r = backdrop.r * (1.0f - coverage) + glass.r * coverage;
            backdrop.g = backdrop.g * (1.0f - coverage) + glass.g * coverage;
            backdrop.b = backdrop.b * (1.0f - coverage) + glass.b * coverage;
            backdrop.a = backdrop.a * (1.0f - coverage) + glass.a * coverage;
            if (top_row && x >= top.origin_x() && x < top.origin_x() + top.width())
                backdrop = compositor::blend(top_row[x - top.origin_x()], backdrop, mode);
            dst[x] = backdrop;
        }
    }
    output->set_origin(0, 0);
    if (ctx.node_exec.counters)
        ctx.node_exec.counters->full_frame_passes.fetch_add(1, std::memory_order_relaxed);
    return NodeExecResult{std::move(output)};
}

// ── try_native_affine_composite ──────────────────────────────────────────
//
// Composite `source` onto `destination` when the two surfaces differ in
// size or origin.  Uses `transform_surface_affine` — the direct GPU path
// that handles arbitrary affine transforms without intermediate surfaces.
// Returns true when the native composite succeeded; false when the caller
// should fall back to the CPU path.
bool try_native_affine_composite(
    RenderGraphContext& ctx, Framebuffer& destination,
    const Framebuffer& source, const std::optional<raster::BBox>& clip) {
    if (!ctx.backend || !ctx.surface_registry ||
        (source.width() == destination.width() &&
         source.height() == destination.height() &&
         source.origin_x() == destination.origin_x() &&
         source.origin_y() == destination.origin_y())) {
        return false;
    }
    const auto original_destination = destination.surface_handle();
    if (!ensure_native_surface(ctx, destination, "CompositeNode.affine.destination")) return false;

    OwnedFB source_copy;
    runtime::RenderSurfaceHandle source_handle = source.surface_handle();
    if (source_handle == runtime::kInvalidRenderSurfaceHandle) {
        source_copy = ctx.acquire_owned_fb(source);
        if (!ensure_native_surface(ctx, *source_copy, "CompositeNode.affine.source_copy")) {
            if (original_destination == runtime::kInvalidRenderSurfaceHandle) {
                release_native_surface(ctx, destination);
            }
            return false;
        }
        source_handle = source_copy->surface_handle();
    }

    const float dx = static_cast<float>(source.origin_x() - destination.origin_x());
    const float dy = static_cast<float>(source.origin_y() - destination.origin_y());
    const float src_w = static_cast<float>(source.width());
    const float src_h = static_cast<float>(source.height());

    runtime::SurfaceAffineTransform transform{};
    // The shader evaluates the source equation in canvas coordinates.  Do
    // not use the destination-relative delta here: destination_origin is
    // added by the shader before evaluation, so the source surface origin is
    // the only offset that belongs in the equation.
    runtime::set_canvas_source_mapping(
        transform,
        1.0f, 0.0f, -static_cast<float>(source.origin_x()),
        0.0f, 1.0f, -static_cast<float>(source.origin_y()),
        runtime::RectF{0.0f, 0.0f, src_w, src_h});
    transform.opacity = 1.0f;
    transform.bilinear = 0u; // Direct 1:1 pixel sampling
    // The destination is seeded with the canvas background. Preserve those
    // pixels where the projected source has no coverage (fit=contain bars).
    transform.blend_source_over = 1u;
    transform.destination_origin_x = destination.origin_x();
    transform.destination_origin_y = destination.origin_y();

    std::int32_t eff_x0 = static_cast<std::int32_t>(dx);
    std::int32_t eff_y0 = static_cast<std::int32_t>(dy);
    std::int32_t eff_x1 = static_cast<std::int32_t>(dx + src_w);
    std::int32_t eff_y1 = static_cast<std::int32_t>(dy + src_h);

    if (clip) {
        const runtime::SurfaceCoordinateMapper mapper{
            destination.origin_x(), destination.origin_y()};
        const auto local_clip = mapper.canvas_to_surface(runtime::RectF{
            static_cast<float>(clip->x0), static_cast<float>(clip->y0),
            static_cast<float>(clip->x1), static_cast<float>(clip->y1)});
        const std::int32_t local_clip_x0 = static_cast<std::int32_t>(local_clip.x0);
        const std::int32_t local_clip_y0 = static_cast<std::int32_t>(local_clip.y0);
        const std::int32_t local_clip_x1 = static_cast<std::int32_t>(local_clip.x1);
        const std::int32_t local_clip_y1 = static_cast<std::int32_t>(local_clip.y1);

        eff_x0 = std::max(eff_x0, local_clip_x0);
        eff_y0 = std::max(eff_y0, local_clip_y0);
        eff_x1 = std::min(eff_x1, local_clip_x1);
        eff_y1 = std::min(eff_y1, local_clip_y1);
    }

    if (eff_x1 <= eff_x0 || eff_y1 <= eff_y0) {
        if (source_copy) release_native_surface(ctx, *source_copy);
        return true;
    }

    transform.clip_enabled = 1u;
    transform.clip_rect[0] = eff_x0;
    transform.clip_rect[1] = eff_y0;
    transform.clip_rect[2] = eff_x1;
    transform.clip_rect[3] = eff_y1;

    const auto composited = ctx.backend->transform_surface_affine(
        destination.surface_handle(), source_handle, transform);
    if (source_copy) release_native_surface(ctx, *source_copy);
    if (!composited.ok()) {
        if (original_destination == runtime::kInvalidRenderSurfaceHandle) {
            release_native_surface(ctx, destination);
        }
        return false;
    }
    return true;
}

bool try_native_composite(RenderGraphContext& ctx, Framebuffer& destination,
                          Framebuffer& source, BlendMode mode,
                          CompositeOperator op,
                          const std::optional<raster::BBox>& clip) {
    // Vulkan's surface compositor supports both the ordinary source-over
    // path and additive overlays. Keeping Add on the CPU fallback would call
    // composite_layer(), whose legacy contract intentionally rejects it.
    if ((mode != BlendMode::Normal && mode != BlendMode::Add) ||
        op != CompositeOperator::SourceOver ||
        source.width() != destination.width() ||
        source.height() != destination.height() ||
        source.origin_x() != destination.origin_x() ||
        source.origin_y() != destination.origin_y()) {
        return false;
    }
    const auto original_handle = destination.surface_handle();
    const auto original_source_handle = source.surface_handle();
    if (!ensure_native_surface(ctx, destination, "CompositeNode.destination") ||
        !ensure_native_surface(ctx, source, "CompositeNode.source")) {
        if (original_handle == runtime::kInvalidRenderSurfaceHandle) {
            release_native_surface(ctx, destination);
        }
        if (original_source_handle == runtime::kInvalidRenderSurfaceHandle) {
            release_native_surface(ctx, source);
        }
        return false;
    }
    const auto result = ctx.backend->composite_surfaces(
        destination.surface_handle(), source.surface_handle(), mode, op, clip);
    if (result.ok()) {
        return true;
    }
    if (original_handle == runtime::kInvalidRenderSurfaceHandle) {
        release_native_surface(ctx, destination);
    }
    if (original_source_handle == runtime::kInvalidRenderSurfaceHandle) {
        release_native_surface(ctx, source);
    }
    return false;
}

// seed_native_destination() replaces the destination pixels with a same-size
// copy from the source.  This is deliberately routed through copy_surface,
// not transform_surface_affine: the seed is a full-frame 1:1 copy and the
// Vulkan copy/composite kernel is the native path that also works when the
// destination is part of the CUDA/NVENC surface chain.
bool seed_native_destination(
    RenderGraphContext& ctx, Framebuffer& destination,
    const Framebuffer& source) {
    if (!ctx.backend || !ctx.surface_registry) {
        return false;
    }
    const auto original_destination = destination.surface_handle();
    // The CPU copy made by acquire_owned_fb is always a valid source of
    // truth. If the bottom did not retain a native handle, promote the result
    // directly instead of rejecting the whole frame.
    if (source.surface_handle() == runtime::kInvalidRenderSurfaceHandle ||
        !ctx.backend->is_native_surface_valid(source.surface_handle())) {
        return ensure_native_surface(ctx, destination,
                                     "CompositeNode.seed.source-fallback");
    }
    if (!ensure_empty_native_surface(ctx, destination)) {
        // The CPU framebuffer is still authoritative at this point. Promote
        // it directly so native NVENC can proceed even when a backend cannot
        // allocate an empty same-size target during startup.
        if (original_destination == runtime::kInvalidRenderSurfaceHandle) {
            destination.clear_surface_handle();
        }
        return ensure_native_surface(ctx, destination,
                                     "CompositeNode.seed.fallback");
    }

    // `copy_surface` maps to replace-mode composite on Vulkan. It preserves
    // the source pixels exactly and avoids affine sampling/coordinate setup.
    const auto seeded = ctx.backend->copy_surface(
        destination.surface_handle(), source.surface_handle(), std::nullopt);
    if (!seeded.ok()) {
        if (original_destination == runtime::kInvalidRenderSurfaceHandle) {
            release_native_surface(ctx, destination);
        }
        // A rejected seed is a hard error at the only call site (see below):
        // on a native backend, composite of a canvas-sized SourceOver chain is
        // fail-closed and CPU fallback is forbidden by policy, so a per-frame
        // warn here would spam without changing the outcome.  Log the first
        // occurrences and roll up at 1000 boundaries instead.
        static std::atomic<std::uint64_t> g_seed_failures{0};
        const std::uint64_t nth =
            g_seed_failures.fetch_add(1, std::memory_order_relaxed) + 1;
        if (nth <= 3 || nth % 1000 == 0) {
            spdlog::warn(
                "[native-seed] transform_surface_affine rejected by backend "
                "(failure #{}): {}",
                nth, seeded.error().message);
        }
        // Some Vulkan drivers reject a replace-copy while the destination is
        // being imported into the CUDA/NVENC chain. The CPU pixels in
        // `destination` are valid (acquire_owned_fb copied them above), so
        // promote those pixels as a correctness-preserving native fallback.
        if (original_destination == runtime::kInvalidRenderSurfaceHandle) {
            release_native_surface(ctx, destination);
        }
        return ensure_native_surface(ctx, destination,
                                     "CompositeNode.seed.copy-fallback");
    }
    return true;
}

// publish_dof_provenance() — single authority for DOF depth/coverage
// publication.  Previously this loop existed twice verbatim (pass-through
// branch and post-blend branch); the two copies had to be kept in sync by
// discipline.  Writes world-z into the depth buffer for every covered pixel
// and expands the DOF source bbox/radius coverage.
void publish_dof_provenance(
    RenderGraphContext& ctx, const Framebuffer& surface,
    float world_z, float source_radius,
    const std::optional<raster::BBox>& clip,
    i32 framebuffer_width, i32 framebuffer_height) {
    auto& dof_depth = ctx.node_exec.dof_depth_buffer();
    if (dof_depth.empty()) return;
    const i32 w = framebuffer_width;
    const i32 bx0 = clip ? clip->x0 : 0;
    const i32 by0 = clip ? clip->y0 : 0;
    const i32 bx1 = clip ? clip->x1 : framebuffer_width;
    const i32 by1 = clip ? clip->y1 : framebuffer_height;
    for (i32 y = by0; y < by1; ++y) {
        const i32 sy = y - surface.origin_y();
        if (sy < 0 || sy >= surface.height()) continue;
        const Color* row = surface.pixels_row(sy);
        for (i32 x = bx0; x < bx1; ++x) {
            const i32 sx = x - surface.origin_x();
            if (sx < 0 || sx >= surface.width() || row[sx].a <= 0.01f) continue;
            dof_depth[static_cast<size_t>(y) * w + x] = world_z;
            if (std::isfinite(source_radius) && source_radius >= 0.5f) {
                auto& coverage = ctx.node_exec.dof_sources();
                if (!coverage.source_bbox) {
                    coverage.source_bbox = raster::BBox{x, y, x + 1, y + 1};
                } else {
                    auto& bbox = *coverage.source_bbox;
                    bbox.x0 = std::min(bbox.x0, x);
                    bbox.y0 = std::min(bbox.y0, y);
                    bbox.x1 = std::max(bbox.x1, x + 1);
                    bbox.y1 = std::max(bbox.y1, y + 1);
                }
                coverage.max_radius = std::max(coverage.max_radius, source_radius);
            }
        }
    }
}

// dof_source_radius() — shared radius derivation for both DOF publish sites.
float dof_source_radius(RenderGraphContext& ctx, float world_z) {
    const auto& camera_2_5d = ctx.frame_input.camera_2_5d;
    return camera_2_5d.dof.enabled
        ? compute_dof_blur_radius(camera_2_5d.dof, camera_2_5d.lens,
                                  world_z,
                                  static_cast<float>(ctx.frame_input.width))
        : 0.0f;
}

// materialize_native_or_fail() — the ensure(destination)+ensure(source)+
// fail-closed dance shared by the operator and fallback composite branches.
// On a native backend a materialization failure is a hard error: the GPU
// program must never silently degrade to a CPU blend (P0.2 contract).
[[nodiscard]] std::optional<NodeExecutionError> materialize_native_or_fail(
    RenderGraphContext& ctx, Framebuffer& destination,
    const Framebuffer& source, std::string_view context) {
    if (!ctx.backend || !ctx.surface_registry ||
        !ctx.backend->supports_native_surfaces()) {
        return std::nullopt;
    }
    const bool destination_ready =
        ensure_native_surface(ctx, destination, "CompositeNode.destination");
    auto& mutable_source = const_cast<Framebuffer&>(source);
    const bool source_ready =
        ensure_native_surface(ctx, mutable_source, "CompositeNode.source");
    if (!destination_ready || !source_ready) {
        return NodeExecutionError{
            RenderBackendErrorCode::ExecutionFailure,
            "CompositeNode",
            std::string(context) +
            " could not materialize native surfaces; CPU fallback is forbidden"};
    }
    return std::nullopt;
}

} // namespace

NodeExecResult CompositeNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes
) {
    // An upstream node can fail before this level is scheduled.  The
    // executor still drains dependent nodes so the shared frame error can be
    // observed consistently; never dereference a missing input while doing
    // so.  Returning the latched upstream error preserves the original
    // failure at the frame boundary instead of turning it into SIGSEGV.
    if (ctx.frame_error && ctx.frame_error->has_value()) {
        return NodeExecResult{ctx.frame_error->value()};
    }

    const FramebufferRef empty_fb{};
    const FramebufferRef& in0 = inputs.size() >= 1 ? inputs[0] : empty_fb;
    const FramebufferRef& in1 = inputs.size() >= 2 ? inputs[1] : empty_fb;

    if (!in0 && !in1) {
        if (inputs.empty()) {
            auto fallback = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
            return NodeExecResult{std::move(fallback)};
        }
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::InvalidInput,
            "Composite",
            "composite input framebuffer is missing"}};
    }

    const FramebufferRef& bottom = in0 ? in0 : in1;
    const FramebufferRef& top = in0 ? in1 : empty_fb;

    if (m_backdrop_blur) {
        if (!top || m_operator != CompositeOperator::SourceOver || m_mode != BlendMode::Normal)
            return NodeExecutionError{RenderBackendErrorCode::InvalidInput,
                "CompositeNode", "backdrop blur requires a normal source-over shape layer"};
        if (ctx.backend && ctx.surface_registry && ctx.backend->supports_native_surfaces() &&
            m_backdrop_blur->brightness == 1.0f && m_backdrop_blur->saturation == 1.0f &&
            bottom->width() == ctx.frame_input.width && bottom->height() == ctx.frame_input.height &&
            top->width() == bottom->width() && top->height() == bottom->height() &&
            bottom->origin_x() == 0 && bottom->origin_y() == 0 &&
            top->origin_x() == 0 && top->origin_y() == 0) {
            auto result = ctx.acquire_owned_fb(*bottom);
            auto horizontal = ctx.acquire_owned_fb(bottom->width(), bottom->height(), false);
            auto blurred = ctx.acquire_owned_fb(bottom->width(), bottom->height(), false);
            auto masked = ctx.acquire_owned_fb(bottom->width(), bottom->height(), false);
            if (!ensure_native_surface(ctx, *result, "CompositeNode.backdrop.destination") ||
                !ensure_native_surface(ctx, *horizontal, "CompositeNode.backdrop.horizontal") ||
                !ensure_native_surface(ctx, *blurred, "CompositeNode.backdrop.blurred") ||
                !ensure_native_surface(ctx, *masked, "CompositeNode.backdrop.masked") ||
                !ensure_native_surface(ctx, const_cast<Framebuffer&>(*top), "CompositeNode.backdrop.mask"))
                return NodeExecutionError{RenderBackendErrorCode::ExecutionFailure,
                    "CompositeNode", "failed to materialize backdrop blur surfaces"};
            const auto h = ctx.backend->blur_surface(horizontal->surface_handle(),
                bottom->surface_handle(), m_backdrop_blur->blur, true);
            const auto v = h.ok() ? ctx.backend->blur_surface(blurred->surface_handle(),
                horizontal->surface_handle(), m_backdrop_blur->blur, false) : h;
            const auto m = v.ok() ? ctx.backend->matte_surface(masked->surface_handle(),
                blurred->surface_handle(), top->surface_handle(), false, false) : v;
            const auto c = m.ok() ? ctx.backend->composite_surfaces(result->surface_handle(),
                masked->surface_handle(), BlendMode::Normal, CompositeOperator::SourceOver, std::nullopt) : m;
            const auto overlay = c.ok() ? ctx.backend->composite_surfaces(result->surface_handle(),
                top->surface_handle(), BlendMode::Normal, CompositeOperator::SourceOver, std::nullopt) : c;
            if (!overlay.ok())
                return NodeExecutionError{RenderBackendErrorCode::ExecutionFailure,
                    "CompositeNode", "native backdrop blur operation failed"};
            result->mark_gpu_authoritative();
            return NodeExecResult{std::move(result)};
        }
        return execute_backdrop_software(ctx, *bottom, *top, *m_backdrop_blur, m_mode);
    }

    // Which inputs the compiler actually wired here. A Composite with a single
    // input is a pass-through (the `!top` early return below), so this line is
    // what separates "the layer was never wired" from "the layer was wired and
    // the blend dropped it" — the two failures look identical in the output.
    if (native_surface_promotion_diag_enabled()) {
        spdlog::info(
            "[composite-inputs] frame={} inputs={} in0={} in1={} "
            "bottom={}x{} top={}x{} mode={}",
            static_cast<int>(ctx.frame_input.frame), inputs.size(),
            in0 ? 1 : 0, in1 ? 1 : 0,
            bottom ? bottom->width() : -1, bottom ? bottom->height() : -1,
            top ? top->width() : -1, top ? top->height() : -1,
            static_cast<int>(m_mode));
    }


    if (ctx.policy.diagnostics_enabled) {
        spdlog::info(
            "[dirty-debug] frame={} Composite mode={} bottom_opaque={} top_opaque={} clip=[{}:{} -> {}:{}]",
            static_cast<int>(ctx.frame_input.frame),
            static_cast<int>(m_mode),
            bottom ? (bottom->is_opaque() ? 1 : 0) : 0,
            top ? (top->is_opaque() ? 1 : 0) : 0,
            ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->x0 : 0,
            ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->y0 : 0,
            ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->x1 : ctx.frame_input.width,
            ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->y1 : ctx.frame_input.height
        );
    }

    // Skip-opaque optimization — only applies to SourceOver + Normal blend
    if (top && m_operator == CompositeOperator::SourceOver &&
        m_mode == BlendMode::Normal && top->is_opaque() &&
        top->is_cpu_authoritative() &&
        input_bboxes.size() >= 2 && input_bboxes[1].has_value())
    {
        const auto& tb = *input_bboxes[1];
        if (tb.x0 <= 0 && tb.y0 <= 0 && tb.x1 >= ctx.frame_input.width && tb.y1 >= ctx.frame_input.height) {
            if (!ctx.node_exec.clip_rect ||
                (ctx.node_exec.clip_rect->x0 <= 0 && ctx.node_exec.clip_rect->y0 <= 0 &&
                 ctx.node_exec.clip_rect->x1 >= ctx.frame_input.width && ctx.node_exec.clip_rect->y1 >= ctx.frame_input.height))
            {
                if (ctx.node_exec.counters) {
                    ctx.node_exec.counters->composite_calls.fetch_add(1, std::memory_order_relaxed);
                    const uint64_t area = static_cast<uint64_t>(ctx.frame_input.width) * static_cast<uint64_t>(ctx.frame_input.height);
                    ctx.node_exec.counters->composite_copy_pixels.fetch_add(area, std::memory_order_relaxed);
                    // F3.2 (TICKET-GLOW-FULLFRAME-AUDIT-V1) — skip-opaque
                    // optimization is a full-frame pass.  The top input may
                    // be a cached framebuffer, so it must remain immutable;
                    // do not swap its pixel storage into the result.
                    ctx.node_exec.counters->full_frame_passes.fetch_add(1, std::memory_order_relaxed);
                }
                auto result = ctx.acquire_owned_fb(top->width(), top->height(), false);
                for (i32 y = 0; y < top->height(); ++y) {
                    std::memcpy(result->pixels_row(y), top->pixels_row(y),
                                static_cast<std::size_t>(top->width()) * sizeof(Color));
                }
                result->set_origin(top->origin_x(), top->origin_y());
                result->set_opaque(true);
                return NodeExecResult{std::move(result)};
            }
        }
    }

    // ── Acquire output framebuffer ────────────────────────────────────
    //
    // The existing acquire_owned_fb(const Framebuffer&) overload already
    // implements zero-copy via scratch.reusable_inputs when the input has
    // sole ownership (consumer_remaining==1 && use_count==1).  When the
    // bottom is reusable, it swaps pixel storage without copying ~8MB.
    //
    // NOTE: in a chain of composites the CachedFB often has use_count>1
    // because the node_cache holds a reference, so the reusable path is
    // skipped and a full copy occurs.  This is mitigated by increasing
    // the framebuffer pool budget (1 GB) and max_buffers_per_size_class
    // (8) to reduce pool thrashing + eviction pressure — the root cause
    // of the 70.9K ms compositenode_acquire_wall_ms bottleneck.
    const auto t_acquire0 = profiling::now();
    const bool bottom_matches_canvas =
        bottom->width() == ctx.frame_input.width &&
        bottom->height() == ctx.frame_input.height;
    OwnedFB result;
    if (bottom_matches_canvas) {
        const bool native_backend =
            ctx.backend && ctx.surface_registry &&
            ctx.backend->supports_native_surfaces();
        auto& mutable_bottom = const_cast<Framebuffer&>(*bottom);
        bool bottom_native_valid =
            bottom->surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
            native_backend &&
            ctx.backend->is_native_surface_valid(bottom->surface_handle());
        if (native_backend && !bottom_native_valid) {
            if (!ensure_native_surface(ctx, mutable_bottom, "CompositeNode.bottom")) {
                return NodeExecutionError{
                    RenderBackendErrorCode::ExecutionFailure,
                    "CompositeNode",
                    "failed to materialize native bottom surface"};
            }
            bottom_native_valid = true;
        }
        result = ctx.acquire_owned_fb(*bottom);
        if (native_backend && result->surface_handle() != bottom->surface_handle()) {
            if (!seed_native_destination(ctx, *result, *bottom)) {
                return NodeExecutionError{
                    RenderBackendErrorCode::ExecutionFailure,
                    "CompositeNode",
                    "failed to seed native destination from bottom surface"};
            }
        }
    } else {
        result = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height, true);
        if (ctx.backend) {
            if (!try_native_affine_composite(ctx, *result, *bottom, std::nullopt)) {
                // try_native_affine_composite returns false both when the
                // native affine path is unavailable (software backend, or
                // source/destination already same size) and when it failed.
                // Materialize native surfaces when the backend owns them;
                // then fall back to the CPU composite_layer.  Only a strict
                // require_native_gpu policy turns materialization failure
                // into a hard error.  (Regression from the Sep-5 TU split
                // 39a5c03c9: it deleted this CPU fallback, making every
                // software dimension-mismatch composite — e.g. shadow
                // receivers smaller than the canvas — fail hard.)
                if (ctx.surface_registry &&
                    ctx.backend->supports_native_surfaces()) {
                    const bool destination_ready = ensure_native_surface(
                        ctx, *result, "CompositeNode.dimension.destination");
                    auto& mutable_bottom = const_cast<Framebuffer&>(*bottom);
                    const bool source_ready = ensure_native_surface(
                        ctx, mutable_bottom, "CompositeNode.dimension.source");
                    if (ctx.policy.require_native_gpu &&
                        (!destination_ready || !source_ready)) {
                        return NodeExecutionError{
                            RenderBackendErrorCode::ExecutionFailure,
                            "CompositeNode",
                            "native residency violation while materializing "
                            "dimension-mismatched inputs"};
                    }
                }
                ctx.backend->composite_layer(
                    *result, *bottom, BlendMode::Normal);
            }
        }
        // F3.2 — size mismatch forces a full-canvas composite (every pixel
        // touched). Surface as a full-frame pass. The byte side-cost is
        // captured separately by framebuffer_copy_wall_ms when the pool
        // returns a re-used allocation that demands std::copy.
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->full_frame_passes.fetch_add(1, std::memory_order_relaxed);
        }
    }
    const auto t_acquire1 = profiling::now();
    if (ctx.node_exec.counters) {
        const auto acquire_ms = static_cast<uint64_t>(
            profiling::duration_ms(t_acquire0, t_acquire1));
        ctx.node_exec.counters->compositenode_acquire_wall_ms.fetch_add(acquire_ms, std::memory_order_relaxed);
    }
    // Note: when bottom doesn't fit (rare), the bottom composite_layer
    // call is included in this timing AND already tracked in
    // compositenode_blend_wall_ms.  This minor double-counting is acceptable
    // because the common path (bottom fits, >=99% of frames) is pure.

    // Start dispatch timing here — AFTER acquire and optional bottom composite,
    // so dispatch_ms measures only clip/bbox computation + overhead, without
    // double-counting bottom composite time (already in compositenode_blend_wall_ms).
    if (!top) {
        if (ctx.policy.track_dof_depth) {
            publish_dof_provenance(
                ctx, *result, m_world_z,
                dof_source_radius(ctx, m_world_z),
                ctx.node_exec.clip_rect,
                ctx.frame_input.width, ctx.frame_input.height);
        }
        return NodeExecResult{std::move(result)};
    }

    const auto t_dispatch0 = profiling::now();

    if (ctx.backend) {
        std::optional<raster::BBox> clip = (input_bboxes.size() >= 2) ? input_bboxes[1] : std::nullopt;
        if (ctx.node_exec.clip_rect) {
            if (clip) {
                clip = raster::BBox{
                    .x0 = std::max(clip->x0, ctx.node_exec.clip_rect->x0),
                    .y0 = std::max(clip->y0, ctx.node_exec.clip_rect->y0),
                    .x1 = std::min(clip->x1, ctx.node_exec.clip_rect->x1),
                    .y1 = std::min(clip->y1, ctx.node_exec.clip_rect->y1)
                };
                if (clip->x0 >= clip->x1 || clip->y0 >= clip->y1) {
                    clip = raster::BBox{0, 0, 0, 0};
                }
            } else {
                clip = ctx.node_exec.clip_rect;
            }
        }

        // Declare these in the backend scope so the aggregate non-blend
        // calculation below can reference them regardless of whether the
        // inner telemetry blocks are entered.
        // Use auto (same as rest of codebase) — profiling::now() returns
        // a POD timestamp, default-constructed without clock syscall.
        auto _de = profiling::now(); // dispatch end
        auto _os = profiling::now(); // overhead start
        auto _oe = _os;              // overhead end

        // Record dispatch time before composite_layer call.
        if (ctx.node_exec.counters) {
            _de = profiling::now();
            const auto dms = static_cast<uint64_t>(profiling::duration_ms(t_dispatch0, _de));
            ctx.node_exec.counters->compositenode_dispatch_wall_ms.fetch_add(dms, std::memory_order_relaxed);
        }

        // Check if stencil/silhouette operator — these use the underlying
        // composite operator field instead of a blend mode when applicable.
        const bool native_composite = try_native_composite(
            ctx, *result, *top, m_mode, m_operator, clip);
        const bool native_dimension_composite = !native_composite &&
            m_mode == BlendMode::Normal &&
            m_operator == CompositeOperator::SourceOver &&
            try_native_affine_composite(ctx, *result, *top, clip);
        if (ctx.policy.require_native_gpu &&
            !native_composite && !native_dimension_composite) {
            spdlog::error(
                "[native-residency] CompositeNode cannot keep frame {} on GPU "
                "(destination={} source={})",
                static_cast<int>(ctx.frame_input.frame),
                result->surface_handle(), top->surface_handle());
            return NodeExecutionError{
                RenderBackendErrorCode::ExecutionFailure,
                "CompositeNode",
                "native residency violation: composite inputs could not be materialized"};
        }
        if (ctx.backend && ctx.backend->supports_native_surfaces() &&
            m_operator == CompositeOperator::SourceOver &&
            !native_composite && !native_dimension_composite) {
            return NodeExecutionError{
                RenderBackendErrorCode::ExecutionFailure,
                "CompositeNode",
                "native SourceOver composite failed; CPU fallback is forbidden"};
        }
        if (native_composite || native_dimension_composite) {
            // The native path has already composed the logical surfaces. The
            // CPU pixels remain a reference snapshot for any legacy consumer;
            // the handle is the authoritative value for subsequent native
            // composite nodes.
            result->mark_gpu_authoritative();
        } else if (m_operator != CompositeOperator::SourceOver) {
            // Stencil/Silhouette: use Normal blend to copy top first, then
            // apply the operator via the backend (which handles the masking).
            // The operator is passed along so the backend can apply the
            // appropriate matte-style coverage to the backdrop.
            if (ctx.backend && ctx.surface_registry &&
                ctx.backend->supports_native_surfaces()) {
                if (auto err = materialize_native_or_fail(
                        ctx, *result, *top,
                        "non-SourceOver composite")) {
                    return *err;
                }
            }
            ctx.backend->composite_layer(*result, *top, m_mode, clip, m_operator);
            if (result->surface_handle() != runtime::kInvalidRenderSurfaceHandle)
                result->mark_gpu_authoritative();
    } else {
        // VulkanBackend::composite_layer is a native primitive even when the
        // fast-path probe above declined the optimized SourceOver route
        // (for example Add/stencil operators).  Materialize both handles
        // before dispatching that fallback; otherwise the backend receives
        // the framebuffer sentinel 0 and fails later in resolve_image().
        if (ctx.backend && ctx.surface_registry &&
            ctx.backend->supports_native_surfaces()) {
            if (auto err = materialize_native_or_fail(
                    ctx, *result, *top, "composite fallback")) {
                return *err;
            }
        }
        ctx.backend->composite_layer(*result, *top, m_mode, clip);
        if (result->surface_handle() != runtime::kInvalidRenderSurfaceHandle)
            result->mark_gpu_authoritative();
    }

        // ── Post-blend overhead ────────────────────────────────────────
        _os = profiling::now();

        auto& dof_depth = ctx.node_exec.dof_depth_buffer();
        if (ctx.policy.track_dof_depth && !dof_depth.empty()) {
            publish_dof_provenance(
                ctx, *top, m_world_z, dof_source_radius(ctx, m_world_z),
                clip, ctx.frame_input.width, ctx.frame_input.height);
        }

        if (m_mode == BlendMode::Normal && top->is_opaque() &&
            input_bboxes.size() >= 2 && input_bboxes[1].has_value() &&
            input_bboxes[1]->x0 <= 0 && input_bboxes[1]->y0 <= 0 &&
            input_bboxes[1]->x1 >= ctx.frame_input.width && input_bboxes[1]->y1 >= ctx.frame_input.height &&
            (!ctx.node_exec.clip_rect ||
             (ctx.node_exec.clip_rect->x0 <= 0 && ctx.node_exec.clip_rect->y0 <= 0 &&
              ctx.node_exec.clip_rect->x1 >= ctx.frame_input.width && ctx.node_exec.clip_rect->y1 >= ctx.frame_input.height)))
        {
            result->set_opaque(true);
        }

        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->composite_calls.fetch_add(1, std::memory_order_relaxed);
            uint64_t area = clip ? (static_cast<uint64_t>(std::max(0, clip->x1 - clip->x0)) * std::max(0, clip->y1 - clip->y0))
                                 : static_cast<uint64_t>(ctx.frame_input.width * ctx.frame_input.height);
            if (m_mode == BlendMode::Normal && top->is_opaque()) {
                ctx.node_exec.counters->composite_copy_pixels.fetch_add(area, std::memory_order_relaxed);
            } else {
                ctx.node_exec.counters->composite_pixels.fetch_add(area, std::memory_order_relaxed);
            }
        }

        if (ctx.node_exec.counters) {
            _oe = profiling::now();
            const auto oms = static_cast<uint64_t>(
                profiling::duration_ms(_os, _oe));
            ctx.node_exec.counters->compositenode_overhead_wall_ms.fetch_add(oms, std::memory_order_relaxed);
        }

        // Aggregate non-blend cost: (acquire + dispatch) + overhead.
        // Blend (composite_layer) is excluded because it runs between _de and _os.
        if (ctx.node_exec.counters) {
            const auto nb_work = profiling::duration_us(t_acquire0, _de)
                               + profiling::duration_us(_os, _oe);
            const auto nb_us = static_cast<uint64_t>(std::max(0.0, nb_work));
            ctx.node_exec.counters->compositenode_internal_wall_us.fetch_add(
                nb_us, std::memory_order_relaxed);
        }
    }
    return NodeExecResult{std::move(result)};
}

} // namespace chronon3d::graph
