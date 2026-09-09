// scene_native_output.cpp — native output synchronization and native
// encode-source residency for the scene render pipeline, extracted from
// scene.cpp (phases 0/8/11 native-surface contract).

#include "scene_native_output.hpp"

#include "../nodes/native_surface.hpp"

#include <chronon3d/cache/framebuffer_copy.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

namespace chronon3d::graph::detail {

void synchronize_native_output(RenderGraphContext& ctx,
                               const std::shared_ptr<Framebuffer>& framebuffer) {
    if (!framebuffer || !ctx.backend ||
        framebuffer->surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
        return;
    }
    // A native encode surface is the authoritative output contract for
    // Vulkan+NVENC. Never read it back merely because the public API also
    // returns a CPU framebuffer; the encoder consumes the device surface.
    if (ctx.policy.native_video_encode_surface !=
        runtime::kInvalidRenderSurfaceHandle) {
        framebuffer->mark_gpu_authoritative();
        return;
    }
    if (ctx.backend->supports_native_video_surface() &&
        ctx.policy.retain_native_surface_for_video) {
        framebuffer->mark_gpu_authoritative();
        return;
    }
    // P0.4 — native video contract: a video job on a native-video backend
    // must consume the device surface (encode surface or retained residency).
    // A silent full-frame readback here is exactly the GPU→CPU fallback the
    // demolition removed everywhere else. download_surface() remains a
    // legitimate capability for images, golden tests and SDK consumers;
    // the *video* path may not use it as a fallback.
    if (ctx.backend->supports_native_video_surface() &&
        ctx.policy.is_gpu_native_required()) {
        spdlog::error(
            "[video] native video contract violation: frame reached "
            "synchronize_native_output without a native encode surface and "
            "without retain_native_surface_for_video; refusing silent readback");
        framebuffer->mark_gpu_authoritative();
        return;
    }
    if (ctx.policy.disable_pixel_readback) {
        return;
    }
    // A framebuffer may retain a pooled native handle while its CPU pixels
    // are already the authoritative render result.  Downloading that stale
    // handle makes Vulkan reject an otherwise valid FullGraph frame as an
    // uninitialized surface.
    if (framebuffer->is_cpu_authoritative()) {
        return;
    }
    const auto scratch = ctx.download_scratch(framebuffer->width(),
                                              framebuffer->height());
    const auto result = ctx.backend->download_surface(
        framebuffer->surface_handle(), scratch);
    if (!result.ok()) {
        spdlog::error("[backend] native output synchronization failed: {}",
                      result.error().message);
        if (ctx.surface_registry) {
            (void)ctx.backend->release_surface(framebuffer->surface_handle());
            (void)ctx.surface_registry->release(framebuffer->surface_handle());
        }
        framebuffer->clear_surface_handle();
        return;
    }
    // Single copy authority: the download payload is packed (stride ==
    // width), while the framebuffer may be stride-padded; copy_rows() uses
    // each buffer's own stride so no row is shifted. Both the previously
    // flat fast path (stride == width) and the row-wise fallback are exactly
    // this operation.
    FramebufferCopyAuthority::copy_rows(
        framebuffer->pixels_row(0), framebuffer->stride(),
        reinterpret_cast<const Color*>(scratch.data()),
        framebuffer->width() /* packed source has no padding */,
        framebuffer->width(), framebuffer->height());
    if (ctx.surface_registry) {
        (void)ctx.backend->release_surface(framebuffer->surface_handle());
        (void)ctx.surface_registry->release(framebuffer->surface_handle());
    }
    framebuffer->clear_surface_handle();
    framebuffer->mark_cpu_gpu_synchronized();
}

void release_temporary_native_output(RenderGraphContext& ctx,
                                     const std::shared_ptr<Framebuffer>& framebuffer,
                                     runtime::RenderSurfaceHandle source) {
    if (!framebuffer || source == runtime::kInvalidRenderSurfaceHandle ||
        source == ctx.policy.native_video_source_surface ||
        source == ctx.policy.native_video_encode_surface ||
        !ctx.backend || !ctx.surface_registry) {
        return;
    }
    // The frame batch has already submitted the device-to-device copy, but
    // the NVENC/CUDA consumer can still be reading the physical image.  Do
    // not call release_surface here: the Vulkan compatibility release path
    // waits for every pending submission and turned every video frame into a
    // full GPU drain.  Drop only the logical registry lease; physical
    // transient retirement is fenced centrally at batch/job cleanup.
    (void)ctx.surface_registry->release(source);
    if (framebuffer->surface_handle() == source) {
        framebuffer->clear_surface_handle();
    }
}

NativeSourceResidency::NativeSourceResidency(RenderGraphContext& ctx,
                                             RenderBackend& backend)
    : ctx_(ctx), backend_(backend) {}

void NativeSourceResidency::begin_encode_batch() {
    if (!encode_batch_active_ &&
        ctx_.policy.native_video_encode_surface !=
            runtime::kInvalidRenderSurfaceHandle) {
        backend_.begin_frame_batch();
        encode_batch_active_ = true;
    }
}

void NativeSourceResidency::begin_frame_batch() {
    if (!encode_batch_active_) {
        backend_.begin_frame_batch();
        encode_batch_active_ = true;
    }
}

runtime::RenderSurfaceHandle NativeSourceResidency::ensure_native_source(
    const std::shared_ptr<Framebuffer>& framebuffer, Frame frame) {
    if (!framebuffer) return runtime::kInvalidRenderSurfaceHandle;
    auto handle = framebuffer->surface_handle();
    // Native graph outputs are already resident and synchronized;
    // never copy them through the CPU-source ring a second time.
    if (handle != runtime::kInvalidRenderSurfaceHandle &&
        backend_.is_native_surface_valid(handle)) {
        return handle;
    }
    const auto persistent_source = ctx_.policy.native_video_source_surface;
    if (persistent_source != runtime::kInvalidRenderSurfaceHandle) {
        if (handle != runtime::kInvalidRenderSurfaceHandle &&
            handle != persistent_source && ctx_.surface_registry) {
            (void)backend_.release_surface(handle);
            (void)ctx_.surface_registry->release(handle);
        }
        handle = persistent_source;
        const auto desc = native_surface_desc(framebuffer->width(), framebuffer->height());
        // The encoder-owned source handle can be bound before its
        // first upload. Ensure its Vulkan image exists, then populate
        // it from the CPU framebuffer instead of treating the bound
        // but uninitialized image as a ready source.
        if (!backend_.is_native_surface_valid(handle) &&
            !backend_.create_surface(handle, desc).ok()) {
            return runtime::kInvalidRenderSurfaceHandle;
        }
        raster::BBox clip{0, 0, framebuffer->width(), framebuffer->height()};
        if (native_source_upload_clip_) clip = *native_source_upload_clip_;
        clip.x0 = std::clamp(clip.x0, 0, framebuffer->width());
        clip.y0 = std::clamp(clip.y0, 0, framebuffer->height());
        clip.x1 = std::clamp(clip.x1, clip.x0, framebuffer->width());
        clip.y1 = std::clamp(clip.y1, clip.y0, framebuffer->height());
        const auto rw = static_cast<std::size_t>(clip.x1 - clip.x0);
        const auto rh = static_cast<std::size_t>(clip.y1 - clip.y0);
        if (rw != 0 && rh != 0) {
            profiling::GpuUploadProducerScope upload_scope(
                has_projected_surface
                    ? profiling::GpuUploadProducer::Projection
                    : profiling::GpuUploadProducer::Video);
            runtime::UploadTicket upload_ticket{};
            const auto* first_pixel = framebuffer->data() +
                static_cast<std::size_t>(clip.y0) *
                    static_cast<std::size_t>(framebuffer->stride()) + clip.x0;
            const RenderBackend::UploadRows rows{
                .first_row = std::span<const Color>(first_pixel, rw),
                .stride = static_cast<std::size_t>(framebuffer->stride()),
            };
            const auto uploaded = backend_.upload_surface_rows_async(
                handle, desc, clip, rows, upload_ticket);
            if (!uploaded.ok()) return runtime::kInvalidRenderSurfaceHandle;
        }
        framebuffer->set_surface_handle(handle);
        return handle;
    }
    if (ctx_.policy.is_gpu_native_required()) {
        spdlog::error(
            "[native-residency] GPU_NATIVE_REQUIRED: missing native source surface at frame {}",
            static_cast<int>(frame));
        return runtime::kInvalidRenderSurfaceHandle;
    }
    if (!ctx_.surface_registry) {
        return runtime::kInvalidRenderSurfaceHandle;
    }
    if (handle != runtime::kInvalidRenderSurfaceHandle) {
        (void)ctx_.surface_registry->release(handle);
        framebuffer->clear_surface_handle();
    }
    const runtime::SurfaceDesc desc = runtime::SurfaceDesc::make(
        static_cast<std::uint32_t>(framebuffer->width()),
        static_cast<std::uint32_t>(framebuffer->height()),
        runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::FrameTransient);
    handle = ctx_.surface_registry->create(desc);
    if (!backend_.create_surface(handle, desc).ok()) {
        (void)ctx_.surface_registry->release(handle);
        return runtime::kInvalidRenderSurfaceHandle;
    }
    const auto upload_start = std::chrono::steady_clock::now();
    profiling::GpuUploadProducerScope upload_scope(
        profiling::GpuUploadProducer::Video);
    runtime::UploadTicket upload_ticket{};
    const raster::BBox full_clip{0, 0, framebuffer->width(), framebuffer->height()};
    const RenderBackend::UploadRows rows{
        .first_row = std::span<const Color>(
            framebuffer->data(), static_cast<std::size_t>(framebuffer->width())),
        .stride = static_cast<std::size_t>(framebuffer->stride()),
    };
    if (!backend_.upload_surface_rows_async(handle, desc, full_clip, rows, upload_ticket).ok()) {
        (void)backend_.release_surface(handle);
        (void)ctx_.surface_registry->release(handle);
        return runtime::kInvalidRenderSurfaceHandle;
    }
    const auto upload_ms = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - upload_start).count() / 1000;
    const auto total_bytes = static_cast<std::uint64_t>(
        static_cast<std::size_t>(framebuffer->width()) * framebuffer->height() * sizeof(Color));
    if (ctx_.node_exec.counters) {
        ctx_.node_exec.counters->video_surface_upload_count.fetch_add(
            1, std::memory_order_relaxed);
        ctx_.node_exec.counters->video_surface_upload_bytes.fetch_add(
            total_bytes,
            std::memory_order_relaxed);
        ctx_.node_exec.counters->cpu_full_surface_upload_bytes.fetch_add(
            total_bytes,
            std::memory_order_relaxed);
        ctx_.node_exec.counters->video_surface_upload_wall_ms.fetch_add(
            static_cast<std::uint64_t>(upload_ms),
            std::memory_order_relaxed);
        ctx_.node_exec.counters->full_surface_upload_ms.fetch_add(
            static_cast<std::uint64_t>(upload_ms),
            std::memory_order_relaxed);
    }
    framebuffer->set_surface_handle(handle);
    return handle;
}

std::shared_ptr<Framebuffer> NativeSourceResidency::finish_reused_native_frame(
    const std::shared_ptr<Framebuffer>& framebuffer, Frame frame) {
    if (!encode_batch_active_) return framebuffer;
    const auto encode_surface = ctx_.policy.native_video_encode_surface;
    const auto source = ensure_native_source(framebuffer, frame);
    if (source == runtime::kInvalidRenderSurfaceHandle) {
        backend_.end_frame_batch();
        encode_batch_active_ = false;
        return std::shared_ptr<Framebuffer>{};
    }
    if (source == encode_surface) {
        spdlog::debug(
            "[native-residency] encode-surface alias binding active: reused "
            "frame {} needs no D2D copy", static_cast<int>(frame));
    } else {
        spdlog::error(
            "[native-residency] terminal output is not bound to the encode "
            "surface on reused frame {} (source={}, encode={}); refusing "
            "degraded copy lane",
            static_cast<int>(frame), source, encode_surface);
        backend_.end_frame_batch();
        encode_batch_active_ = false;
        return std::shared_ptr<Framebuffer>{};
    }
    backend_.end_frame_batch();
    encode_batch_active_ = false;
    return framebuffer;
}

std::shared_ptr<Framebuffer> NativeSourceResidency::finish_frame_encode(
    std::shared_ptr<Framebuffer> exec_fb, Frame frame) {
    if (!encode_batch_active_) return exec_fb;
    const auto encode_surface = ctx_.policy.native_video_encode_surface;
    runtime::RenderSurfaceHandle source = runtime::kInvalidRenderSurfaceHandle;
    if (encode_surface != runtime::kInvalidRenderSurfaceHandle && exec_fb) {
        source = ensure_native_source(exec_fb, frame);
        if (source == runtime::kInvalidRenderSurfaceHandle) {
            spdlog::error("[backend] native encode copy has no valid source surface for frame {}",
                          static_cast<int>(frame));
            exec_fb.reset();
        } else if (source == encode_surface) {
            spdlog::debug(
                "[native-residency] encode-surface alias binding active: frame "
                "{} rendered directly into encode surface; D2D skipped",
                static_cast<int>(frame));
        } else {
            spdlog::error(
                "[native-residency] terminal output is not bound to the encode "
                "surface on frame {} (source={}, encode={}); refusing degraded "
                "copy lane",
                static_cast<int>(frame), source, encode_surface);
            exec_fb.reset();
        }
    } else if (encode_surface != runtime::kInvalidRenderSurfaceHandle) {
        spdlog::error("[backend] native encode copy has no source surface for frame {}",
                      static_cast<int>(frame));
        exec_fb.reset();
    }
    backend_.end_frame_batch();
    encode_batch_active_ = false;
    return exec_fb;
}

} // namespace chronon3d::graph::detail
