// ── Internal helper: extract counter by name ─────────────────────────────
using CounterGetter = std::function<uint64_t(const char*)>;

// ── Internal helper: hot work attribution section ────────────────────────
static void write_hot_work_attribution(
    std::ofstream& out,
    const CounterGetter& get_counter)
{
    const uint64_t clr_memcpy = get_counter("clearnode_memcpy_wall_ms");
    const uint64_t clr_clear  = get_counter("clearnode_clear_wall_ms");
    const uint64_t c_blend    = get_counter("compositenode_blend_wall_ms");
    const uint64_t conv_ms    = get_counter("frame_conversion_copy_wall_ms");
    const uint64_t pipe_ms    = get_counter("video_pipe_write_wall_ms");
    const uint64_t n_exec     = get_counter("node_execute_actual_wall_ms");
    const uint64_t clear_px   = get_counter("clear_pixels");
    const uint64_t copy_px    = get_counter("clearnode_copy_pixels");
    const uint64_t comp_px    = get_counter("composite_pixels");
    const uint64_t xform_px   = get_counter("transform_pixels");
    const uint64_t touched_px = get_counter("pixels_touched");
    const uint64_t used_par_c = get_counter("used_parallel_clear");
    const uint64_t used_par_t = get_counter("used_parallel_transform");
    const uint64_t used_par_p = get_counter("used_parallel_composite");
    const uint64_t tbb_sum    = get_counter("tbb_active_workers_avg_sum");
    const uint64_t tbb_cnt    = get_counter("tbb_active_workers_avg_count");
    const double tbb_avg = tbb_cnt > 0
        ? static_cast<double>(tbb_sum) / static_cast<double>(tbb_cnt) : 1.0;
    const double xform_share = (touched_px > 0 && xform_px > 0)
        ? std::min(1.0, static_cast<double>(xform_px) / static_cast<double>(touched_px)) : 0.0;
    const uint64_t xform_wall = static_cast<uint64_t>(
        static_cast<double>(n_exec) * xform_share);
    const uint64_t conv_pixels = (conv_ms > 0) ? touched_px : 0;

    out << "\n--- HOT WORK ATTRIBUTION ---\n";
    out << std::left << std::setw(22) << "Work"
        << std::right << std::setw(10) << "Wall"
        << std::setw(12) << "Pixels"
        << std::setw(12) << "Est.Cores"
        << std::setw(12) << "CPU ms"
        << std::setw(10) << "GB/s" << "\n";
    out << std::string(78, '-') << "\n";

    auto attr_line = [&](const char* name, uint64_t w_ms, uint64_t px,
                          double cores, const char* gbs = nullptr) {
        if (w_ms == 0 && px == 0) return;
        const double w   = static_cast<double>(w_ms);
        const double cpu = w * cores;
        const double by  = static_cast<double>(px) * 16.0;
        const double ws  = w / 1000.0;
        const double gb  = ws > 0.0 ? (by / ws) / 1.0e9 : 0.0;
        out << std::left << std::setw(22) << name
            << std::right << std::setw(10) << fmt::format("{:.0f}", w)
            << std::setw(12) << px
            << std::setw(12) << fmt::format("{:.2f}", cores)
            << std::setw(12) << fmt::format("{:.0f}", cpu);
        if (gbs) {
            out << std::setw(10) << gbs;
        } else {
            out << std::setw(10) << fmt::format("{:.2f}", gb);
        }
        out << "\n";
    };

    attr_line("ClearNode memcpy",  clr_memcpy, copy_px,   used_par_c ? tbb_avg : 1.0);
    attr_line("ClearNode clear",   clr_clear,  clear_px,  used_par_c ? tbb_avg : 1.0);
    attr_line("Composite blend",   c_blend,    comp_px,   used_par_p ? tbb_avg : 1.0);
    attr_line("Transform rows",    xform_wall, xform_px,  used_par_t ? tbb_avg : 1.0);
    attr_line("Frame conversion",  conv_ms,    conv_pixels, tbb_avg);
    attr_line("Pipe write",        pipe_ms,    touched_px, 1.0, "blocked");

    const uint64_t avoided = get_counter("clearnode_bytes_avoided");
    if (avoided > 0) {
        out << std::left << std::setw(22) << "Bytes avoided (COW)"
            << std::right            << std::setw(10) << "—"
            << std::setw(12) << "---"
            << std::setw(12) << "---"
            << std::setw(12) << "---"
            << std::setw(10)
            << fmt::format("{:.2f} GB", static_cast<double>(avoided) / 1e9)
            << "\n";
    }

    const uint64_t total_attr = clr_memcpy + clr_clear + c_blend + xform_wall + conv_ms + pipe_ms;
    if (n_exec > 0) {
        const double cov = static_cast<double>(total_attr) / static_cast<double>(n_exec) * 100.0;
        out << "\nAttribution coverage: " << total_attr << " / " << n_exec
            << " (" << fmt::format("{:.1f}%", cov) << ")\n";
    }
}

static void write_cleardiag_section(
    std::ofstream& out,
    const CounterGetter& get_counter)
{
    const uint64_t memcpy_bytes   = get_counter("clearnode_memcpy_bytes");
    const uint64_t memcpy_calls   = get_counter("clearnode_memcpy_calls");
    const uint64_t detach_count   = get_counter("clearnode_detach_shared_count");
    const uint64_t partial_count  = get_counter("clearnode_partial_clip_copy_count");
    const uint64_t skip_count     = get_counter("clearnode_full_clip_skip_count");
    const uint64_t use_sum        = get_counter("prev_fb_use_count_sum");
    const uint64_t use_samples    = get_counter("prev_fb_use_count_samples");
    const uint64_t use_peak       = get_counter("prev_fb_use_count_peak");
    const uint64_t comp_copy_px   = get_counter("composite_copy_pixels");
    const uint64_t copy_px        = get_counter("clearnode_copy_pixels");

    if (memcpy_calls > 0 || detach_count > 0 || skip_count > 0 || comp_copy_px > 0) {
        out << "\n--- CLEARNODE COPY DIAGNOSTICS ---\n";
        out << "clearnode_memcpy_calls           : " << memcpy_calls << "\n";
        out << "clearnode_memcpy_bytes           : " << fmt::format("{:.2f} GB", static_cast<double>(memcpy_bytes) / 1e9) << "\n";
        out << "clearnode_detach_shared_count    : " << detach_count << "\n";
        out << "clearnode_partial_clip_copy_count: " << partial_count << "\n";
        out << "clearnode_full_clip_skip_count   : " << skip_count << "\n";
        out << "prev_fb_use_count_peak           : " << use_peak << "\n";
        if (use_samples > 0) {
            out << "prev_fb_use_count_avg            : "
                << fmt::format("{:.2f}", static_cast<double>(use_sum) / static_cast<double>(use_samples)) << "\n";
        }
        out << "composite_copy_pixels            : " << comp_copy_px << "\n";
        out << "clearnode_copy_pixels            : " << copy_px << "\n";
    }
}

static void write_bottleneck_diagnosis(
    std::ofstream& out,
    const CounterGetter& get_counter,
    const chronon3d::telemetry::RenderTelemetryRecord& run)
{
    const uint64_t sys_cores  = get_counter("system_logical_cores");
    const uint64_t cpu_user   = get_counter("process_cpu_user_ms");
    const uint64_t cpu_sys    = get_counter("process_cpu_sys_ms");
    const uint64_t rss_peak   = get_counter("process_rss_peak_mb");
    const uint64_t ram_avail  = get_counter("system_ram_available_min_mb");
    const uint64_t ffmpeg_blk = get_counter("ffmpeg_pipe_write_wall_ms");
    const uint64_t lev_par_c  = get_counter("level_parallel_count");
    const uint64_t lev_seq_c  = get_counter("level_sequential_count");
    const uint64_t cache_h    = get_counter("cache_hits");
    const uint64_t cache_m    = get_counter("cache_misses");
    const uint64_t dirty_px   = get_counter("dirty_pixels");
    const uint64_t tbb_peak_c = get_counter("tbb_active_workers_peak");
    const uint64_t n_exec     = get_counter("node_execute_actual_wall_ms");
    const uint64_t touched_px = get_counter("pixels_touched");
    const uint64_t used_par_c = get_counter("used_parallel_clear");
    const uint64_t used_par_t = get_counter("used_parallel_transform");
    const uint64_t used_par_p = get_counter("used_parallel_composite");
    const uint64_t conv_ms    = get_counter("frame_conversion_copy_wall_ms");
    const uint64_t clr_memcpy = get_counter("clearnode_memcpy_wall_ms");
    const uint64_t clr_clear  = get_counter("clearnode_clear_wall_ms");
    const uint64_t c_blend    = get_counter("compositenode_blend_wall_ms");
    const uint64_t pipe_ms    = get_counter("video_pipe_write_wall_ms");
    const uint64_t tbb_sum    = get_counter("tbb_active_workers_avg_sum");
    const uint64_t tbb_cnt    = get_counter("tbb_active_workers_avg_count");
    const double tbb_avg = tbb_cnt > 0
        ? static_cast<double>(tbb_sum) / static_cast<double>(tbb_cnt) : 1.0;

    const uint64_t cpu_total  = cpu_user + cpu_sys;
    const double render_ms = run.render_ms > 0 ? run.render_ms : 1.0;
    const double eff_cores = (run.wall_time_ms > 0 && cpu_total > 0)
        ? static_cast<double>(cpu_total) / run.wall_time_ms : 0.0;
    const double cpu_util = (sys_cores > 0)
        ? eff_cores / static_cast<double>(sys_cores) * 100.0 : 0.0;
    const double cache_rate = (cache_h + cache_m) > 0
        ? static_cast<double>(cache_h) / static_cast<double>(cache_h + cache_m) : 0.0;
    const double dirty_ratio = (touched_px > 0)
        ? static_cast<double>(dirty_px) / static_cast<double>(touched_px) : 1.0;
    const double ffmpeg_share = render_ms > 0
        ? static_cast<double>(ffmpeg_blk) / render_ms * 100.0 : 0.0;

    const double xform_share = (touched_px > 0 && run.transform_pixels > 0)
        ? std::min(1.0, static_cast<double>(run.transform_pixels) /
                        static_cast<double>(touched_px)) : 0.0;
    const uint64_t xform_wall = static_cast<uint64_t>(
        static_cast<double>(n_exec) * xform_share);

    const uint64_t total_attr = clr_memcpy + clr_clear + c_blend + xform_wall + conv_ms + pipe_ms;
    const double attr_cov = n_exec > 0
        ? static_cast<double>(total_attr) / static_cast<double>(n_exec) * 100.0 : 0.0;

    auto status = [](const char* label, const char* st) {
        return fmt::format("{:<26} {}", label, st);
    };

    out << "\n--- BOTTLENECK DIAGNOSIS ---\n";
    const char* cpu_st = (cpu_util >= 65.0) ? "GOOD"
                       : (cpu_util >= 40.0) ? "MODERATE"
                       : "LIMITED";
    out << status("CPU saturation", cpu_st)
        << fmt::format("  ({:.1f}% of {} cores)", cpu_util, sys_cores) << "\n";

    const char* mem_st = (ram_avail > 0 && rss_peak > 0)
        ? ((static_cast<double>(rss_peak) / static_cast<double>(ram_avail) < 0.25) ? "SAFE"
         : (static_cast<double>(rss_peak) / static_cast<double>(ram_avail) < 0.50) ? "MODERATE"
         : "WARNING")
        : "UNKNOWN";
    out << status("Memory pressure", mem_st);
    if (rss_peak > 0 && ram_avail > 0)
        out << fmt::format("  ({} MB RSS / {} MB avail)", rss_peak, ram_avail);
    out << "\n";

    const char* enc_st = (ffmpeg_share < 10.0) ? "GOOD"
                       : (ffmpeg_share < 25.0) ? "MODERATE BOTTLENECK"
                       : "MAJOR BOTTLENECK";
    out << status("Encoder pipe", enc_st)
        << fmt::format("  ({:.0f}% of render time blocked)", ffmpeg_share) << "\n";

    const char* gph_st = (lev_par_c > lev_seq_c) ? "STRONG"
                       : (lev_par_c > 0 && lev_par_c == lev_seq_c) ? "MODERATE"
                       : (lev_seq_c > lev_par_c) ? "LIMITED BY DAG"
                       : "UNKNOWN";
    out << status("Graph parallelism", gph_st)
        << fmt::format("  ({} parallel / {} sequential levels)", lev_par_c, lev_seq_c) << "\n";

    const bool any_par = (used_par_c > 0 || used_par_t > 0 || used_par_p > 0);
    const char* nod_st = (any_par && tbb_peak_c >= 4) ? "STRONG"
                       : (any_par) ? "MODERATE"
                       : "WEAK";
    out << status("Node parallelism", nod_st)
        << fmt::format("  (TBB peak: {} workers)", tbb_peak_c) << "\n";

    const double conv_share = render_ms > 0
        ? static_cast<double>(conv_ms) / render_ms * 100.0 : 0.0;
    const char* cnv_st = (conv_share < 10.0) ? "GOOD"
                       : (conv_share < 20.0) ? "MODERATE BOTTLENECK"
                       : "MAJOR BOTTLENECK";
    out << status("Frame conversion", cnv_st)
        << fmt::format("  ({:.0f}% of render time)", conv_share) << "\n";

    const char* ch_st = (cache_rate >= 0.80) ? "EXCELLENT"
                      : (cache_rate >= 0.50) ? "GOOD"
                      : "MODERATE";
    out << status("Cache efficiency", ch_st)
        << fmt::format("  ({:.0f}% hit rate)", cache_rate * 100.0) << "\n";

    const char* dr_st = (dirty_ratio <= 0.20) ? "EXCELLENT"
                      : (dirty_ratio <= 0.50) ? "GOOD"
                      : "MODERATE";
    out << status("Dirty rect efficiency", dr_st)
        << fmt::format("  ({:.0f}% dirty)", dirty_ratio * 100.0) << "\n";

    const char* ha_st = (attr_cov >= 90.0) ? "EXCELLENT"
                      : (attr_cov >= 80.0) ? "GOOD"
                      : "NEEDS WORK";
    out << status("Hot attribution", ha_st)
        << fmt::format("  ({:.0f}% explained)", attr_cov) << "\n";

    out << "\n";
    if (ffmpeg_share >= 25.0) {
        out << "Primary bottleneck: Encoder pipe (FFmpeg subprocess).\n";
        out << "Recommendation: Increase encoder queue depth or use native encoder path.\n";
    } else if (conv_share >= 20.0) {
        out << "Primary bottleneck: Frame conversion (RGBA->YUV).\n";
        out << "Recommendation: Benchmark Swscale vs Packed; consider direct YUV path.\n";
    } else if (cpu_util < 40.0) {
        out << "Primary bottleneck: CPU underutilization.\n";
        out << "Recommendation: Increase parallelism thresholds or enable frame-level parallelism.\n";
        out << "Do not increase render workers beyond " << (sys_cores > 0 ? sys_cores - 2 : 6)
            << " to leave margin for OS + encoder.\n";
    } else {
        out << "No single dominant bottleneck -- system is well-balanced for this workload.\n";
    }
}
