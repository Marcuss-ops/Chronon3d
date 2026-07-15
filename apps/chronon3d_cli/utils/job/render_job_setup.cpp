#include "render_job_setup.hpp"
#include "render_job_detail.hpp"
#include "cli_render_utils.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

namespace chronon3d::cli {

void setup_render_job(const CompositionRegistry& registry,
                      RenderJob& job,
                      RenderJobSetupResult& out) {
    profiling::g_live_framebuffer_bytes.store(0, std::memory_order_relaxed);
    profiling::g_peak_live_framebuffer_bytes.store(0, std::memory_order_relaxed);

    out.job_started_iso = chronon3d::telemetry::TelemetryManager::get_current_iso_time();
    out.wall_t0 = profiling::now();

    out.setup_t0 = profiling::now();
    out.renderer = create_renderer(
        registry, job.settings, std::move(job.execution.config));
    const auto renderer_t1 = profiling::now();

    if (!out.renderer) {
        out.setup_t1 = renderer_t1;
        return;
    }

    if (out.renderer->counters()) {
        const auto setup_ms = static_cast<uint64_t>(
            profiling::duration_ms(out.setup_t0, renderer_t1));
        out.renderer->counters()->setup_graph_parsing_ms.fetch_add(
            setup_ms, std::memory_order_relaxed);
    }

    if (job.execution.warmup_renderer) {
        const auto warmup_t0 = profiling::now();
        runtime::warmup_renderer(*out.renderer, *job.comp, runtime::RendererWarmupOptions{
            .width = static_cast<int>(job.comp->width()),
            .height = static_cast<int>(job.comp->height()),
            .framebuffer_count = job.execution.warmup_framebuffers,
            .preallocate_framebuffers = true,
            .touch_memory = true,
            .render_dummy_frame = job.execution.warmup_dummy_frame,
            .dummy_frame = 0,
            .quiet = (job.execution.log_level != "trace" &&
                      job.execution.log_level != "debug")
        });
        const auto warmup_t1 = profiling::now();

        if (out.renderer->counters()) {
            const auto warmup_ms = static_cast<uint64_t>(
                profiling::duration_ms(warmup_t0, warmup_t1));
            out.renderer->counters()->setup_pool_preallocation_ms.fetch_add(
                warmup_ms, std::memory_order_relaxed);

            out.saved_fb_alloc = out.renderer->counters()->framebuffer_allocations.load(std::memory_order_relaxed);
            out.saved_fb_reuses = out.renderer->counters()->framebuffer_reuses.load(std::memory_order_relaxed);
            out.saved_fb_bytes = out.renderer->counters()->framebuffer_bytes_allocated.load(std::memory_order_relaxed);
            out.saved_fb_peak = out.renderer->counters()->framebuffer_bytes_peak.load(std::memory_order_relaxed);
        }
    }

    out.renderer->counters()->reset();

    if (out.renderer->counters()) {
        out.renderer->counters()->framebuffer_allocations.store(out.saved_fb_alloc, std::memory_order_relaxed);
        out.renderer->counters()->framebuffer_reuses.store(out.saved_fb_reuses, std::memory_order_relaxed);
        out.renderer->counters()->framebuffer_bytes_allocated.store(out.saved_fb_bytes, std::memory_order_relaxed);
        out.renderer->counters()->framebuffer_bytes_peak.store(out.saved_fb_peak, std::memory_order_relaxed);
    }

    chronon3d::telemetry::clear_telemetry_stores();
    out.setup_t1 = profiling::now();
}

} // namespace chronon3d::cli
