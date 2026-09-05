// scene_native_output.cpp — native output synchronization and native
// encode-source residency for the scene render pipeline, extracted from
// scene.cpp (phases 0/8/11 native-surface contract).

#include "scene_native_output.hpp"

#include "../nodes/native_surface.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

namespace chronon3d::graph::detail {

void synchronize_native_output(RenderGraphContext& ctx,
                               const std::shared_ptr<Framebuffer>& framebuffer) {
    if (!framebuffer || !ctx.services.backend ||
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
    if (ctx.services.backend->supports_native_video_surface() &&
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
    if (ctx.services.backend->supports_native_video_surface()) {
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
    std::vector<float> rgba(static_cast<std::size_t>(framebuffer->width()) *
                            framebuffer->height() * 4);
    const auto result = ctx.services.backend->download_surface(
        framebuffer->surface_handle(), rgba);
    if (!result.ok()) {
        spdlog::error("[backend] native output synchronization failed: {}",
                      result.error().message);
        if (ctx.services.surface_registry) {
            (void)ctx.services.backend->release_surface(framebuffer->surface_handle());
            (void)ctx.services.surface_registry->release(framebuffer->surface_handle());
        }
        framebuffer->clear_surface_handle();
        return;
    }
    if (framebuffer->stride() == framebuffer->width()) {
        std::memcpy(framebuffer->data(), rgba.data(),
                    static_cast<std::size_t>(framebuffer->width()) * framebuffer->height() * sizeof(Color));
    } else {
        const std::size_t row_bytes = static_cast<std::size_t>(framebuffer->width()) * sizeof(Color);
        for (int y = 0; y < framebuffer->height(); ++y) {
            std::memcpy(framebuffer->pixels_row(y),
                        rgba.data() + static_cast<std::size_t>(y) * framebuffer->width() * 4,
                        row_bytes);
        }
    }
    if (ctx.services.surface_registry) {
        (void)ctx.services.backend->release_surface(framebuffer->surface_handle());
        (void)ctx.services.surface_registry->release(framebuffer->surface_handle());
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
        !ctx.services.backend || !ctx.services.surface_registry) {
        return;
    }
    (void)ctx.services.backend->release_surface(source);
    (void)ctx.services.surface_registry->release(source);
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
    spdlog::info("[ensure_source_diag] fb={} handle={} valid={} persistent={}",
                 static_cast<const void*>(framebuffer.get()), handle,
                 backend_.is_native_surface_valid(handle),
                 ctx_.policy.native_video_source_surface);
    // Native graph outputs are already resident and synchronized;
    // never copy them through the CPU-source ring a second time.
    if (handle != runtime::kInvalidRenderSurfaceHandle &&
        backend_.is_native_surface_valid(handle)) {
        return handle;
    }
    const auto persistent_source = ctx_.policy.native_video_source_surface;
    if (persistent_source != runtime::kInvalidRenderSurfaceHandle) {
        if (handle != runtime::kInvalidRenderSurfaceHandle &&
            handle != persistent_source && ctx_.services.surface_registry) {
            (void)backend_.release_surface(handle);
            (void)ctx_.services.surface_registry->release(handle);
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
            std::vector<float> rgba(rw * rh * 4);
            for (std::size_t y = 0; y < rh; ++y) {
                const auto* row = framebuffer->data() +
                    static_cast<std::size_t>(clip.y0 + static_cast<int>(y)) *
                        static_cast<std::size_t>(framebuffer->stride()) + clip.x0;
                std::memcpy(rgba.data() + y * rw * 4, row, rw * sizeof(Color));
            }
            profiling::GpuUploadProducerScope upload_scope(
                has_projected_surface
                    ? profiling::GpuUploadProducer::Projection
                    : profiling::GpuUploadProducer::Video);
            runtime::UploadTicket upload_ticket{};
            const auto full_upload = clip.x0 == 0 && clip.y0 == 0 &&
                clip.x1 == framebuffer->width() &&
                clip.y1 == framebuffer->height();
            const auto uploaded = full_upload
                ? backend_.upload_surface_async(handle, desc, rgba, upload_ticket)
                : backend_.upload_surface_region(
                      handle, desc, clip.x0, clip.y0,
                      static_cast<std::uint32_t>(rw),
                      static_cast<std::uint32_t>(rh), rgba);
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
    if (!ctx_.services.surface_registry) {
        return runtime::kInvalidRenderSurfaceHandle;
    }
    if (handle != runtime::kInvalidRenderSurfaceHandle) {
        (void)ctx_.services.surface_registry->release(handle);
        framebuffer->clear_surface_handle();
    }
    const runtime::SurfaceDesc desc = runtime::SurfaceDesc::make(
        static_cast<std::uint32_t>(framebuffer->width()),
        static_cast<std::uint32_t>(framebuffer->height()),
        runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::FrameTransient);
    handle = ctx_.services.surface_registry->create(desc);
    if (!backend_.create_surface(handle, desc).ok()) {
        (void)ctx_.services.surface_registry->release(handle);
        return runtime::kInvalidRenderSurfaceHandle;
    }
    std::vector<float> packed;
    const auto rgba = framebuffer_rgba_view(*framebuffer, packed);
    const auto upload_start = std::chrono::steady_clock::now();
    profiling::GpuUploadProducerScope upload_scope(
        profiling::GpuUploadProducer::Video);
    runtime::UploadTicket upload_ticket{};
    if (!backend_.upload_surface_async(handle, desc, rgba, upload_ticket).ok()) {
        (void)backend_.release_surface(handle);
        (void)ctx_.services.surface_registry->release(handle);
        return runtime::kInvalidRenderSurfaceHandle;
    }
    const auto upload_ms = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - upload_start).count() / 1000;
    if (ctx_.node_exec.counters) {
        ctx_.node_exec.counters->video_surface_upload_count.fetch_add(
            1, std::memory_order_relaxed);
        ctx_.node_exec.counters->video_surface_upload_bytes.fetch_add(
            static_cast<std::uint64_t>(rgba.size() * sizeof(float)),
            std::memory_order_relaxed);
        ctx_.node_exec.counters->cpu_full_surface_upload_bytes.fetch_add(
            static_cast<std::uint64_t>(rgba.size() * sizeof(float)),
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
    const auto source = ensure_native_source(framebuffer, frame);
    if (source == runtime::kInvalidRenderSurfaceHandle) {
        backend_.end_frame_batch();
        encode_batch_active_ = false;
        return std::shared_ptr<Framebuffer>{};
    }
    const auto copy = backend_.copy_surface_to_video_encode(
        source, ctx_.policy.native_video_encode_surface);
    if (!copy.ok()) {
        spdlog::error("[backend] native encode copy failed on reused frame {}: {}",
                      static_cast<int>(frame), copy.error().message);
        backend_.end_frame_batch();
        encode_batch_active_ = false;
        return std::shared_ptr<Framebuffer>{};
    }
    release_temporary_native_output(ctx_, framebuffer, source);
    backend_.end_frame_batch();
    encode_batch_active_ = false;
    return framebuffer;
}

std::shared_ptr<Framebuffer> NativeSourceResidency::finish_frame_encode(
    std::shared_ptr<Framebuffer> exec_fb, Frame frame) {
    if (!encode_batch_active_) return exec_fb;
    if (ctx_.policy.native_video_encode_surface !=
            runtime::kInvalidRenderSurfaceHandle &&
        exec_fb) {
        const auto source = ensure_native_source(exec_fb, frame);
        if (source == runtime::kInvalidRenderSurfaceHandle) {
            spdlog::error("[backend] native encode copy has no valid source surface for frame {}",
                          static_cast<int>(frame));
            exec_fb.reset();
        } else {
            const auto copy = backend_.copy_surface_to_video_encode(
                source, ctx_.policy.native_video_encode_surface);
            if (!copy.ok()) {
                spdlog::error("[backend] native encode copy failed for frame {}: {}",
                              static_cast<int>(frame), copy.error().message);
                exec_fb.reset();
            } else {
                release_temporary_native_output(ctx_, exec_fb, source);
            }
        }
    } else if (ctx_.policy.native_video_encode_surface !=
               runtime::kInvalidRenderSurfaceHandle) {
        spdlog::error("[backend] native encode copy has no source surface for frame {}",
                      static_cast<int>(frame));
        exec_fb.reset();
    }
    backend_.end_frame_batch();
    encode_batch_active_ = false;
    return exec_fb;
}

} // namespace chronon3d::graph::detail
