#include "../common/pipe_export_pipeline.hpp"

#include "../../../utils/video/encoder_config_resolution.hpp"

#include <chronon3d/cache/framebuffer_pool.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {
namespace {

void warmup_text_size_classes(cache::FramebufferPool& pool) {
    struct TextSizeClass { int w; int h; size_t count; const char* label; };
    const TextSizeClass layout[] = {
        {.w = 1920, .h = 900,  .count = 3, .label = "text-bbox+glow-pad"},
        {.w = 960,  .h = 540,  .count = 3, .label = "downsample-half"},
        {.w = 480,  .h = 270,  .count = 3, .label = "downsample-quarter"},
    };
    for (const auto& cls : layout) {
        const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(cls.w, cls.h);
        const auto n = pool.preallocate(cache::FramebufferPoolPreallocOptions{
            .width = bw,
            .height = bh,
            .count = cls.count,
            .clear = true,
            .touch_memory = false,
        });
        if (n > 0) {
            spdlog::info("[pool-warm] Pre-allocated {} buffers ({}) bucket {}x{} at startup",
                         n, cls.label, bw, bh);
        }
    }
}

} // namespace

void warmup_pipe_slots(PipeExportSession& session) {
    if (!session.full_graph_session || !session.device_runtime ||
        !session.full_graph_session->surface_backend ||
        !session.full_graph_session->surface_registry ||
        !session.encoder) {
        return;
    }
    auto& backend = *session.full_graph_session->surface_backend;
    if (!backend.supports_native_video_surface() ||
        !backend.supports_native_surfaces()) {
        return;
    }
    const bool is_native_nvenc =
        is_native_encoder_transport(session.opts.encoder.encoder_backend) &&
        hardware_nvenc_requested(session.opts.encoder.hardware_encoder);
    if (!is_native_nvenc) return;

    auto& slots = session.full_graph_session->execution_slots;
    auto& registry = *session.full_graph_session->surface_registry;
    const auto width = static_cast<std::uint32_t>(session.canvas_width);
    const auto height = static_cast<std::uint32_t>(session.canvas_height);
    if (width == 0 || height == 0) return;

    std::vector<GpuSlotPool::Lease> leases;
    leases.reserve(slots.capacity());
    for (std::size_t i = 0; i < slots.capacity(); ++i) {
        auto lease = slots.acquire();
        if (!lease.valid()) break;
        std::string reason;
        if (session.device_runtime->acquire_slot_surfaces(
                lease.slot(), registry, backend, width, height, reason)) {
            static const bool s_trace = std::getenv("CHRONON_CUDA_INTEROP_TRACE") != nullptr;
            if (s_trace) {
                spdlog::info("[interop-trace] WARMUP_SLOT slot_idx={} slot_id={} native_surface={} source_surface={}",
                             i, lease.slot().slot_id, lease.slot().native_surface, lease.slot().source_surface);
            }
            session.encoder->prepare_native_surface(backend, lease.slot().native_surface);
        } else {
            spdlog::warn("[pool-warm] Failed to pre-acquire surfaces for slot {}: {}", i, reason);
        }
        leases.push_back(std::move(lease));
    }
    spdlog::info("[pool-warm] Pre-allocated and pre-imported {} execution slots ({}x{}) for native NVENC",
                 leases.size(), width, height);
    leases.clear();
}

void warmup_pipe_pool(PipeExportSession& session) {

    if (!session.renderer_ptr() || !session.renderer_ptr()->framebuffer_pool()) return;

    const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(
        session.canvas_width, session.canvas_height);
    const auto prealloced = session.renderer_ptr()->framebuffer_pool()->preallocate(
        cache::FramebufferPoolPreallocOptions{
            .width = bw,
            .height = bh,
            .count = 4,
            .clear = true,
            .touch_memory = false,
        });
    if (prealloced > 0) {
        spdlog::info("[pool-warm] Pre-allocated {} canvas buffers ({}x{} bucket) at startup",
                     prealloced, bw, bh);
    }

    warmup_text_size_classes(*session.renderer_ptr()->framebuffer_pool());
}

} // namespace chronon3d::cli

