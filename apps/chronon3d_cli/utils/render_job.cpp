#include "render_job.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/render_telemetry.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#include <chronon3d/core/trace.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <vector>
#include <fstream>
#include <unordered_map>
#include <sstream>
#include <iomanip>

namespace chronon3d::cli {
    extern std::string g_command_line;
}

namespace chronon3d::cli {

namespace {

bool write_render_frame(const Composition& comp,
                        SoftwareRenderer& renderer,
                        Frame frame,
                        const FrameRange& range,
                        const std::string& output_pattern,
                        bool& ok,
                        std::vector<telemetry::FrameTelemetryRecord>& telemetry_frames,
                        double& total_render_ms,
                        double& total_encode_ms,
                        int& frames_written) {
    const auto layer_count = static_cast<int>(comp.evaluate(frame).layers().size());
    const auto hits_before = renderer.node_cache().stats().hits;
    const auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render_frame(comp, frame);
    const auto t1 = std::chrono::steady_clock::now();
    const auto hits_after = renderer.node_cache().stats().hits;
    const double dirty_ratio = renderer.last_dirty_area_ratio();

    if (!fb) {
        spdlog::error("Failed to render frame {}", frame);
        ok = false;
        return false;
    }

    const bool is_range = (range.start != range.end);
    const std::string path = format_path(output_pattern, frame, is_range);
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    const auto t_encode0 = std::chrono::steady_clock::now();

    ImageWriteOptions write_options;
    write_options.format = image_format_from_path(path);

    if (write_options.format == ImageFormat::Unknown) {
        spdlog::error("Unsupported image output format for path: {}", path);
        ok = false;
        return false;
    }

    if (!save_image(*fb, path, write_options)) {
        spdlog::error("Failed to save frame {} to {} as {}",
                      frame,
                      path,
                      image_format_name(write_options.format));
        ok = false;
        return false;
    }

    const auto t_encode1 = std::chrono::steady_clock::now();

    const double render_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double encode_ms = std::chrono::duration<double, std::milli>(t_encode1 - t_encode0).count();
    total_render_ms += render_ms;
    total_encode_ms += encode_ms;
    frames_written++;

    const bool hit = (hits_after > hits_before);

    // Record Legacy Telemetry CSV
    telemetry::record_render_telemetry({
        .event = "image_render",
        .frame = frame,
        .width = comp.width(),
        .height = comp.height(),
        .total_ms = std::chrono::duration<double, std::milli>(t_encode1 - t0).count(),
        .setup_ms = render_ms,
        .encode_ms = encode_ms,
        .cache_hit = hit ? 1 : 0,
        .layer_count = layer_count,
    });

    // Record Unified Telemetry Frame Record
    telemetry_frames.push_back({
        .frame_number = static_cast<int>(frame),
        .duration_ms = render_ms + encode_ms,
        .cache_hit = hit,
        .dirty_area_ratio = dirty_ratio
    });

    spdlog::info("Frame {} saved to {}", frame, path);
    return true;
}

} // namespace

std::optional<RenderJobPlan> plan_render_job(const CompositionRegistry& registry,
                                             const RenderArgs& args) {
    auto range = parse_frames(args.frames);
    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) {
        return std::nullopt;
    }

    RenderJobPlan plan;
    plan.range = range;
    plan.comp = std::move(resolved.comp);
    plan.comp_id = args.comp_id;
    plan.output = args.output;
    plan.settings = settings_from_args(args, !resolved.from_specscene, args.pipeline.diagnostic);
    plan.from_specscene = resolved.from_specscene;
    plan.trace_file = args.trace_file;
    plan.log_level = args.log_level;
    plan.benchmark_all = args.benchmark_all;
    plan.report = args.report;
    plan.warmup_renderer = args.pipeline.warmup_renderer;
    plan.warmup_framebuffers = args.pipeline.warmup_framebuffers;
    plan.warmup_dummy_frame = args.pipeline.warmup_dummy_frame;
    return plan;
}

bool execute_render_job(const CompositionRegistry& registry, const RenderJobPlan& plan) {
    const auto job_started_iso = telemetry::TelemetryManager::get_current_iso_time();
    const auto wall_t0 = std::chrono::steady_clock::now();

    const auto setup_t0 = std::chrono::steady_clock::now();
    auto renderer = create_renderer(registry, plan.settings);
    // Renderer warmup (preallocate framebuffers + optional dummy frame)
    if (plan.warmup_renderer) {
        runtime::warmup_renderer(*renderer, *plan.comp, runtime::RendererWarmupOptions{
            .width = plan.comp->width(),
            .height = plan.comp->height(),
            .framebuffer_count = plan.warmup_framebuffers,
            .preallocate_framebuffers = true,
            .touch_memory = true,
            .render_dummy_frame = plan.warmup_dummy_frame,
            .dummy_frame = 0,
            .quiet = (plan.log_level != "trace" && plan.log_level != "debug")
        });
    }
    renderer->trace()->clear();
    renderer->counters()->reset();
    const auto setup_t1 = std::chrono::steady_clock::now();

    if (plan.from_specscene && plan.settings.motion_blur.enabled) {
        spdlog::warn("Motion blur is ignored for specscene inputs in this build");
    }

    spdlog::info("Rendering {} [{} -> {} step {}]{}{}...",
                 plan.comp_id, plan.range.start, plan.range.end, plan.range.step,
                 plan.settings.motion_blur.enabled
                     ? fmt::format(" [MB {}smp {:.0f}°]",
                                   plan.settings.motion_blur.samples,
                                   plan.settings.motion_blur.shutter_angle)
                     : "",
                 plan.settings.ssaa_factor > 1.0f
                     ? fmt::format(" [SSAA {:.1f}x]", plan.settings.ssaa_factor)
                     : "");

    // Initialize the default telemetry manager stores
    telemetry::TelemetryManager::instance().initialize_default_stores();

    std::vector<telemetry::FrameTelemetryRecord> telemetry_frames;
    double total_render_ms = 0.0;
    double total_encode_ms = 0.0;
    int frames_written = 0;

    const int64_t effective_end = (plan.range.start == plan.range.end) ? plan.range.start + 1 : plan.range.end;
    bool ok = true;

    const auto loop_t0 = std::chrono::steady_clock::now();
    for (int64_t f = plan.range.start; f < effective_end; f += plan.range.step) {
        if (!write_render_frame(*plan.comp, *renderer, static_cast<Frame>(f), plan.range, plan.output, ok,
                                telemetry_frames, total_render_ms, total_encode_ms, frames_written)) {
            // keep going to report all failures, but preserve false
        }
    }
    const auto loop_t1 = std::chrono::steady_clock::now();

    spdlog::info("Render complete.");

    // Write legacy CSV and summary
    telemetry::flush_telemetry();

    if (!plan.trace_file.empty()) {
        spdlog::info("Writing performance trace to {}...", plan.trace_file);
        write_trace_json(*renderer->trace(), plan.trace_file);
    }

    const auto wall_t1 = std::chrono::steady_clock::now();
    const double wall_time_ms = std::chrono::duration<double, std::milli>(wall_t1 - wall_t0).count();

    // Commit and save structured telemetry
    const auto* counters = renderer->counters();
    telemetry::RenderTelemetryRecord run;
    run.run_id = telemetry::TelemetryManager::generate_uuid();
    run.composition_id = plan.comp_id;
    run.output_path = plan.output;
    run.success = ok;
    run.frames_total = static_cast<int>(telemetry_frames.size());
    run.frames_written = frames_written;
    run.wall_time_ms = wall_time_ms;
    run.render_ms = total_render_ms;
    run.encode_ms = total_encode_ms;
    run.effective_fps = wall_time_ms > 0 ? (run.frames_total * 1000.0 / wall_time_ms) : 0.0;

    run.pixels_touched = counters->pixels_touched.load(std::memory_order_relaxed);
    run.cache_hits = counters->cache_hits.load(std::memory_order_relaxed);
    run.cache_misses = counters->cache_misses.load(std::memory_order_relaxed);
    run.nodes_executed = counters->nodes_executed.load(std::memory_order_relaxed);
    run.layers_rendered = counters->layers_rendered.load(std::memory_order_relaxed);
    run.text_glyphs_rasterized = counters->text_glyphs_rasterized.load(std::memory_order_relaxed);
    run.images_sampled = counters->images_sampled.load(std::memory_order_relaxed);
    run.blur_pixels = counters->blur_pixels.load(std::memory_order_relaxed);
    run.simd_lerp_calls = counters->simd_lerp_calls.load(std::memory_order_relaxed);
    run.tiles_total = counters->tiles_total.load(std::memory_order_relaxed);
    run.tiles_hit = counters->tiles_hit.load(std::memory_order_relaxed);
    run.tiles_miss = counters->tiles_miss.load(std::memory_order_relaxed);
    run.tiles_partial = counters->tiles_partial.load(std::memory_order_relaxed);
    run.bytes_allocated_peak = telemetry::TelemetryManager::get_peak_memory_usage();
    run.node_cache_hash_collisions = counters->node_cache_hash_collisions.load(std::memory_order_relaxed);
    run.clear_calls = counters->clear_calls.load(std::memory_order_relaxed);
    run.clear_pixels = counters->clear_pixels.load(std::memory_order_relaxed);
    run.composite_calls = counters->composite_calls.load(std::memory_order_relaxed);
    run.composite_pixels = counters->composite_pixels.load(std::memory_order_relaxed);
    run.transform_calls = counters->transform_calls.load(std::memory_order_relaxed);
    run.transform_pixels = counters->transform_pixels.load(std::memory_order_relaxed);
    run.effect_stack_calls = counters->effect_stack_calls.load(std::memory_order_relaxed);
    run.effect_pixels = counters->effect_pixels.load(std::memory_order_relaxed);
    run.layer_culling_tests = counters->layer_culling_tests.load(std::memory_order_relaxed);
    run.layers_culled = counters->layers_culled.load(std::memory_order_relaxed);
    run.layers_visible = counters->layers_visible.load(std::memory_order_relaxed);
    run.framebuffer_allocations = counters->framebuffer_allocations.load(std::memory_order_relaxed);
    run.framebuffer_reuses = counters->framebuffer_reuses.load(std::memory_order_relaxed);
    run.framebuffer_bytes_allocated = counters->framebuffer_bytes_allocated.load(std::memory_order_relaxed);
    run.framebuffer_bytes_peak = counters->framebuffer_bytes_peak.load(std::memory_order_relaxed);

    run.started_at_iso = job_started_iso;
    run.finished_at_iso = telemetry::TelemetryManager::get_current_iso_time();

    // Pool metrics
    const auto pool_current_bytes = renderer->software_framebuffer_pool().current_bytes();
    const auto pool_available_count = renderer->software_framebuffer_pool().available_count();

    std::vector<telemetry::CounterTelemetryRecord> counters_list = {
        {"pixels_touched", run.pixels_touched},
        {"cache_hits", run.cache_hits},
        {"cache_misses", run.cache_misses},
        {"nodes_executed", run.nodes_executed},
        {"layers_rendered", run.layers_rendered},
        {"text_glyphs_rasterized", run.text_glyphs_rasterized},
        {"images_sampled", run.images_sampled},
        {"blur_pixels", run.blur_pixels},
        {"simd_lerp_calls", run.simd_lerp_calls},
        {"tiles_total", run.tiles_total},
        {"tiles_hit", run.tiles_hit},
        {"tiles_miss", run.tiles_miss},
        {"tiles_partial", run.tiles_partial},
        {"bytes_allocated_peak", run.bytes_allocated_peak},
        {"node_cache_hash_collisions", run.node_cache_hash_collisions},
        {"clear_calls", run.clear_calls},
        {"clear_pixels", run.clear_pixels},
        {"composite_calls", run.composite_calls},
        {"composite_pixels", run.composite_pixels},
        {"transform_calls", run.transform_calls},
        {"transform_pixels", run.transform_pixels},
        {"effect_stack_calls", run.effect_stack_calls},
        {"effect_pixels", run.effect_pixels},
        {"layer_culling_tests", run.layer_culling_tests},
        {"layers_culled", run.layers_culled},
        {"layers_visible", run.layers_visible},
        {"framebuffer_allocations", run.framebuffer_allocations},
        {"framebuffer_reuses", run.framebuffer_reuses},
        {"framebuffer_bytes_allocated", run.framebuffer_bytes_allocated},
        {"framebuffer_bytes_peak", run.framebuffer_bytes_peak},
        {"pool_current_bytes", pool_current_bytes},
        {"pool_available_count", pool_available_count}
    };

    std::vector<telemetry::PhaseTelemetryRecord> phases = {
        {"setup_renderer", std::chrono::duration<double, std::milli>(setup_t1 - setup_t0).count()},
        {"rendering_loop", std::chrono::duration<double, std::milli>(loop_t1 - loop_t0).count()}
    };

    if (plan.benchmark_all && renderer->trace()) {
        std::unordered_map<std::string, double> node_durations;
        for (const auto& ev : renderer->trace()->events()) {
            if (ev.category == "node_execute") {
                node_durations[ev.name] += ev.dur_us / 1000.0;
            }
        }
        for (const auto& [node_name, dur_ms] : node_durations) {
            phases.push_back({"node:" + node_name, dur_ms});
        }
    }

    // Flush per-node telemetry collected during graph execution
    auto node_events = telemetry::collect_node_telemetry();
    auto layer_events = telemetry::collect_layer_telemetry();
    auto cache_events = telemetry::collect_cache_telemetry();
    auto culling_events = telemetry::collect_culling_telemetry();
    auto text_events = telemetry::collect_text_telemetry();
    auto image_events = telemetry::collect_image_telemetry();
    auto tile_events = telemetry::collect_tile_telemetry();

    telemetry::TelemetryManager::instance().record_run(run, telemetry_frames, phases, counters_list,
                                                        node_events, layer_events,
                                                        cache_events, culling_events,
                                                        text_events, image_events,
                                                        tile_events);

    if (plan.report) {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
#if defined(_WIN32)
        localtime_s(&tm_buf, &now_time);
#else
        localtime_r(&now_time, &tm_buf);
#endif
        std::ostringstream file_name;
        file_name << "chronon3d-" 
                  << std::put_time(&tm_buf, "%Y%m%d-%H%M%S") 
                  << ".log";

        std::ofstream report_out(file_name.str());
        if (report_out) {
            report_out << "==========================================\n";
            report_out << "       CHRONON3D RUN EXECUTION REPORT     \n";
            report_out << "==========================================\n\n";
            report_out << "Command Line: " << cli::g_command_line << "\n";
            report_out << "Timestamp:    " << run.finished_at_iso << "\n";
            report_out << "Git Commit:   " << run.git_commit_short << "\n";
            report_out << "OS:           " << run.os << "\n";
            report_out << "CPU Model:    " << run.cpu_model << "\n";
            report_out << "Cores:        " << run.cores << "\n";
            report_out << "Compiler:     " << run.compiler_info << "\n";
            report_out << "Build Type:   " << run.build_type << "\n\n";
            report_out << "Run ID:         " << run.run_id << "\n";
            report_out << "Composition ID: " << run.composition_id << "\n";
            report_out << "Output Path:    " << run.output_path << "\n";
            report_out << "Frames Total:   " << run.frames_total << "\n";
            report_out << "Wall Time:      " << run.wall_time_ms << " ms\n";
            report_out << "Render Time:    " << run.render_ms << " ms\n";
            report_out << "Effective FPS:  " << run.effective_fps << "\n\n";
            report_out << "--- PERFORMANCE COUNTERS ---\n";
            for (const auto& c : counters_list) {
                report_out << std::left << std::setw(28) << c.counter_name << ": " << c.counter_value << "\n";
            }
            report_out << "\n--- PHASE DURATIONS ---\n";
            for (const auto& p : phases) {
                report_out << std::left << std::setw(28) << p.phase_name << ": " << p.duration_ms << " ms\n";
            }
            report_out << "\n--- FRAME BREAKDOWN ---\n";
            for (const auto& f : telemetry_frames) {
                report_out << "Frame " << std::setw(4) << f.frame_number 
                           << " | Dur: " << std::setw(8) << f.duration_ms << " ms"
                           << " | Cache Hit: " << (f.cache_hit ? "YES" : "NO ")
                           << " | Dirty Ratio: " << f.dirty_area_ratio << "\n";
            }
            spdlog::info("Execution report saved to {}", file_name.str());
        }
    }

    return ok;
}

} // namespace chronon3d::cli
