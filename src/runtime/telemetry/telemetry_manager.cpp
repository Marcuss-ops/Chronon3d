#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <chronon3d/runtime/telemetry/sqlite_telemetry_store.hpp>
#else
#include <chronon3d/runtime/telemetry/null_telemetry_store.hpp>
#endif
#include <spdlog/spdlog.h>
#include <atomic>
#include <filesystem>
#include <thread>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <fstream>
#include <sys/resource.h>

namespace chronon3d::telemetry {

namespace {

std::string get_telemetry_directory() {
    if (const char* env = std::getenv("CHRONON3D_TELEMETRY_PATH")) {
        return env;
    }
    std::filesystem::path home_path;
    if (const char* home = std::getenv("HOME")) {
        home_path = home;
    } else {
        home_path = "/tmp";
    }
    return (home_path / ".chronon3d" / "telemetry").string();
}

// ── TICKET-122 ── Get_peak_memory_usage cache ──────────────────────────────
//
// Single-file-static anonymous struct (program-lifetime) with its own
// ~1 Hz worker thread that scans /proc/self/status for VmHWM and
// maintains a monotonic peak via CAS.  TelemetryManager::get_peak_memory_usage
// reduces to a single atomic load; callers (only record_run) no longer
// block on a synchronous file-open + string-scan on the record path.
//
// Pause-on-shutdown: the destructor sets `stop` and joins; the worst-
// case wait is one tick (~1s) when the worker is mid-sleep.
//
// Why per-TU instead of extending SystemMetricsCollector: Cat-2 freeze
// forbids new public API symbols; inlining the cache in this TU keeps
// the public surface untouched (get_peak_memory_usage signature is
// preserved verbatim; only its body changed).
struct PeakMemoryCache {
    std::atomic<bool>     stop{false};
    std::atomic<uint64_t> max_vmhwm_bytes{0};  // monotonic CAS-updated peak
    std::thread           worker;

    static uint64_t parse_vmhwm_internal() {
        std::ifstream status("/proc/self/status");
        std::string line;
        while (std::getline(status, line)) {
            if (line.rfind("VmHWM:", 0) == 0) {
                std::istringstream iss(line.substr(6));
                uint64_t kb = 0;
                iss >> kb;
                if (kb > 0) return kb * 1024ULL;
                break;
            }
        }
        // Fallback to getrusage when /proc/self/status did not report
        // VmHWM (e.g. non-Linux build path or older kernel).
        struct rusage usage {};
        if (getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss > 0) {
            return static_cast<uint64_t>(usage.ru_maxrss) * 1024ULL;
        }
        return 0;
    }

    // TICKET-122 -- extracted primed-update body; called both as the
    // ctor's synchronous first read and as the worker's per-tick body.
    // Acquire-load on the previous peak pairs with the release-CAS on
    // success so the consumer thread (TelemetryManager::get_peak_memory_usage)
    // sees a coherent monotonic peak value across calls.
    void prime_once_internal() {
        uint64_t current = parse_vmhwm_internal();
        uint64_t prev = max_vmhwm_bytes.load(std::memory_order_acquire);
        while (current > prev) {
            if (max_vmhwm_bytes.compare_exchange_weak(prev, current,
                                                      std::memory_order_release,
                                                      std::memory_order_relaxed)) {
                break;
            }
        }
    }
    PeakMemoryCache() {
        // TICKET-122 -- prime synchronously BEFORE spawning the worker
        // so the very first call into get_peak_memory_usage() observes
        // a real VmHWM value (the OLD implementation returned 0 for up
        // to ~1s until the worker had completed its first tick).
        prime_once_internal();
        worker = std::thread([this]() {
            // Acquire-load pairs with the dtor's release-store so the
            // worker respects shutdown within at most one tick (<1s).
            while (!stop.load(std::memory_order_acquire)) {
                prime_once_internal();
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
    }
    ~PeakMemoryCache() {
        stop.store(true, std::memory_order_release);
        if (worker.joinable()) worker.join();
    }
};

// Function-local static (Meyers singleton): thread-safe init per
// [stmt.dcl]/3 in C++11 onwards.  Program-lifetime; worker runs once.
PeakMemoryCache& peak_memory_cache() {
    static PeakMemoryCache inst;
    return inst;
}

} // namespace

TelemetryManager& TelemetryManager::instance() {
    static TelemetryManager inst;
    return inst;
}

TelemetryManager::TelemetryManager() = default;

void TelemetryManager::add_store(std::shared_ptr<TelemetryStore> store) {
    if (store) {
        m_stores.push_back(std::move(store));
    }
}

void TelemetryManager::clear_stores() {
    m_stores.clear();
}

void TelemetryManager::initialize_default_stores() {
    clear_stores();

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
    std::string base_dir = get_telemetry_directory();
    spdlog::info("[telemetry] Initializing default stores in base directory: {}", base_dir);
    
    // Ensure base directory exists
    std::error_code ec;
    std::filesystem::create_directories(base_dir, ec);

    // 2. SQLite Store
    // Preference: local output/telemetry.db if we are in a workspace
    const std::filesystem::path sqlite_path = resolve_sqlite_telemetry_path();
    spdlog::info("[telemetry] Resolving SQLite path to: {}", sqlite_path.string());
    
    auto sqlite_store = std::make_shared<SqliteTelemetryStore>();
    if (sqlite_store->initialize(sqlite_path.string())) {
        spdlog::info("[telemetry] Successfully initialized SQLite store at {}", sqlite_path.string());
        add_store(std::move(sqlite_store));
    } else {
        spdlog::warn("[telemetry] Failed to initialize workspace SQLite store at {}; falling back to user telemetry DB",
                     sqlite_path.string());
        const std::filesystem::path fallback_path =
            std::filesystem::path(get_telemetry_directory()) / "chronon3d_render_history.sqlite";
        auto fallback_store = std::make_shared<SqliteTelemetryStore>();
        if (fallback_store->initialize(fallback_path.string())) {
            spdlog::info("[telemetry] Successfully initialized fallback SQLite store at {}", fallback_path.string());
            add_store(std::move(fallback_store));
        } else {
            spdlog::warn("[telemetry] Failed to initialize fallback SQLite store at {}", fallback_path.string());
        }
    }
#else
    spdlog::info("[telemetry] Telemetry support is disabled in this build.");
    add_store(std::make_shared<NullTelemetryStore>());
#endif
}

std::filesystem::path TelemetryManager::resolve_sqlite_telemetry_path() {
    if (const char* env = std::getenv("CHRONON3D_TELEMETRY_PATH")) {
        std::filesystem::path env_base(env);
        if (env_base.extension() == ".db" || env_base.extension() == ".sqlite") {
            return env_base;
        }
        return env_base / "chronon3d_render_history.sqlite";
    }
    return std::filesystem::path(get_telemetry_directory()) / "chronon3d_render_history.sqlite";
}

bool TelemetryManager::record_run(RenderTelemetryRecord& run,
                                  const std::vector<FrameTelemetryRecord>& frames,
                                  const std::vector<PhaseTelemetryRecord>& phases,
                                  const std::vector<CounterTelemetryRecord>& counters,
                                  const std::vector<NodeTelemetryRecord>& node_events,
                                  const std::vector<LayerTelemetryRecord>& layer_events,
                                  const std::vector<CacheTelemetryRecord>& cache_events,
                                  const std::vector<CullingTelemetryRecord>& culling_events,
                                  const std::vector<TextTelemetryRecord>& text_events,
                                  const std::vector<ImageTelemetryRecord>& image_events,
                                  const std::vector<TileTelemetryRecord>& tile_events,
                                  const std::vector<RenderArtifactRecord>& artifacts) {
    spdlog::info("[telemetry] record_run called with {} stores registered", m_stores.size());
    // Inject automatically gathered host attributes
    if (run.run_id.empty()) {
        run.run_id = generate_uuid();
    }
    if (run.os.empty()) {
        run.os = get_os_name();
    }
    if (run.cpu_model.empty()) {
        run.cpu_model = get_cpu_model();
    }
    if (run.cores == 0) {
        run.cores = get_logical_cores();
    }
    if (run.compiler_info.empty()) {
        run.compiler_info = get_compiler_info();
    }
    if (run.build_type.empty()) {
        run.build_type = get_build_type();
    }
    if (run.git_commit_short.empty()) {
        run.git_commit_short = get_git_commit();
    }
    if (run.finished_at_iso.empty()) {
        run.finished_at_iso = get_current_iso_time();
    }

    bool all_ok = true;
    for (auto& store : m_stores) {
        store->begin_transaction();
        bool ok = store->write_render_run(run);
        spdlog::info("[telemetry] write_render_run returned: {}", ok);
        if (!frames.empty()) {
            bool f_ok = store->write_frames(run.run_id, frames);
            spdlog::info("[telemetry] write_frames returned: {}", f_ok);
            ok &= f_ok;
        }
        if (!phases.empty()) {
            bool p_ok = store->write_phases(run.run_id, phases);
            spdlog::info("[telemetry] write_phases returned: {}", p_ok);
            ok &= p_ok;
        }
        if (!counters.empty()) {
            bool c_ok = store->write_counters(run.run_id, counters);
            spdlog::info("[telemetry] write_counters returned: {}", c_ok);
            ok &= c_ok;
        }
        if (!node_events.empty()) {
            bool r = store->write_node_events(run.run_id, node_events);
            spdlog::info("[telemetry] write_node_events returned: {}", r);
            ok &= r;
        }
        if (!layer_events.empty()) {
            bool r = store->write_layer_events(run.run_id, layer_events);
            spdlog::info("[telemetry] write_layer_events returned: {}", r);
            ok &= r;
        }
        if (!cache_events.empty()) {
            bool r = store->write_cache_events(run.run_id, cache_events);
            spdlog::info("[telemetry] write_cache_events returned: {}", r);
            ok &= r;
        }
        if (!culling_events.empty()) {
            bool r = store->write_culling_events(run.run_id, culling_events);
            spdlog::info("[telemetry] write_culling_events returned: {}", r);
            ok &= r;
        }
        if (!text_events.empty()) {
            bool r = store->write_text_events(run.run_id, text_events);
            spdlog::info("[telemetry] write_text_events returned: {}", r);
            ok &= r;
        }
        if (!image_events.empty()) {
            bool r = store->write_image_events(run.run_id, image_events);
            spdlog::info("[telemetry] write_image_events returned: {}", r);
            ok &= r;
        }
        if (!tile_events.empty()) {
            bool r = store->write_tile_events(run.run_id, tile_events);
            spdlog::info("[telemetry] write_tile_events returned: {}", r);
            ok &= r;
        }
        if (!artifacts.empty()) {
            bool r = store->write_artifacts(run.run_id, artifacts);
            spdlog::info("[telemetry] write_artifacts returned: {}", r);
            ok &= r;
        }
        store->end_transaction(ok);
        spdlog::info("[telemetry] store transaction end with status: {}", ok);
        all_ok &= ok;
    }
    return all_ok;
}

std::string TelemetryManager::get_os_name() {
#if defined(__linux__)
    return "Linux";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Unknown OS";
#endif
}

std::string TelemetryManager::get_cpu_model() {
    return "Generic CPU";
}

int TelemetryManager::get_logical_cores() {
    unsigned int n = std::thread::hardware_concurrency();
    return n > 0 ? static_cast<int>(n) : 1;
}

std::string TelemetryManager::get_compiler_info() {
#if defined(__clang__)
    return "Clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(__GNUC__)
    return "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
    return "Unknown Compiler";
#endif
}

std::string TelemetryManager::get_build_type() {
#if defined(NDEBUG)
    return "Release";
#else
    return "Debug";
#endif
}

std::string TelemetryManager::get_git_commit() {
#ifdef CHRONON3D_GIT_COMMIT
    return CHRONON3D_GIT_COMMIT;
#else
    return "unknown";
#endif
}

std::string TelemetryManager::get_current_iso_time() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    gmtime_r(&now_time, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string TelemetryManager::generate_uuid() {
    if (const char* env = std::getenv("CHRONON3D_RUN_ID")) {
        return env;
    }
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);

    std::ostringstream oss;
    oss << "run_" 
        << std::hex << std::setw(8) << std::setfill('0') << dis(gen)
        << "_" 
        << std::hex << std::setw(8) << std::setfill('0') << dis(gen);
    return oss.str();
}

// TICKET-122 — get_peak_memory_usage now serves the cached monotonic
// peak maintained by peak_memory_cache() (separate ~1Hz worker in this
// TU).  Callers (record_run) no longer block on a /proc/self/status
// re-scan on the record path.  Acquire-load pairs with the worker's
// release-CAS so the value observed is coherent with respect to the
// producer's release point.
uint64_t TelemetryManager::get_peak_memory_usage() {
    return peak_memory_cache().max_vmhwm_bytes.load(std::memory_order_acquire);
}

} // namespace chronon3d::telemetry
