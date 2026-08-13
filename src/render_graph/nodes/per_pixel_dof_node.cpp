// ============================================================================
// per_pixel_dof_node.cpp — PerPixelDofNode::execute() implementation.
//
// Moved out of the header to break the inline dependency on the software
// backend.  Uses DofKernelInterface from RenderResourceContext.
//
// ── TICKET-PROJECTION-V1: DOF V1 deterministic-result contract ────────────────
//
// The user spec mandates "risultato deterministico" (deterministic result)
// for the DOF V1 pass.  `PerPixelDofNode` is a PURE FUNCTION:
//
//   apply_per_pixel_dof(input_fb, depth_buffer, dof_settings, lens) -> output_fb
//
// Determinism invariants:
//
//   1. No RNG: the blur kernel is a deterministic Gaussian with a
//      per-pixel radius derived from the depth value + DOF settings.
//      There is no `std::rand`, `mt19937`, `uniform_real_distribution`,
//      or temporal noise source.  The same (depth_buffer, dof_settings,
//      lens) tuple ALWAYS produces the same output_fb byte-for-byte.
//
//   2. No temporal drift: the node does not hold any state across
//      `execute()` invocations.  There are no member variables that
//      accumulate, no caches, no last-frame memory, no thread-local
//      state.  Each call is a self-contained pure function of its
//      inputs.
//
//   3. No compilation: the DOF pass is a render-graph node, not a
//      camera compile step.  The graph node is compiled outside the
//      render loop, while execute() reads the already-evaluated camera
//      from the current frame context.  No `compile_camera()` call is
//      allowed inside execute() (same invariant as the
//      motion-blur-no-recompile contract).
//
//   4. No threading-induced non-determinism: the kernel processes
//      pixels in a deterministic order (left-to-right, top-to-bottom
//      per the clip rectangle).  Even with multi-threaded dispatch
//      the output is identical because each pixel's result depends
//      only on the depth buffer + settings (no cross-pixel feedback
//      that would race).
//
// REGRESSION LOCK: `tests/renderer/camera/test_per_pixel_dof.cpp` exercises
// the deterministic contract via the following invariants:
//   - Same inputs → same output (tested by repeating the kernel call
//     and asserting byte-identical output via `memcmp`).
//   - Empty framebuffer (all transparent) survives blur (no NaN/Inf
//     injected from empty depth data).
//   - 1x1 framebuffer does not crash (no off-by-one on the kernel
//     boundary).
//   - Mismatched depth buffer size is a no-op (early-return guard
//     prevents OOB reads).
//
// DO NOT introduce state into the kernel.  DO NOT add RNG.  DO NOT
// add cross-pixel feedback (e.g. iterative refinement).  The
// "risultato deterministico" guarantee is part of the user spec.
// ============================================================================

#include <chronon3d/render_graph/nodes/per_pixel_dof_node.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace chronon3d::graph {

namespace {

constexpr float kVisibleBlurThreshold = 0.5f;

struct DofActiveRegion {
    bool depth_size_valid = false;
    std::size_t blur_source_pixels = 0;
    float max_radius = 0.0f;
    std::optional<raster::BBox> clip;

    // Preserve the existing diagnostics signal used by telemetry/debugging.
    std::size_t non_neutral_depth = 0;
    i32 depth_x0 = 0;
    i32 depth_y0 = 0;
    i32 depth_x1 = 0;
    i32 depth_y1 = 0;
};

DofActiveRegion analyze_dof_active_region(
    std::span<const float> depth,
    i32 width,
    i32 height,
    const DepthOfFieldSettings& dof,
    const LensModel& lens,
    const DofSourceCoverage& published_sources)
{
    DofActiveRegion region;
    region.depth_x0 = width;
    region.depth_y0 = height;

    if (width <= 0 || height <= 0) {
        return region;
    }

    const auto expected_size = static_cast<std::size_t>(width) *
                               static_cast<std::size_t>(height);
    if (depth.size() != expected_size) {
        return region;
    }
    region.depth_size_valid = true;

    i32 active_x0 = width;
    i32 active_y0 = height;
    i32 active_x1 = 0;
    i32 active_y1 = 0;
    bool seeded_from_compositor = false;
    if (published_sources.source_bbox && published_sources.max_radius >= 0.5f) {
        const auto& source_bbox = *published_sources.source_bbox;
        active_x0 = std::clamp(source_bbox.x0, 0, width);
        active_y0 = std::clamp(source_bbox.y0, 0, height);
        active_x1 = std::clamp(source_bbox.x1, 0, width);
        active_y1 = std::clamp(source_bbox.y1, 0, height);
        region.max_radius = published_sources.max_radius;
        seeded_from_compositor = active_x0 < active_x1 && active_y0 < active_y1;
    }

    const i32 scan_margin = seeded_from_compositor
        ? std::max(1, static_cast<i32>(std::ceil(region.max_radius))) : 0;
    const i32 scan_x0 = seeded_from_compositor
        ? std::max(0, active_x0 - scan_margin) : 0;
    const i32 scan_y0 = seeded_from_compositor
        ? std::max(0, active_y0 - scan_margin) : 0;
    const i32 scan_x1 = seeded_from_compositor
        ? std::min(width, active_x1 + scan_margin) : width;
    const i32 scan_y1 = seeded_from_compositor
        ? std::min(height, active_y1 + scan_margin) : height;

    for (i32 y = scan_y0; y < scan_y1; ++y) {
        for (i32 x = scan_x0; x < scan_x1; ++x) {
            const float z = depth[static_cast<std::size_t>(y) * width + x];

            // Keep the historical diagnostic definition intact.  In
            // particular, z == 0 (the common focus/background value) is not
            // counted as non-neutral even though it is a valid depth sample.
            if (!seeded_from_compositor && std::abs(z) > 1e-4f &&
                z < kUnsetDofDepth * 0.5f) {
                ++region.non_neutral_depth;
                region.depth_x0 = std::min(region.depth_x0, x);
                region.depth_y0 = std::min(region.depth_y0, y);
                region.depth_x1 = std::max(region.depth_x1, x + 1);
                region.depth_y1 = std::max(region.depth_y1, y + 1);
            }

            // kUnsetDofDepth means the compositor did not write a surface here.
            // NaN/Inf are also rejected defensively: they are not meaningful
            // scene depths and must never grow the processing region.
            if (!std::isfinite(z) || z >= kUnsetDofDepth * 0.5f) {
                continue;
            }

            const float radius = compute_dof_blur_radius(
                dof, lens, z, static_cast<float>(width));
            if (!std::isfinite(radius) || radius < kVisibleBlurThreshold) {
                continue;
            }

            ++region.blur_source_pixels;
            region.max_radius = std::max(region.max_radius, radius);
            if (!seeded_from_compositor) {
                active_x0 = std::min(active_x0, x);
                active_y0 = std::min(active_y0, y);
                active_x1 = std::max(active_x1, x + 1);
                active_y1 = std::max(active_y1, y + 1);
            }
        }
    }

    if (region.blur_source_pixels == 0) {
        return region;
    }

    // The kernel is source-driven: a defocused source can influence a
    // destination at most max_radius pixels away in each separable pass.
    // Expanding the tight source bbox by that radius therefore contains the
    // complete horizontal + vertical blur halo while excluding unrelated
    // full-frame background.  Cap the integer margin at the largest canvas
    // dimension; anything larger already expands to the whole framebuffer.
    const float capped_radius = std::min(
        region.max_radius,
        static_cast<float>(std::max(width, height)));
    const i32 margin = std::max(
        1, static_cast<i32>(std::ceil(capped_radius)));

    raster::BBox clip{
        std::max(0, active_x0 - margin),
        std::max(0, active_y0 - margin),
        std::min(width, active_x1 + margin),
        std::min(height, active_y1 + margin)
    };
    if (!clip.is_empty()) {
        region.clip = clip;
    }
    return region;
}

} // namespace

NodeExecResult PerPixelDofNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes)
{
    (void)input_bboxes;

    // DOF mutates its destination in place.  Unlike a composite, it must
    // never take ownership of (and blur) its input: the input may still be
    // referenced by the graph cache or by the frame-lifetime state.  Disable
    // the executor's zero-copy hints for this node and make an independent
    // framebuffer copy before applying the kernel.
    ctx.node_exec.planned_physical_slot = nullptr;
    ctx.node_exec.reusable_bottom.reset();
    const auto clone_input = [&](const Framebuffer& input) {
        auto copy = ctx.acquire_owned_fb(input.width(), input.height(), false);
        for (i32 y = 0; y < input.height(); ++y) {
            std::memcpy(copy->pixels_row(y), input.pixels_row(y),
                        static_cast<std::size_t>(input.width()) * sizeof(Color));
        }
        copy->set_origin(input.origin_x(), input.origin_y());
        copy->set_opaque(input.is_opaque());
        copy->set_content_cleared(input.is_content_cleared());
        return copy;
    };

    const auto& camera = camera_for(ctx);
    const auto& dof_depth = ctx.node_exec.dof_depth_buffer();

    if (ctx.policy.diagnostics_enabled) {
        spdlog::info("[PerPixelDofNode] focus_z={:.1f} enabled={} dof_depth_size={} inputs_count={}",
            camera.dof.focus_z, camera.dof.enabled,
            dof_depth.size(), inputs.size());
    }

    if (inputs.empty() || !inputs[0]) {
        auto empty = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
        empty->clear(Color::transparent());
        return NodeExecResult{std::move(empty)};
    }

    if (!camera.dof.enabled) {
        return NodeExecResult{clone_input(*inputs[0])};
    }

    // Check that the depth buffer was populated during compositing.
    if (dof_depth.empty()) {
        // No depth data — fall through without blur.
        return NodeExecResult{clone_input(*inputs[0])};
    }

    auto result = clone_input(*inputs[0]);

    // Find only sources that can produce a visible blur.  A focused background
    // may legitimately have valid depth over the entire canvas; using all
    // valid depth as the ROI would therefore collapse back to full-frame work.
    // The O(width*height) scan is intentionally cheap compared with the
    // O(width*height*radius) gather blur it removes.
    const auto roi_analysis_start = profiling::now();
    const auto active_region = analyze_dof_active_region(
        std::span<const float>{dof_depth},
        ctx.frame_input.width,
        ctx.frame_input.height,
        camera.dof,
        camera.lens,
        ctx.node_exec.dof_sources());

    if (profiling::g_current_counters) {
        profiling::g_current_counters->dof_blur_source_pixels.fetch_add(
            active_region.blur_source_pixels, std::memory_order_relaxed);
        profiling::g_current_counters->dof_roi_analysis_us.fetch_add(
            static_cast<uint64_t>(profiling::elapsed_us(roi_analysis_start)),
            std::memory_order_relaxed);
    }

    if (ctx.policy.diagnostics_enabled) {
        if (active_region.depth_size_valid) {
            spdlog::info("[PerPixelDofNode] non_neutral_depth={} bbox=[{}:{} -> {}:{}]",
                active_region.non_neutral_depth,
                active_region.depth_x0, active_region.depth_y0,
                active_region.depth_x1, active_region.depth_y1);
            if (active_region.clip) {
                spdlog::info(
                    "[PerPixelDofNode] blur_source_pixels={} max_radius={:.2f} processing_bbox=[{}:{} -> {}:{}]",
                    active_region.blur_source_pixels, active_region.max_radius,
                    active_region.clip->x0, active_region.clip->y0,
                    active_region.clip->x1, active_region.clip->y1);
            } else {
                spdlog::info(
                    "[PerPixelDofNode] blur_source_pixels=0 max_radius=0.00 processing_bbox=empty");
            }
        } else {
            spdlog::warn(
                "[PerPixelDofNode] depth buffer size mismatch: got={} expected={}",
                dof_depth.size(),
                static_cast<std::size_t>(ctx.frame_input.width) *
                    static_cast<std::size_t>(ctx.frame_input.height));
        }
    }

    // A mismatched depth buffer is a no-op (same kernel contract as before).
    // Likewise, if every valid source is in focus, there is no visible blur
    // and we can skip the backend entirely instead of allocating its scratch
    // buffers just to discover max_r < 0.5f again.
    const std::optional<raster::BBox> clip = active_region.clip;
    if (active_region.depth_size_valid && clip && ctx.services.backend) {
        // Explicit span-wrap: `ctx.node_exec.dof_depth` is `std::vector<float>`
        // (the executor owns the depth buffer for node lifetimes).  The kernel
        // signature took `std::vector<float>` historically; that forced
        // `SoftwareBackend::apply_per_pixel_dof` to allocate a 1920×1080 copy
        // — 8 MiB — on every dispatch.  Forwarding a `std::span` makes that
        // alloc go away.  Implicit C++20 range-to-span conversion would also
        // work, but explicit construction documents intent and guards
        // against future type drift on `dof_depth`.
        ctx.services.backend->apply_per_pixel_dof(
            *result, std::span<const float>{dof_depth},
            camera.dof, camera.lens, clip);
    }

    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->effect_stack_calls.fetch_add(1, std::memory_order_relaxed);
        uint64_t area = 0;
        if (clip) {
            raster::BBox clipped = *clip;
            clipped.clip_to(ctx.frame_input.width, ctx.frame_input.height);
            area = clipped.is_empty() ? 0
                : static_cast<uint64_t>(clipped.x1 - clipped.x0) *
                  static_cast<uint64_t>(clipped.y1 - clipped.y0);
        }
        ctx.node_exec.counters->effect_pixels.fetch_add(area, std::memory_order_relaxed);
    }

    return NodeExecResult{std::move(result)};
}

} // namespace chronon3d::graph
