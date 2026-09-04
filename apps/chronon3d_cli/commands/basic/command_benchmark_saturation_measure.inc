if (!registry.contains(scene)) {
    spdlog::error("Unknown composition: {}", scene);
    return 1;
}

auto composition = registry.create(scene);
const auto compile_t0 = std::chrono::steady_clock::now();
auto compiled_result = chronon3d::compile_composition(
    composition, CompositionCompileContext{});
const double compile_us = std::chrono::duration<double, std::micro>(
    std::chrono::steady_clock::now() - compile_t0).count();
if (!compiled_result) {
    spdlog::error("Compilation failed: {}", compiled_result.error().message);
    return 1;
}
auto compiled = std::make_shared<const CompiledComposition>(
    std::move(compiled_result).value());

RenderSettings settings;
settings.dirty.enabled = true;
settings.disable_pixel_readback = true;
settings.motion_blur.mode = motion_blur_mode;
settings.motion_blur.samples = motion_blur_samples;
settings.motion_blur.shutter_angle_deg = 180.0f;
settings.motion_blur.shutter_phase_deg = -90.0f;
std::optional<Config> renderer_config;
if (backend != graph::BackendPreference::Auto) {
    Config cfg;
    cfg.set_backend_preference(backend);
    renderer_config = std::move(cfg);
}
auto renderer = create_renderer(
    registry, settings, std::move(renderer_config), std::filesystem::current_path());

auto snapshot_gpu = [&]() {
    std::vector<std::pair<std::string, std::uint64_t>> ordered;
    if (renderer->runtime().backend_attached()) {
        renderer->runtime().backend().export_gpu_telemetry_counters(ordered);
    }
    std::unordered_map<std::string, std::uint64_t> by_name;
    by_name.reserve(ordered.size());
    for (const auto& [name, value] : ordered) by_name.emplace(name, value);
    return std::make_pair(std::move(ordered), std::move(by_name));
};

spdlog::info("Warming up: 10 frames");
for (int i = 0; i < 10; ++i) {
    renderer->render_compiled(*compiled, static_cast<Frame>(i));
}

const auto* counters = renderer->counters();
const auto proc_start = read_proc_status();
const double cpu_start_seconds = read_process_cpu_seconds();
const uint64_t pre_alloc = counters->framebuffer_allocations.load(std::memory_order_relaxed);
const uint64_t pre_copies = counters->full_frame_copies.load(std::memory_order_relaxed);
const uint64_t pre_passes = counters->full_frame_passes.load(std::memory_order_relaxed);
const uint64_t pre_timeline_eval_us = counters->timeline_eval_wall_us.load(std::memory_order_relaxed);
const uint64_t pre_graph_total_us = counters->graph_total_wall_us.load(std::memory_order_relaxed);
const uint64_t pre_graph_refresh_us = counters->compiled_graph_refresh_wall_us.load(std::memory_order_relaxed);
const uint64_t pre_text_shape_ms = counters->text_shaping_wall_ms.load(std::memory_order_relaxed);
const uint64_t pre_text_layout_ms = counters->text_layout_wall_ms.load(std::memory_order_relaxed);
const uint64_t pre_asset_resolve_us = counters->image_resolve_wall_us.load(std::memory_order_relaxed);
auto [gpu_pre_ordered, gpu_pre] = snapshot_gpu();
(void)gpu_pre_ordered;

spdlog::info("Benchmarking {} for {} seconds...", scene, duration_sec);
using Clock = std::chrono::steady_clock;
const auto start_time = Clock::now();
std::vector<double> frame_times_ms;
frame_times_ms.reserve(static_cast<std::size_t>(duration_sec) * 30);
std::vector<double> reuse_frame_times;
std::vector<double> sparse_frame_times;
std::vector<double> full_rgb_frame_times;
std::vector<double> sparse_yuv_frame_times;
std::vector<double> full_yuv_frame_times;
std::vector<double> copy_gop_frame_times;

int frame_index = 10;
while (true) {
    const auto frame_start = Clock::now();
    renderer->render_compiled(*compiled, static_cast<Frame>(frame_index));
    const auto frame_end = Clock::now();
    const double elapsed_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
    frame_times_ms.push_back(elapsed_ms);

    const auto decision = renderer->last_execution_decision();
    switch (decision) {
        case graph::FrameExecutionPath::ReuseSurface:
            reuse_frame_times.push_back(elapsed_ms);
            break;
        case graph::FrameExecutionPath::SparseTiles:
            sparse_frame_times.push_back(elapsed_ms);
            break;
        case graph::FrameExecutionPath::FullRgb:
            full_rgb_frame_times.push_back(elapsed_ms);
            break;
        case graph::FrameExecutionPath::SparseYuv:
            sparse_yuv_frame_times.push_back(elapsed_ms);
            break;
        case graph::FrameExecutionPath::FullYuv:
            full_yuv_frame_times.push_back(elapsed_ms);
            break;
        case graph::FrameExecutionPath::CopyGop:
            copy_gop_frame_times.push_back(elapsed_ms);
            break;
    }

    ++frame_index;
    const auto now = Clock::now();
    const double elapsed_sec = std::chrono::duration<double>(now - start_time).count();
    if (elapsed_sec >= static_cast<double>(duration_sec)) break;
}

const auto end_time = Clock::now();
const double total_elapsed_sec = std::chrono::duration<double>(end_time - start_time).count();
const int total_frames = static_cast<int>(frame_times_ms.size());

const auto proc_end = read_proc_status();
const double cpu_end_seconds = read_process_cpu_seconds();
const uint64_t post_alloc = counters->framebuffer_allocations.load(std::memory_order_relaxed);
const uint64_t post_copies = counters->full_frame_copies.load(std::memory_order_relaxed);
const uint64_t post_passes = counters->full_frame_passes.load(std::memory_order_relaxed);
const uint64_t post_timeline_eval_us = counters->timeline_eval_wall_us.load(std::memory_order_relaxed);
const uint64_t post_graph_total_us = counters->graph_total_wall_us.load(std::memory_order_relaxed);
const uint64_t post_graph_refresh_us = counters->compiled_graph_refresh_wall_us.load(std::memory_order_relaxed);
const uint64_t post_text_shape_ms = counters->text_shaping_wall_ms.load(std::memory_order_relaxed);
const uint64_t post_text_layout_ms = counters->text_layout_wall_ms.load(std::memory_order_relaxed);
const uint64_t post_asset_resolve_us = counters->image_resolve_wall_us.load(std::memory_order_relaxed);
auto [gpu_post_ordered, gpu_post] = snapshot_gpu();

const auto gpu_absolute = [&](const std::string& name) -> std::uint64_t {
    const auto it = gpu_post.find(name);
    return it == gpu_post.end() ? 0 : it->second;
};

const auto delta_u64 = [](std::uint64_t after, std::uint64_t before) {
    return after >= before ? after - before : std::uint64_t{0};
};
const auto gpu_delta = [&](const std::string& name) -> std::uint64_t {
    const auto post_it = gpu_post.find(name);
    if (post_it == gpu_post.end()) return 0;
    const auto pre_it = gpu_pre.find(name);
    const std::uint64_t before = pre_it == gpu_pre.end() ? 0 : pre_it->second;
    return delta_u64(post_it->second, before);
};

const uint64_t total_ctx_sw = (proc_end.voluntary_cs + proc_end.involuntary_cs)
                               - (proc_start.voluntary_cs + proc_start.involuntary_cs);
const uint64_t peak_rss_kb = proc_end.peak_rss_kb;
const auto pool_stats = renderer->framebuffer_pool()
    ? renderer->framebuffer_pool()->stats()
    : cache::FramebufferPoolStats{};
const auto node_cache_stats = renderer->node_cache().stats();

auto sorted = frame_times_ms;
std::sort(sorted.begin(), sorted.end());

const double fps = static_cast<double>(total_frames) / total_elapsed_sec;
const double cpu_percent = total_elapsed_sec > 0.0
    ? std::max(0.0, (cpu_end_seconds - cpu_start_seconds) / total_elapsed_sec * 100.0)
    : 0.0;
const double p50 = percentile(sorted, 0.50);
const double p95 = percentile(sorted, 0.95);
const double p99 = percentile(sorted, 0.99);
const double frames_d = total_frames > 0 ? static_cast<double>(total_frames) : 1.0;
const double avg_frame_wall_us = total_frames > 0
    ? (std::accumulate(frame_times_ms.begin(), frame_times_ms.end(), 0.0) * 1000.0 / frames_d)
    : 0.0;

const uint64_t alloc_per_frame = static_cast<uint64_t>(
    static_cast<double>(post_alloc - pre_alloc) / frames_d);
const uint64_t copies_per_frame = static_cast<uint64_t>(
    static_cast<double>(post_copies - pre_copies) / frames_d);
const uint64_t passes_per_frame = static_cast<uint64_t>(
    static_cast<double>(post_passes - pre_passes) / frames_d);
const uint64_t peak_rss_mb = peak_rss_kb / 1024;

const auto caps = simd::detect_cpu_capabilities();
const auto logical_cpus = std::thread::hardware_concurrency();
PerfMetrics perf;
perf.available = "false";

const bool is_gpu = renderer->runtime().backend_attached() &&
                    renderer->runtime().backend().supports_native_surfaces();
const auto backend_name = renderer->runtime().backend_attached()
    ? (is_gpu ? "Vulkan" : "Software")
    : "None";

auto& out = std::cout;
out << "\n";
out << "CHRONON3D SATURATION REPORT\n";
out << "================================================\n";
out << "HARDWARE & BACKEND\n";
out << "Backend..................... " << backend_name << "\n";
out << "Native surfaces support..... " << (is_gpu ? "true" : "false") << "\n";
out << "Compiled program............ true\n";
out << "CPU model................... " << read_cpu_model() << "\n";
out << "Logical CPUs................ " << logical_cpus << "\n";
out << "NUMA nodes.................. " << count_numa_nodes() << "\n";
out << "Context switches............ " << total_ctx_sw << "\n";
out << "\n";
out << "SCENE\n";
out << "Composition................. " << scene << "\n";
out << "Duration.................... " << duration_sec << " s\n";
out << "Rendered frames............. " << total_frames << "\n";
out << "Motion blur................. mode=" << static_cast<int>(motion_blur_mode)
    << " samples=" << motion_blur_samples << "\n";
out << "\n";
out << "HOT-PATH CORPUS\n";
for (const auto& corpus_case : benchmark::kHotPathCorpus) {
    out << corpus_case.id << "  " << corpus_case.description
        << "  " << (corpus_case.native_path ? "native" : "baseline") << "\n";
}
out << "\n";

auto sorted_reuse = reuse_frame_times;
std::sort(sorted_reuse.begin(), sorted_reuse.end());
const double reuse_p50 = sorted_reuse.empty() ? 0.0 : percentile(sorted_reuse, 0.50);

auto sorted_sparse = sparse_frame_times;
std::sort(sorted_sparse.begin(), sorted_sparse.end());
const double sparse_p50 = sorted_sparse.empty() ? 0.0 : percentile(sorted_sparse, 0.50);

auto sorted_full = full_rgb_frame_times;
std::sort(sorted_full.begin(), sorted_full.end());
const double full_p50 = sorted_full.empty() ? 0.0 : percentile(sorted_full, 0.50);

auto sorted_sparse_yuv = sparse_yuv_frame_times;
std::sort(sorted_sparse_yuv.begin(), sorted_sparse_yuv.end());
const double sparse_yuv_p50 = sorted_sparse_yuv.empty() ? 0.0 : percentile(sorted_sparse_yuv, 0.50);

auto sorted_full_yuv = full_yuv_frame_times;
std::sort(sorted_full_yuv.begin(), sorted_full_yuv.end());
const double full_yuv_p50 = sorted_full_yuv.empty() ? 0.0 : percentile(sorted_full_yuv, 0.50);

auto sorted_copy_gop = copy_gop_frame_times;
std::sort(sorted_copy_gop.begin(), sorted_copy_gop.end());
const double copy_gop_p50 = sorted_copy_gop.empty() ? 0.0 : percentile(sorted_copy_gop, 0.50);
