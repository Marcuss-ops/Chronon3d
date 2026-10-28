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
#include "native_surface.hpp"
#include "composite_backdrop_detail.hpp"
#include <chronon3d/runtime/frame/frame_error_slot.hpp>

namespace chronon3d::graph::detail {

bool try_native_affine_composite(
    RenderGraphContext& ctx, Framebuffer& destination,
    const Framebuffer& source, const std::optional<raster::BBox>& clip,
    BlendMode mode = BlendMode::Normal) {
    if (!ctx.backend || !ctx.surface_registry) return false;
    if (source.width() == destination.width() &&
        source.height() == destination.height() &&
        source.origin_x() == destination.origin_x() &&
        source.origin_y() == destination.origin_y()) {
        return false;
    }
    const auto original_destination = destination.surface_handle();
    OwnedFB affine_layer;
    Framebuffer* affine_destination = &destination;
    if (mode != BlendMode::Normal) {
        // Affine sampling has source-over semantics. Rasterize the cropped
        // source onto a transparent canvas first, then apply the requested
        // blend mode against the already-seeded destination.
        affine_layer = ctx.acquire_owned_fb(destination.width(), destination.height(), false);
        if (!ensure_empty_native_surface(ctx, *affine_layer)) return false;
        affine_destination = affine_layer.get();
    }
    // A dimension-mismatched bottom arrives in a fresh, uninitialized result;
    // create it transparent because strict GPU mode has no CPU pixels to
    // upload. A dimension-mismatched top arrives after the bottom has already
    // seeded the destination, so preserve that valid native surface.
    if (affine_destination == &destination) {
        const bool destination_is_native =
            original_destination != runtime::kInvalidRenderSurfaceHandle &&
            ctx.surface_registry->lookup(original_destination).has_value() &&
            ctx.backend_surface_api()->is_native_surface_valid(original_destination);
        if (destination_is_native) {
            if (!ensure_native_surface(ctx, destination,
                                       "CompositeNode.affine.destination")) return false;
        } else if (!ensure_empty_native_surface(ctx, destination)) {
            return false;
        }
    }

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
    transform.destination_origin_x = affine_destination->origin_x();
    transform.destination_origin_y = affine_destination->origin_y();

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

    const auto composited = ctx.backend_surface_api()->transform_surface_affine(
        affine_destination->surface_handle(), source_handle, transform);
    if (source_copy) release_native_surface(ctx, *source_copy);
    if (!composited.ok()) {
        spdlog::error(
            "[native-composite] affine operation failed: [{}] {}",
            chronon3d::graph::render_backend_error_code_name(composited.error().code),
            composited.error().message);
        if (original_destination == runtime::kInvalidRenderSurfaceHandle) {
            release_native_surface(ctx, destination);
        }
        return false;
    }
    if (affine_layer) {
        const auto blended = ctx.backend_surface_api()->composite_surfaces(
            destination.surface_handle(), affine_layer->surface_handle(), mode,
            CompositeOperator::SourceOver, clip);
        if (!blended.ok()) {
            spdlog::error(
                "[native-composite] affine blend failed: [{}] {}",
                chronon3d::graph::render_backend_error_code_name(blended.error().code),
                blended.error().message);
            return false;
        }
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
    if ((mode != BlendMode::Normal && mode != BlendMode::Add &&
         mode != BlendMode::Multiply && mode != BlendMode::Screen &&
         mode != BlendMode::Overlay) ||
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
    const auto result = ctx.backend_surface_api()->composite_surfaces(
        destination.surface_handle(), source.surface_handle(), mode, op, clip);
    if (result.ok()) {
        return true;
    }
    spdlog::error(
        "[native-composite] surface operation failed: [{}] {}",
        chronon3d::graph::render_backend_error_code_name(result.error().code),
        result.error().message);
    if (original_handle == runtime::kInvalidRenderSurfaceHandle) {
        release_native_surface(ctx, destination);
    }
    if (original_source_handle == runtime::kInvalidRenderSurfaceHandle) {
        release_native_surface(ctx, source);
    }
    return false;
}

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
        !ctx.backend_surface_api()->is_native_surface_valid(source.surface_handle())) {
        return ensure_native_surface(ctx, destination,
                                     "CompositeNode.seed.source-fallback");
    }
    // A seed is only a 1:1 copy when both framebuffers have the same extent.
    // Composite intermediates can be cropped to a layer's local bounds while
    // their source still covers the full canvas; in that case the destination's
    // CPU pixels are the correct authority to promote to a native surface.
    if (destination.width() != source.width() ||
        destination.height() != source.height()) {
        return ensure_native_surface(ctx, destination,
                                     "CompositeNode.seed.extent-fallback");
    }
    // A pooled framebuffer can retain a logical handle whose registered
    // surface still has the previous crop's physical extent. Do not ask the
    // backend to seed that stale surface and then recover from a rejected
    // copy: materialize the current CPU pixels into a correctly-sized surface
    // before issuing any GPU operation.
    if (original_destination != runtime::kInvalidRenderSurfaceHandle &&
        original_destination != ctx.policy.native_video_encode_surface) {
        const auto record = ctx.surface_registry->lookup(original_destination);
        if (record &&
            (record->desc.width != static_cast<std::uint32_t>(destination.width()) ||
             record->desc.height != static_cast<std::uint32_t>(destination.height()))) {
            spdlog::debug(
                "[native-seed] replacing stale surface handle={} extent={}x{} "
                "framebuffer={}x{}",
                original_destination, record->desc.width, record->desc.height,
                destination.width(), destination.height());
            // The destination is a pooled GPU-only framebuffer whose prior
            // occupant had a cropped extent. The current bottom surface is
            // still authoritative and will be copied below; do not try to
            // upload nonexistent CPU pixels from this destination.
            if (!ensure_empty_native_surface(ctx, destination)) {
                return false;
            }
        }
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
    const auto seeded = ctx.backend_surface_api()->copy_surface(
        destination.surface_handle(), source.surface_handle(), std::nullopt);
    if (!seeded.ok()) {
        // A rejected seed is a hard error at the only call site (see below):
        // on a native backend, composite of a canvas-sized SourceOver chain is
        // fail-closed and CPU fallback is forbidden by policy, so a per-frame
        // warn here would spam without changing the outcome.  Log the first
        // occurrences and roll up at 1000 boundaries instead.
        static std::atomic<std::uint64_t> g_seed_failures{0};
        const std::uint64_t nth =
            g_seed_failures.fetch_add(1, std::memory_order_relaxed) + 1;
        if (nth <= 3 || nth % 1000 == 0) {
            const auto destination_record = ctx.surface_registry
                ? ctx.surface_registry->lookup(destination.surface_handle())
                : std::nullopt;
            const auto source_record = ctx.surface_registry
                ? ctx.surface_registry->lookup(source.surface_handle())
                : std::nullopt;
            spdlog::warn(
                "[native-seed] copy_surface rejected by backend (failure #{}): "
                "{}; destination_fb={}x{} handle={} registry={}x{}; "
                "source_fb={}x{} handle={} registry={}x{}",
                nth, seeded.error().message,
                destination.width(), destination.height(),
                destination.surface_handle(),
                destination_record ? destination_record->desc.width : 0,
                destination_record ? destination_record->desc.height : 0,
                source.width(), source.height(), source.surface_handle(),
                source_record ? source_record->desc.width : 0,
                source_record ? source_record->desc.height : 0);
        }
        // Some Vulkan drivers reject a replace-copy while the destination is
        // being imported into the CUDA/NVENC chain. The CPU pixels in
        // `destination` are valid (acquire_owned_fb copied them above), so
        // promote those pixels as a correctness-preserving native fallback.
        // `ensure_native_surface` accepts a still-valid attached handle. A
        // failed copy can mean that the handle is bound to a stale extent, so
        // detach it before promoting the authoritative CPU copy. The old
        // frame-transient resource remains owned by the frame registry and is
        // reclaimed with the rest of this frame; other framebuffer aliases of
        // the handle remain valid.
        destination.clear_surface_handle();
        destination.mark_cpu_authoritative();
        return ensure_native_surface(ctx, destination,
                                     "CompositeNode.seed.copy-fallback");
    }
    return true;
}

[[nodiscard]] std::optional<NodeExecutionError> materialize_native_or_fail(
    RenderGraphContext& ctx, Framebuffer& destination,
    const Framebuffer& source, std::string_view context) {
    if (!ctx.backend || !ctx.surface_registry ||
        !ctx.backend_stats_api()->supports_native_surfaces()) {
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

} // namespace chronon3d::graph::detail
