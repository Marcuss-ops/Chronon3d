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
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chronon3d::graph {

/// Build the canonical Rgba32Float / Storage / FrameTransient surface
/// description used by every native full-frame effect/composite fast-path.
inline runtime::SurfaceDesc native_surface_desc(i32 width, i32 height) {
    return runtime::SurfaceDesc{
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height),
        runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::FrameTransient,
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
            sizeof(float) * 4};
}

/// Pack a framebuffer's pixels into a tightly-packed RGBA float buffer in
/// the layout the native `upload_surface()` contract expects.
inline std::vector<float> pack_framebuffer_rgba(const Framebuffer& framebuffer) {
    std::vector<float> rgba(static_cast<std::size_t>(framebuffer.width()) *
                            static_cast<std::size_t>(framebuffer.height()) * 4);
    std::size_t index = 0;
    for (i32 y = 0; y < framebuffer.height(); ++y) {
        for (i32 x = 0; x < framebuffer.width(); ++x) {
            const auto pixel = framebuffer.get_pixel(x, y);
            rgba[index++] = pixel.r;
            rgba[index++] = pixel.g;
            rgba[index++] = pixel.b;
            rgba[index++] = pixel.a;
        }
    }
    return rgba;
}

/// Ensure `framebuffer` has a native (device-local) surface: create the
/// surface, upload the current pixels, and attach the handle.  Returns false
/// (leaving the framebuffer untouched) when the backend/registry are absent,
/// or any native step fails.  Returns true immediately when the framebuffer
/// already carries a handle.
inline bool ensure_native_surface(RenderGraphContext& ctx, Framebuffer& framebuffer) {
    if (!ctx.services.backend || !ctx.services.surface_registry) return false;
    if (framebuffer.surface_handle() != runtime::kInvalidRenderSurfaceHandle) {
        if (ctx.services.surface_registry->lookup(framebuffer.surface_handle())) {
            return true;
        }
        // A previous job may have reclaimed a transient backend surface while
        // a pooled CPU framebuffer still carried its old logical handle.
        framebuffer.clear_surface_handle();
    }

    const auto desc = native_surface_desc(framebuffer.width(), framebuffer.height());
    const auto handle = ctx.services.surface_registry->create(desc);
    if (handle == runtime::kInvalidRenderSurfaceHandle) return false;

    const auto created = ctx.services.backend->create_surface(handle, desc);
    if (!created.ok()) {
        ctx.services.surface_registry->release(handle);
        return false;
    }
    const auto uploaded = ctx.services.backend->upload_surface(
        handle, desc, pack_framebuffer_rgba(framebuffer));
    if (!uploaded.ok()) {
        // The backend surface was already allocated by create_surface();
        // release it symmetrically before dropping the registry entry so the
        // native path stays leak-free on the upload-failure branch.
        (void)ctx.services.backend->release_surface(handle);
        ctx.services.surface_registry->release(handle);
        return false;
    }
    framebuffer.set_surface_handle(handle);
    return true;
}

/// Attach a device-local destination whose contents are produced completely
/// by the next native kernel. ImageShape uses this variant because its affine
/// kernel writes every destination pixel; uploading the CPU framebuffer first
/// would add a needless GPU<-CPU round trip on every image layer/frame.
inline bool ensure_empty_native_surface(RenderGraphContext& ctx,
                                        Framebuffer& framebuffer) {
    if (!ctx.services.backend || !ctx.services.surface_registry) return false;
    if (framebuffer.surface_handle() != runtime::kInvalidRenderSurfaceHandle) {
        if (ctx.services.surface_registry->lookup(framebuffer.surface_handle())) {
            return true;
        }
        framebuffer.clear_surface_handle();
    }
    const auto desc = native_surface_desc(framebuffer.width(), framebuffer.height());
    const auto handle = ctx.services.surface_registry->create(desc);
    if (handle == runtime::kInvalidRenderSurfaceHandle) return false;
    const auto created = ctx.services.backend->create_surface(handle, desc);
    if (!created.ok()) {
        ctx.services.surface_registry->release(handle);
        return false;
    }
    framebuffer.set_surface_handle(handle);
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
