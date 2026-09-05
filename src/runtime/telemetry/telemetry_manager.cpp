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
#include <sstream>
#include <iomanip>
#include <fstream>
#include <utility>
#include <sys/resource.h>

namespace chronon3d::telemetry {

namespace {

std::filesystem::path telemetry_directory(const TelemetryRuntimeConfig& config) {
    if (!config.path_override.empty()) {
        const auto ext = config.path_override.extension();
        if (ext == ".db" || ext == ".sqlite") {
            const auto parent = config.path_override.parent_path();
            return parent.empty() ? std::filesystem::path{"."} : parent;
        }
        return config.path_override;
    }
    if (!config.default_directory.empty()) return config.default_directory;
    return "/tmp/.chronon3d/telemetry";
}

struct PeakMemoryCache {
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> max_vmhwm_bytes{0};
    std::thread worker;

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
        struct rusage usage {};
        if (getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss > 0) {
            return static_cast<uint64_t>(usage.ru_maxrss) * 1024ULL;
        }
        return 0;
    }

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
        prime_once_internal();
        worker = std::thread([this]() {
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

void TelemetryManager::configure(TelemetryRuntimeConfig config) {
    m_config = std::move(config);
}

void TelemetryManager::add_store(std::shared_ptr<TelemetryStore> store) {
    if (store) m_stores.push_back(std::move(store));
}

void TelemetryManager::clear_stores() {
    m_stores.clear();
}

void TelemetryManager::initialize_default_stores() {
    clear_stores();

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
    const std::filesystem::path base_dir = telemetry_directory(m_config);
    spdlog::info("[telemetry] Initializing default stores in base directory: {}",
                 base_dir.string());

    std::error_code ec;
    std::filesystem::create_directories(base_dir, ec);

    const std::filesystem::path sqlite_path = resolve_sqlite_telemetry_path(m_config);
    spdlog::info("[telemetry] Resolving SQLite path to: {}", sqlite_path.string());

    auto sqlite_store = std::make_shared<SqliteTelemetryStore>();
    if (sqlite_store->initialize(sqlite_path.string())) {
        spdlog::info("[telemetry] Successfully initialized SQLite store at {}",
                     sqlite_path.string());
        add_store(std::move(sqlite_store));
    } else {
        spdlog::warn(
            "[telemetry] Failed to initialize workspace SQLite store at {}; "
            "falling back to default telemetry DB", sqlite_path.string());
        TelemetryRuntimeConfig fallback_config;
        fallback_config.default_directory = m_config.default_directory;
        const std::filesystem::path fallback_path =
            telemetry_directory(fallback_config) / "chronon3d_render_history.sqlite";
        auto fallback_store = std::make_shared<SqliteTelemetryStore>();
        if (fallback_store->initialize(fallback_path.string())) {
            spdlog::info("[telemetry] Successfully initialized fallback SQLite store at {}",
                         fallback_path.string());
            add_store(std::move(fallback_store));
        } else {
            spdlog::warn("[telemetry] Failed to initialize fallback SQLite store at {}",
                         fallback_path.string());
        }
    }
#else
    spdlog::info("[telemetry] Telemetry support is disabled in this build.");
    add_store(std::make_shared<NullTelemetryStore>());
#endif
}

std::filesystem::path TelemetryManager::resolve_sqlite_telemetry_path(
    const TelemetryRuntimeConfig& config) {
    if (!config.path_override.empty()) {
        const auto ext = config.path_override.extension();
        if (ext == ".db" || ext == ".sqlite") return config.path_override;
        return config.path_override / "chronon3d_render_history.sqlite";
    }
    return telemetry_directory(config) / "chronon3d_render_history.sqlite";
}

std::filesystem::path TelemetryManager::resolve_sqlite_telemetry_path() {
    return resolve_sqlite_telemetry_path(instance().m_config);
}

bool TelemetryManager::record_run(const TelemetryRunSnapshot& snapshot) {
    // The snapshot is the immutable photograph; the manager may still fill
    // run metadata defaults that only it owns (run_id, host attribs). Copy
    // locally so the const input is respected while the persisted run can be
    // default-filled. This preserves the value-object invariant at the call
    // site and keeps exactly one authoritative run_id assignment point.
    TelemetryRunSnapshot mutable_snapshot = snapshot;
    RenderTelemetryRecord& run = mutable_snapshot.run;
    const std::vector<FrameTelemetry>& frames = mutable_snapshot.frames;
    const std::vector<PhaseTelemetryRecord>& phases = mutable_snapshot.phases;
    const std::vector<CounterTelemetryRecord>& counters = mutable_snapshot.counters;
    const std::vector<NodeTelemetryRecord>& node_events = mutable_snapshot.node_events;
    const std::vector<LayerTelemetryRecord>& layer_events = mutable_snapshot.layer_events;
    const std::vector<CacheTelemetryRecord>& cache_events = mutable_snapshot.cache_events;
    const std::vector<CullingTelemetryRecord>& culling_events = mutable_snapshot.culling_events;
    const std::vector<ImageTelemetryRecord>& image_events = mutable_snapshot.image_events;
    const std::vector<RenderArtifactRecord>& artifacts = mutable_snapshot.artifacts;

    spdlog::info("[telemetry] record_run called with {} stores registered", m_stores.size());
    if (run.run_id.empty()) {
        run.run_id = m_config.run_id_override.empty()
            ? generate_uuid()
            : m_config.run_id_override;
    }
    if (run.os.empty()) run.os = get_os_name();
    if (run.cpu_model.empty()) run.cpu_model = get_cpu_model();
    if (run.cores == 0) run.cores = get_logical_cores();
    if (run.compiler_info.empty()) run.compiler_info = get_compiler_info();
    if (run.build_type.empty()) run.build_type = get_build_type();
    if (run.git_commit_short.empty()) run.git_commit_short = get_git_commit();
    if (run.finished_at_iso.empty()) run.finished_at_iso = get_current_iso_time();

    bool all_ok = true;
    for (auto& store : m_stores) {
        store->begin_transaction();
        bool ok = store->write_render_run(run);
        spdlog::info("[telemetry] write_render_run returned: {}", ok);
        if (!frames.empty()) {
            const bool r = store->write_frames(run.run_id, frames);
            spdlog::info("[telemetry] write_frames returned: {}", r);
            ok &= r;
        }
        if (!phases.empty()) {
            const bool r = store->write_phases(run.run_id, phases);
            spdlog::info("[telemetry] write_phases returned: {}", r);
            ok &= r;
        }
        if (!counters.empty()) {
            const bool r = store->write_counters(run.run_id, counters);
            spdlog::info("[telemetry] write_counters returned: {}", r);
            ok &= r;
        }
        if (!node_events.empty()) {
            const bool r = store->write_node_events(run.run_id, node_events);
            spdlog::info("[telemetry] write_node_events returned: {}", r);
            ok &= r;
        }
        if (!layer_events.empty()) {
            const bool r = store->write_layer_events(run.run_id, layer_events);
            spdlog::info("[telemetry] write_layer_events returned: {}", r);
            ok &= r;
        }
        if (!cache_events.empty()) {
            const bool r = store->write_cache_events(run.run_id, cache_events);
            spdlog::info("[telemetry] write_cache_events returned: {}", r);
            ok &= r;
        }
        if (!culling_events.empty()) {
            const bool r = store->write_culling_events(run.run_id, culling_events);
            spdlog::info("[telemetry] write_culling_events returned: {}", r);
            ok &= r;
        }
        if (!image_events.empty()) {
            const bool r = store->write_image_events(run.run_id, image_events);
            spdlog::info("[telemetry] write_image_events returned: {}", r);
            ok &= r;
        }
        if (!artifacts.empty()) {
            const bool r = store->write_artifacts(run.run_id, artifacts);
            spdlog::info("[telemetry] write_artifacts returned: {}", r);
            ok &= r;
        }
        // Stage 3 — memory persistence projections (same transaction).
        if (!mutable_snapshot.node_summaries.empty()) {
            const bool r = store->write_node_summaries(run.run_id, mutable_snapshot.node_summaries);
            ok &= r;
        }
        ok &= store->write_memory_summary(run.run_id, mutable_snapshot.memory_summary);
        store->end_transaction(ok);
        spdlog::info("[telemetry] store transaction end with status: {}", ok);
        all_ok &= ok;
    }
    return all_ok;
}

bool TelemetryManager::record_run(RenderTelemetryRecord& run,
                                  const std::vector<FrameTelemetry>& frames,
                                  const std::vector<PhaseTelemetryRecord>& phases,
                                  const std::vector<CounterTelemetryRecord>& counters,
                                  const std::vector<NodeTelemetryRecord>& node_events,
                                  const std::vector<LayerTelemetryRecord>& layer_events,
                                  const std::vector<CacheTelemetryRecord>& cache_events,
                                  const std::vector<CullingTelemetryRecord>& culling_events,
                                  const std::vector<ImageTelemetryRecord>& image_events,
                                  const std::vector<RenderArtifactRecord>& artifacts) {
    TelemetryRunSnapshot snapshot;
    snapshot.run = std::move(run);
    snapshot.frames = frames;
    snapshot.phases = phases;
    snapshot.counters = counters;
    snapshot.node_events = node_events;
    snapshot.layer_events = layer_events;
    snapshot.cache_events = cache_events;
    snapshot.culling_events = culling_events;
    snapshot.image_events = image_events;
    snapshot.artifacts = artifacts;
    const bool ok = record_run(snapshot);
    run = snapshot.run;
    return ok;
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
    const unsigned int n = std::thread::hardware_concurrency();
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
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    gmtime_r(&now_time, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string TelemetryManager::generate_uuid() {
    // A run identifier is an observability token, not a visual random source.
    // Environment overrides are resolved at the process boundary and stored in
    // m_config; this generator is deliberately pure with respect to process env.
    static std::atomic<uint64_t> sequence{0};
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto ticks = static_cast<uint64_t>(now.count());
    const auto serial = sequence.fetch_add(1, std::memory_order_relaxed);

    std::ostringstream oss;
    oss << "run_"
        << std::hex << std::setw(16) << std::setfill('0') << ticks
        << "_"
        << std::hex << std::setw(16) << std::setfill('0') << serial;
    return oss.str();
}

uint64_t TelemetryManager::get_peak_memory_usage() {
    return peak_memory_cache().max_vmhwm_bytes.load(std::memory_order_acquire);
}

} // namespace chronon3d::telemetry
