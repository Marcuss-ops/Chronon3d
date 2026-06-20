#include "render_job_setup.hpp"
#include "render_job_detail.hpp"
#include "cli_render_utils.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#endif
#include <chronon3d/assets/asset_registry.hpp>
#include "../../cli_init.hpp"

#include <spdlog/spdlog.h>

#include <filesystem>

namespace chronon3d::cli {

void setup_render_job(const CompositionRegistry& registry,
                      const RenderJobPlan& plan,
                      RenderJobSetupResult& out) {
    // Mount current working directory as asset root so relative asset paths
    // (fonts, images, etc.) resolve correctly.
    auto& assets = cli_asset_registry();
    assets.mount(std::filesystem::current_path());
    // Also set the default assets root for deep rendering code that
    // uses resolve_asset_path() without explicit context.
    detail::set_default_assets_root(
        std::filesystem::current_path().string());

    profiling::g_live_framebuffer_bytes.store(0, std::memory_order_relaxed);
    profiling::g_peak_live_framebuffer_bytes.store(0, std::memory_order_relaxed);

    out.job_started_iso =
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
        chronon3d::telemetry::TelemetryManager::get_current_iso_time();
#else
        "";
#endif
    out.wall_t0 = profiling::now();

    // ── Renderer creation ──────────────────────────────────────────────
    out.setup_t0 = profiling::now();
    out.renderer = create_renderer(registry, plan.settings, plan.config);
    const auto renderer_t1 = profiling::now();
    if (out.renderer->counters()) {
        const auto setup_ms = static_cast<uint64_t>(
            profiling::duration_ms(out.setup_t0, renderer_t1));
        out.renderer->counters()->setup_graph_parsing_ms.fetch_add(setup_ms, std::memory_order_relaxed);
    }

    // ── Warmup (preallocate framebuffers + optional dummy frame) ────────
    if (plan.warmup_renderer) {
        const auto warmup_t0 = profiling::now();
        runtime::warmup_renderer(*out.renderer, *plan.comp, runtime::RendererWarmupOptions{
            .width = static_cast<int>(plan.comp->width()),
            .height = static_cast<int>(plan.comp->height()),
            .framebuffer_count = plan.warmup_framebuffers,
            .preallocate_framebuffers = true,
            .touch_memory = true,
            .render_dummy_frame = plan.warmup_dummy_frame,
            .dummy_frame = 0,
            .quiet = (plan.log_level != "trace" && plan.log_level != "debug")
        });
        const auto warmup_t1 = profiling::now();

        if (out.renderer->counters()) {
            const auto warmup_ms = static_cast<uint64_t>(
                profiling::duration_ms(warmup_t0, warmup_t1));
            out.renderer->counters()->setup_pool_preallocation_ms.fetch_add(warmup_ms, std::memory_order_relaxed);

            out.saved_fb_alloc = out.renderer->counters()->framebuffer_allocations.load(std::memory_order_relaxed);
            out.saved_fb_reuses = out.renderer->counters()->framebuffer_reuses.load(std::memory_order_relaxed);
            out.saved_fb_bytes = out.renderer->counters()->framebuffer_bytes_allocated.load(std::memory_order_relaxed);
            out.saved_fb_peak = out.renderer->counters()->framebuffer_bytes_peak.load(std::memory_order_relaxed);
        }
    }

    // ── Reset counters (preserve warmup framebuffer stats) ──────────────
    out.renderer->counters()->reset();

    if (out.renderer->counters()) {
        out.renderer->counters()->framebuffer_allocations.store(out.saved_fb_alloc, std::memory_order_relaxed);
        out.renderer->counters()->framebuffer_reuses.store(out.saved_fb_reuses, std::memory_order_relaxed);
        out.renderer->counters()->framebuffer_bytes_allocated.store(out.saved_fb_bytes, std::memory_order_relaxed);
        out.renderer->counters()->framebuffer_bytes_peak.store(out.saved_fb_peak, std::memory_order_relaxed);
    }

    // ── Clear per-event telemetry stores after warmup ───────────────────
    // Atomic counters were reset above, so Hot Nodes events need to be
    // in sync with nodes_executed / composite_calls atomics.
    chronon3d::telemetry::clear_telemetry_stores();

    out.setup_t1 = profiling::now();
}

} // namespace chronon3d::cli
