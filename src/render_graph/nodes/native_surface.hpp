#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// native_surface.hpp — src-local helpers for the native (GPU) surface
// fast-paths shared by the graph nodes.
//
// This header lives under src/ (NOT include/chronon3d/) and is therefore
// internal: it must never be included by a public SDK header.  It factors
// out the four pieces that were previously copy-pasted across
// composite_node.cpp, effect_stack_node.cpp and track_matte_node.cpp:
//
//   - native_surface_desc()     — the canonical Rgba32Float/Storage/
//                                 FrameTransient SurfaceDesc.
//   - pack_framebuffer_rgba()   — tightly-packed RGBA float pixel buffer.
//   - ensure_native_surface()   — create + upload + attach a surface handle.
//   - release_native_surface()  — symmetric teardown of an attached handle.
//
// The first two are used by every full-frame native fast-path; the last two
// are used by CompositeNode (and any future node that attaches a handle to
// its own output framebuffer).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace chronon3d::graph {

inline bool native_surface_promotion_diag_enabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("CHRONON3D_NATIVE_SURFACE_PROMOTION_DIAG");
        if (!value) return false;
        const std::string_view setting(value);
        return setting == "1" || setting == "true" || setting == "on";
    }();
    return enabled;
}

/// Build the canonical Rgba32Float / Storage / FrameTransient surface
/// description used by every native full-frame effect/composite fast-path.
inline runtime::SurfaceDesc native_surface_desc(i32 width, i32 height) {
    return runtime::SurfaceDesc::make(
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height),
        runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::FrameTransient);
}

/// Pack a framebuffer's pixels into a tightly-packed RGBA float buffer in
/// the layout the native `upload_surface()` contract expects.
inline std::vector<float> pack_framebuffer_rgba(const Framebuffer& framebuffer) {
    const auto width = static_cast<std::size_t>(framebuffer.width());
    const auto height = static_cast<std::size_t>(framebuffer.height());
    std::vector<float> rgba(width * height * 4);
    const Color* src = framebuffer.data();
    for (std::size_t y = 0; y < height; ++y) {
        const Color* row = src + y * static_cast<std::size_t>(framebuffer.stride());
        std::memcpy(rgba.data() + y * width * 4, row, width * sizeof(Color));
    }
    return rgba;
}

/// Return a zero-copy RGBA view when the framebuffer storage is tightly
/// packed.  The Vulkan uploader copies the span into its staging buffer before
/// returning, so the view only needs to live for the upload call.
inline std::span<const float> framebuffer_rgba_view(
    const Framebuffer& framebuffer, std::vector<float>& packed) {
    static_assert(sizeof(Color) == sizeof(float) * 4);
    if (framebuffer.stride() == framebuffer.width()) {
        return {reinterpret_cast<const float*>(framebuffer.data()),
                static_cast<std::size_t>(framebuffer.width()) * framebuffer.height() * 4};
    }
    packed = pack_framebuffer_rgba(framebuffer);
    return packed;
}

/// Ensure `framebuffer` has a native (device-local) surface: create the
/// surface, upload the current pixels, and attach the handle.  Returns false
/// (leaving the framebuffer untouched) when the backend/registry are absent,
/// or any native step fails.  Returns true immediately when the framebuffer
/// already carries a handle.
inline bool ensure_native_surface(
    RenderGraphContext& ctx, Framebuffer& framebuffer,
    std::string_view promotion_reason = "unspecified") {
    if (!ctx.services.backend || !ctx.services.surface_registry || !ctx.services.backend->supports_native_surfaces()) return false;
    if (framebuffer.surface_handle() != runtime::kInvalidRenderSurfaceHandle) {
        const auto handle = framebuffer.surface_handle();
        // The registry owns logical identity, while Vulkan owns the physical
        // binding.  They can briefly diverge when a pooled framebuffer is
        // returned while a transient backend surface is reclaimed.  Checking
        // only the registry made a stale handle look valid and the next GPU
        // operation failed with "surface handle is not bound to a physical
        // slot".  Treat either side being stale as a cache miss and rebuild
        // the native surface before issuing a command.
        if (ctx.services.surface_registry->lookup(handle) &&
            ctx.services.backend->is_native_surface_valid(handle)) {
            if (ctx.node_exec.counters) {
                ctx.node_exec.counters->native_surface_reuse_count.fetch_add(
                    1, std::memory_order_relaxed);
            }
            return true;
        }
        if (ctx.services.surface_registry->lookup(handle)) {
            (void)ctx.services.surface_registry->release(handle);
        }
        // A previous job or frame may have reclaimed the backend surface while
        // a pooled CPU framebuffer still carried its old logical handle.
        framebuffer.clear_surface_handle();
    }

    if (!framebuffer.data()) {
        spdlog::error("[native-surface] cannot upload from framebuffer without CPU pixel backing");
        return false;
    }

    const auto desc = native_surface_desc(framebuffer.width(), framebuffer.height());
    // A strict native-video job can still contain a small CPU-authored
    // overlay (for example a decoded watermark or a legacy text fallback).
    // Rejecting promotion here makes CompositeNode fail closed even though
    // this is the only defined CPU->GPU bridge for that overlay, and the
    // caller then may dereference an incomplete native graph.  Keep the
    // promotion measurable instead of disabling it: the base video remains
    // GPU-resident and only the overlay surface crosses the boundary.
    const auto handle = ctx.services.surface_registry->create(desc);
    if (handle == runtime::kInvalidRenderSurfaceHandle) {
        spdlog::error("[native-surface] registry rejected {}x{} surface", framebuffer.width(), framebuffer.height());
        return false;
    }

    const auto created = ctx.services.backend->create_surface(handle, desc);
    if (!created.ok()) {
        spdlog::error("[native-surface] create failed for {}x{}: {}",
                      framebuffer.width(), framebuffer.height(), created.error().message);
        ctx.services.surface_registry->release(handle);
        return false;
    }
    std::vector<float> packed;
    const auto pixels = framebuffer_rgba_view(framebuffer, packed);
    runtime::UploadTicket upload_ticket{};
    const auto promotion_t0 = profiling::now();
    const auto uploaded = ctx.policy.retain_native_surface_for_video
        ? ctx.services.backend->upload_surface_async(handle, desc, pixels, upload_ticket)
        : ctx.services.backend->upload_surface(handle, desc, pixels);
    if (!uploaded.ok()) {
        spdlog::error("[native-surface] upload failed for {}x{}: {}",
                      framebuffer.width(), framebuffer.height(), uploaded.error().message);
        // The backend surface was already allocated by create_surface();
        // release it symmetrically before dropping the registry entry so the
        // native path stays leak-free on the upload-failure branch.
        (void)ctx.services.backend->release_surface(handle);
        ctx.services.surface_registry->release(handle);
        return false;
    }
    if (ctx.node_exec.counters) {
        const auto bytes = static_cast<std::uint64_t>(pixels.size_bytes());
        ctx.node_exec.counters->native_surface_promotion_count.fetch_add(
            1, std::memory_order_relaxed);
        ctx.node_exec.counters->native_surface_promotion_bytes.fetch_add(
            bytes, std::memory_order_relaxed);
        ctx.node_exec.counters->native_surface_promotion_wall_us.fetch_add(
            static_cast<std::uint64_t>(profiling::duration_us(
                promotion_t0, profiling::now())), std::memory_order_relaxed);
    }
    if (native_surface_promotion_diag_enabled()) {
        spdlog::info("[native-surface-promotion] frame={} reason={} bytes={}",
                     static_cast<int>(ctx.frame_input.frame), promotion_reason,
                     pixels.size_bytes());
    }
    framebuffer.set_surface_handle(handle);
    framebuffer.mark_cpu_gpu_synchronized();
    return true;
}

/// Attach a device-local destination whose contents are produced completely
/// by the next native kernel. Affine image transforms may use a clipped
/// dispatch and therefore do not necessarily write every destination pixel.
/// Newly allocated overlay surfaces are initialized to transparent so
/// untouched pixels preserve source-over composition with layers below.
inline bool ensure_empty_native_surface(RenderGraphContext& ctx,
                                        Framebuffer& framebuffer) {
    if (!ctx.services.backend || !ctx.services.surface_registry || !ctx.services.backend->supports_native_surfaces()) return false;
    if (framebuffer.surface_handle() != runtime::kInvalidRenderSurfaceHandle) {
        const auto handle = framebuffer.surface_handle();
        if (ctx.services.surface_registry->lookup(handle) &&
            ctx.services.backend->is_native_surface_valid(handle)) {
            return true;
        }
        if (ctx.services.surface_registry->lookup(handle)) {
            (void)ctx.services.surface_registry->release(handle);
        }
        framebuffer.clear_surface_handle();
    }
    const auto desc = native_surface_desc(framebuffer.width(), framebuffer.height());
    const auto handle = ctx.services.surface_registry->create(desc);
    if (handle == runtime::kInvalidRenderSurfaceHandle) {
        spdlog::error("[native-surface] registry rejected empty {}x{} surface", framebuffer.width(), framebuffer.height());
        return false;
    }
    const auto created = ctx.services.backend->create_surface(handle, desc);
    if (!created.ok()) {
        spdlog::error("[native-surface] empty create failed for {}x{}: {}",
                      framebuffer.width(), framebuffer.height(), created.error().message);
        ctx.services.surface_registry->release(handle);
        return false;
    }
    const auto cleared = ctx.services.backend->initialize_transparent_surface(handle);
    if (!cleared.ok()) {
        spdlog::error("[native-surface] empty transparent clear failed for {}x{}: {}",
                      framebuffer.width(), framebuffer.height(), cleared.error().message);
        (void)ctx.services.backend->release_surface(handle);
        (void)ctx.services.surface_registry->release(handle);
        return false;
    }
    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->native_surface_empty_create_count.fetch_add(
            1, std::memory_order_relaxed);
    }
    framebuffer.set_surface_handle(handle);
    framebuffer.mark_gpu_authoritative();
    return true;
}

/// Release the native backing of a framebuffer's surface handle (backend
/// resource + registry entry) and clear the handle.  This is the symmetric
/// counterpart to ensure_native_surface() and must be used whenever a native
/// path gives up on a handle it created, so transient failure branches do not
/// leak device-local surfaces or registry identities.
inline void release_native_surface(RenderGraphContext& ctx, Framebuffer& framebuffer) {
    const auto handle = framebuffer.surface_handle();
    if (handle == runtime::kInvalidRenderSurfaceHandle) return;
    if (ctx.services.backend) {
        (void)ctx.services.backend->release_surface(handle);
    }
    if (ctx.services.surface_registry) {
        ctx.services.surface_registry->release(handle);
    }
    framebuffer.clear_surface_handle();
}

} // namespace chronon3d::graph
