// ── Public API ───────────────────────────────────────────────────────────
std::string generate_execution_report(const RenderReportContext& ctx) {
    const chronon3d::telemetry::RenderTelemetryRecord& run = ctx.run;
    const auto& counters_list = ctx.counters;
    const auto& phases        = ctx.phases;
    const auto& frames        = ctx.frames;

    auto get_counter = [&](const char* name) -> uint64_t {
        for (const auto& c : counters_list) {
            if (c.counter_name == name) return c.counter_value;
        }
        return 0;
    };

    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_r(&now_time, &tm_buf);
    std::ostringstream file_name;
    file_name << "chronon3d-"
              << std::put_time(&tm_buf, "%Y%m%d-%H%M%S")
              << ".log";

    std::ofstream out(file_name.str());
    if (!out) {
        spdlog::error("Failed to open report file: {}", file_name.str());
        return {};
    }

    out << "==========================================\n";
    out << "       CHRONON3D RUN EXECUTION REPORT     \n";
    out << "==========================================\n\n";
    out << "Command Line: " << ctx.command_line << "\n";
    out << "Timestamp:    " << run.finished_at_iso << "\n";
    out << "Git Commit:   " << run.git_commit_short << "\n";
    out << "OS:           " << run.os << "\n";
    out << "CPU Model:    " << run.cpu_model << "\n";
    out << "Cores:        " << run.cores << "\n";
    out << "Compiler:     " << run.compiler_info << "\n";
    out << "Build Type:   " << run.build_type << "\n\n";
    out << "Run ID:         " << run.run_id << "\n";
    out << "Composition ID: " << run.composition_id << "\n";
    out << "Output Path:    " << run.output_path << "\n";
    out << "Frames Total:   " << run.frames_total << "\n";
    out << "Wall Time:      " << run.wall_time_ms << " ms\n";
    out << "Render Time:    " << run.render_ms << " ms\n";
    out << "Effective FPS:  " << run.effective_fps << "\n\n";

    out << "--- PERFORMANCE COUNTERS ---\n";
    for (const auto& c : counters_list) {
        out << std::left << std::setw(28) << c.counter_name
            << ": " << c.counter_value << "\n";
    }

    out << "\n--- NODE CACHE TOP ENTRIES BY PHYSICAL WEIGHT ---\n";
    if (ctx.node_cache_top_entries.empty()) {
        out << "entries: 0\n";
    } else {
        out << std::left << std::setw(18) << "Digest"
            << std::setw(14) << "Scope"
            << std::setw(16) << "Logical"
            << std::setw(16) << "Allocated"
            << "Bytes\n";
        for (const auto& entry : ctx.node_cache_top_entries) {
            out << std::left << std::setw(18) << entry.key.digest()
                << std::setw(14) << entry.key.scope.substr(0, 13)
                << std::setw(16) << fmt::format("{}x{}", entry.logical_width, entry.logical_height)
                << std::setw(16) << fmt::format("{}x{}", entry.allocated_width, entry.allocated_height)
                << entry.bytes << "\n";
        }
    }

    out << "\n--- PHASE DURATIONS ---\n";
    for (const auto& p : phases) {
        out << std::left << std::setw(28) << p.phase_name
            << ": " << p.duration_ms << " ms\n";
    }

    out << "\n--- FRAME BREAKDOWN ---\n";
    for (const auto& f : frames) {
        out << "Frame " << std::setw(4) << f.frame_number
            << " | Dur: " << std::setw(8) << f.duration_ms << " ms"
            << " | Cache Hit: " << (f.cache_hit ? "YES" : "NO ")
            << " | Dirty Ratio: " << f.dirty_area_ratio << "\n";
    }

    write_hot_work_attribution(out, get_counter);
    write_cleardiag_section(out, get_counter);

    const auto top_bottlenecks = chronon3d::telemetry::analyze_bottlenecks(run);
    out << "\n--- TOP BOTTLENECKS ---\n";
    if (top_bottlenecks.empty()) {
        out << "No measured critical-path phase available.\n";
    } else {
        for (std::size_t i = 0; i < top_bottlenecks.size(); ++i) {
            const auto& finding = top_bottlenecks[i];
            out << (i + 1) << ". " << finding.name << ": "
                << fmt::format("{:.2f} ms", finding.value_ms);
            if (finding.critical_path_share > 0.0) {
                out << fmt::format(" ({:.1f}% of wall)",
                                   finding.critical_path_share * 100.0);
            }
            out << "\n   Next investigation: " << finding.recommendation << "\n";
        }
    }

    out << "\n--- DETAILED PIPELINE TIMELINE ---\n";
    auto timeline_line = [&](const char* label, double value_ms) {
        out << std::left << std::setw(28) << label << ": "
            << fmt::format("{:.3f} ms", value_ms) << "\n";
    };
    timeline_line("scene_eval", run.phase_scene_eval_ms);
    timeline_line("gpu_execute", run.gpu_execute_ms);
    timeline_line("gpu_submit_cpu", run.gpu_submit_cpu_ms);
    timeline_line("gpu_wait_cpu", run.gpu_wait_cpu_ms);
    timeline_line("gpu_readback", run.gpu_readback_ms > 0.0
                                      ? run.gpu_readback_ms
                                      : run.phase_gpu_readback_ms);
    timeline_line("conversion", run.video_conversion_wall_ms);
    timeline_line("encode", run.phase_encode_ms > 0.0
                               ? run.phase_encode_ms
                               : run.ffmpeg_encode_total_ms);
    timeline_line("disk_io", run.phase_disk_io_ms);
    timeline_line("process_startup", run.process_startup_ms);
    timeline_line("ffprobe", run.ffprobe_wall_ms);
    timeline_line("sha256", run.sha256_wall_ms);
    out << "bytes_uploaded: " << run.gpu_upload_bytes
        << " | bytes_readback: " << run.gpu_readback_bytes << "\n";
    out << "submissions: " << run.gpu_submissions
        << " | passes: " << run.passes_executed
        << " | physical_surfaces_peak: " << run.physical_surfaces_peak
        << " | barriers: " << run.barrier_count << "\n";
    out << "framebuffer_allocations_per_frame: "
        << run.framebuffer_allocations_per_frame << "\n";

    write_bottleneck_diagnosis(out, get_counter, run);

    out.flush();
    out.close();
    spdlog::info("Execution report saved to {}", file_name.str());
    return file_name.str();
}
