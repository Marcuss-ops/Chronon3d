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
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace chronon3d::graph {

NodeExecResult PerPixelDofNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes)
{
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
        std::size_t non_neutral_depth = 0;
        i32 depth_x0 = ctx.frame_input.width;
        i32 depth_y0 = ctx.frame_input.height;
        i32 depth_x1 = 0;
        i32 depth_y1 = 0;
        for (i32 y = 0; y < ctx.frame_input.height; ++y) {
            for (i32 x = 0; x < ctx.frame_input.width; ++x) {
                const float z = dof_depth[static_cast<size_t>(y) * ctx.frame_input.width + x];
                if (std::abs(z) > 1e-4f && z < 1e17f) {
                    ++non_neutral_depth;
                    depth_x0 = std::min(depth_x0, x);
                    depth_y0 = std::min(depth_y0, y);
                    depth_x1 = std::max(depth_x1, x + 1);
                    depth_y1 = std::max(depth_y1, y + 1);
                }
            }
        }
        spdlog::info("[PerPixelDofNode] focus_z={:.1f} enabled={} dof_depth_size={} inputs_count={}",
            camera.dof.focus_z, camera.dof.enabled,
            dof_depth.size(), inputs.size());
        spdlog::info("[PerPixelDofNode] non_neutral_depth={} bbox=[{}:{} -> {}:{}]",
            non_neutral_depth, depth_x0, depth_y0, depth_x1, depth_y1);
    }

    if (inputs.empty() || !inputs[0]) {
        auto empty = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
        empty->clear(Color::transparent());
        return NodeExecResult{std::move(empty)};
    }

    if (!camera.dof.enabled) {
        return NodeExecResult{clone_input(*inputs[0])};
    }

    // Check that the depth buffer was populated during compositing
    if (dof_depth.empty()) {
        // No depth data — fall through without blur
        return NodeExecResult{clone_input(*inputs[0])};
    }

    auto result = clone_input(*inputs[0]);

    // DOF is a screen-space post-process over the complete composite. The
    // source-driven kernel treats in-focus background pixels as point samples,
    // while defocused text pixels spread according to their own depth. This
    // avoids both layer-local double blur and sharp+blurred duplicate text.
    const std::optional<raster::BBox> clip = std::nullopt;

    // Apply per-pixel DOF blur via the render backend.
    if (ctx.services.backend) {
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
        uint64_t area = static_cast<uint64_t>(ctx.frame_input.width) * ctx.frame_input.height;
        if (clip) {
            raster::BBox clipped = *clip;
            clipped.clip_to(ctx.frame_input.width, ctx.frame_input.height);
            area = clipped.is_empty() ? 0
                : static_cast<uint64_t>(clipped.x1 - clipped.x0) * (clipped.y1 - clipped.y0);
        }
        ctx.node_exec.counters->effect_pixels.fetch_add(area, std::memory_order_relaxed);
    }

    return NodeExecResult{std::move(result)};
}

} // namespace chronon3d::graph
