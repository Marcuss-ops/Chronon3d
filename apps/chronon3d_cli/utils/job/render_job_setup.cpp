#include "render_job_setup.hpp"
#include "render_job_detail.hpp"
#include "cli_render_utils.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/cache/compiled_graph_cache.hpp>
#include <chronon3d/runtime/render_runtime.hpp>

namespace chronon3d::cli {

void setup_render_job(const CompositionRegistry& registry,
                      const RenderJob& job,
                      RenderJobSetupResult& out,
                      std::shared_ptr<SoftwareRenderer> warm_renderer) {
    profiling::g_live_framebuffer_bytes.store(0, std::memory_order_relaxed);
    profiling::g_peak_live_framebuffer_bytes.store(0, std::memory_order_relaxed);

    out.job_started_iso = chronon3d::telemetry::TelemetryManager::get_current_iso_time();
    out.wall_t0 = profiling::now();

    out.setup_t0 = profiling::now();
    out.renderer = std::move(warm_renderer);
    if (out.renderer) {
        // A daemon reuses the renderer object, but every RenderJob is a new
        // temporal session.  Drop evaluated node values and temporal history
        // before preparing the next plan; keep image/GPU/font/pipeline caches
        // warm so persistence remains useful without leaking state from the
        // previous job into frame zero.
        out.renderer->reset_frame_value_cache();
        out.renderer->reset_temporal_history();
        out.renderer->set_settings(job.settings);
        if (job.execution.assets_root) {
            out.renderer->runtime().resolver().mount(*job.execution.assets_root);
        }
    } else {
        out.renderer = create_renderer(
            registry, job.settings, job.execution.config, job.execution.assets_root);
    }
    const auto renderer_t1 = profiling::now();

    if (!out.renderer) {
        out.setup_t1 = renderer_t1;
        return;
    }

    // The plan preparation snapshot and the renderer runtime are separate
    // ownership boundaries.  Re-assert the job's explicit mount here so the
    // runtime-owned FontEngine and every frame-evaluation path observe the
    // same AssetResolver root, including renderers created through a warm
    // or alternate CLI setup path.
    if (job.execution.assets_root) {
        out.renderer->runtime().resolver().mount(*job.execution.assets_root);
    }

    if (out.renderer->counters()) {
        const auto setup_ms = static_cast<uint64_t>(
            profiling::duration_ms(out.setup_t0, renderer_t1));
        out.renderer->counters()->setup_graph_parsing_wall_ms.fetch_add(
            setup_ms, std::memory_order_relaxed);
    }

    const auto warmup_t0 = profiling::now();
    const auto preparation = runtime::prepare_render(
        out.renderer.get(), *job.compiled,
        runtime::RenderPreparationOptions{
            .preflight_mode = PreflightMode::FullComposition,
            .resources = {},
            .warmup_renderer = job.execution.warmup_renderer,
            .warmup = runtime::RendererWarmupOptions{
                .width = job.metadata.width,
                .height = job.metadata.height,
                .framebuffer_count = job.execution.warmup_framebuffers,
                .preallocate_framebuffers = true,
                .touch_memory = true,
                .render_dummy_frame = job.execution.warmup_dummy_frame,
                .dummy_frame = 0,
                .quiet = (job.execution.log_level != "trace" &&
                          job.execution.log_level != "debug")
            },
            .reference_frame = 0,
        });
    out.preparation_ok = preparation.ok();
    out.preparation_diagnostic = preparation.diagnostic();
    if (!out.preparation_ok) {
        spdlog::error("Render preparation failed for '{}': {}",
                      job.comp_id, out.preparation_diagnostic);
    }

    // The compiled graph already owns the canonical lifetime/aliasing
    // metadata. Convert it once during preparation so final telemetry reads
    // the same logical-resource contract as execution.
    if (const auto* graph = out.renderer->graph_cache().peek(
            job.metadata.width, job.metadata.height); graph != nullptr) {
        runtime::ResourcePlanner planner;
        for (std::size_t id = 0;
             id < graph->resource_table().resources.size(); ++id) {
            const auto& allocation = graph->resource_table().resources[id];
            if (allocation.producer == graph::k_invalid_node) {
                continue;
            }
            const bool persistent = allocation.persistent || allocation.async_use;
            runtime::ResourceRequest request;
            request.id = "GraphNode[" + std::to_string(id) + "]";
            request.desc = allocation.desc;
            request.first = persistent ? 0 : allocation.first_level;
            request.last = persistent ? 0 : allocation.last_level;
            planner.add(std::move(request));
        }
        out.resource_plan = planner.build();
    }
    const auto warmup_t1 = profiling::now();

    if (preparation.warmup_performed && out.renderer->counters()) {
        const auto warmup_ms = static_cast<uint64_t>(
            profiling::duration_ms(warmup_t0, warmup_t1));
        out.renderer->counters()->setup_pool_preallocation_wall_ms.fetch_add(
            warmup_ms, std::memory_order_relaxed);

        out.saved_fb_alloc = out.renderer->counters()->framebuffer_allocations.load(std::memory_order_relaxed);
        out.saved_fb_reuses = out.renderer->counters()->framebuffer_reuses.load(std::memory_order_relaxed);
        out.saved_fb_bytes = out.renderer->counters()->framebuffer_bytes_allocated.load(std::memory_order_relaxed);
        out.saved_fb_peak = out.renderer->counters()->framebuffer_bytes_peak.load(std::memory_order_relaxed);
    }

    if (out.renderer->counters()) {
        out.renderer->counters()->reset();
        out.renderer->counters()->framebuffer_allocations.store(out.saved_fb_alloc, std::memory_order_relaxed);
        out.renderer->counters()->framebuffer_reuses.store(out.saved_fb_reuses, std::memory_order_relaxed);
        out.renderer->counters()->framebuffer_bytes_allocated.store(out.saved_fb_bytes, std::memory_order_relaxed);
        out.renderer->counters()->framebuffer_bytes_peak.store(out.saved_fb_peak, std::memory_order_relaxed);
    }

    chronon3d::telemetry::clear_telemetry_stores();
    out.setup_t1 = profiling::now();
}

} // namespace chronon3d::cli
