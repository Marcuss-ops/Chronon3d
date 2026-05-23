#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#include <chronon3d/runtime/telemetry/jsonl_telemetry_store.hpp>
#include <chronon3d/runtime/telemetry/sqlite_telemetry_store.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#if defined(__linux__)
#include <fstream>
#include <sys/resource.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace chronon3d::telemetry {

namespace {

std::string get_telemetry_directory() {
    if (const char* env = std::getenv("CHRONON3D_TELEMETRY_PATH")) {
        return env;
    }
    std::filesystem::path home_path;
#if defined(_WIN32)
    if (const char* userprofile = std::getenv("USERPROFILE")) {
        home_path = userprofile;
    } else {
        home_path = "C:/";
    }
#else
    if (const char* home = std::getenv("HOME")) {
        home_path = home;
    } else {
        home_path = "/tmp";
    }
#endif
    return (home_path / ".chronon3d" / "telemetry").string();
}

std::filesystem::path find_workspace_root() {
    std::filesystem::path current = std::filesystem::current_path();
    while (!current.empty()) {
        if (std::filesystem::exists(current / "CMakeLists.txt")) {
            return current;
        }
        auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return {};
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

    std::string base_dir = get_telemetry_directory();
    
    // Ensure base directory exists
    std::error_code ec;
    std::filesystem::create_directories(base_dir, ec);

    // 1. JSONL Store (Always enabled)
    auto jsonl_path = (std::filesystem::path(base_dir) / "render_history.jsonl").string();
    auto jsonl_store = std::make_shared<JsonlTelemetryStore>();
    if (jsonl_store->initialize(jsonl_path)) {
        add_store(std::move(jsonl_store));
    }

    // 2. SQLite Store (Uses fallback stub internally if disabled in compile options)
    // Preference: local output/telemetry.db if we are in a workspace
    const std::filesystem::path sqlite_path = resolve_sqlite_telemetry_path();
    
    auto sqlite_store = std::make_shared<SqliteTelemetryStore>();
    if (sqlite_store->initialize(sqlite_path.string())) {
        add_store(std::move(sqlite_store));
    } else {
        spdlog::warn("[telemetry] Failed to initialize workspace SQLite store at {}; falling back to user telemetry DB",
                     sqlite_path.string());
        const std::filesystem::path fallback_path =
            std::filesystem::path(get_telemetry_directory()) / "chronon3d_render_history.sqlite";
        auto fallback_store = std::make_shared<SqliteTelemetryStore>();
        if (fallback_store->initialize(fallback_path.string())) {
            add_store(std::move(fallback_store));
        } else {
            spdlog::warn("[telemetry] Failed to initialize fallback SQLite store at {}", fallback_path.string());
        }
    }
}

std::filesystem::path TelemetryManager::resolve_sqlite_telemetry_path() {
    if (const char* env = std::getenv("CHRONON3D_TELEMETRY_PATH")) {
        std::filesystem::path env_base(env);
        if (env_base.extension() == ".db" || env_base.extension() == ".sqlite") {
            return env_base;
        }
        return env_base / "chronon3d_render_history.sqlite";
    }

    if (const auto workspace_root = find_workspace_root(); !workspace_root.empty()) {
        return workspace_root / "output" / "telemetry.db";
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
                                  const std::vector<TileTelemetryRecord>& tile_events) {
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
        if (!frames.empty()) {
            ok &= store->write_frames(run.run_id, frames);
        }
        if (!phases.empty()) {
            ok &= store->write_phases(run.run_id, phases);
        }
        if (!counters.empty()) {
            ok &= store->write_counters(run.run_id, counters);
        }
        if (!node_events.empty()) {
            ok &= store->write_node_events(run.run_id, node_events);
        }
        if (!layer_events.empty()) {
            ok &= store->write_layer_events(run.run_id, layer_events);
        }
        if (!cache_events.empty()) {
            ok &= store->write_cache_events(run.run_id, cache_events);
        }
        if (!culling_events.empty()) {
            ok &= store->write_culling_events(run.run_id, culling_events);
        }
        if (!text_events.empty()) {
            ok &= store->write_text_events(run.run_id, text_events);
        }
        if (!image_events.empty()) {
            ok &= store->write_image_events(run.run_id, image_events);
        }
        if (!tile_events.empty()) {
            ok &= store->write_tile_events(run.run_id, tile_events);
        }
        store->end_transaction(ok);
        all_ok &= ok;
    }
    return all_ok;
}

std::string TelemetryManager::get_os_name() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Unknown OS";
#endif
}

std::string TelemetryManager::get_cpu_model() {
#if defined(_WIN32)
    if (const char* env = std::getenv("PROCESSOR_IDENTIFIER")) {
        return env;
    }
    return "x86_64 MSVC Generic";
#else
    return "Generic CPU";
#endif
}

int TelemetryManager::get_logical_cores() {
    unsigned int n = std::thread::hardware_concurrency();
    return n > 0 ? static_cast<int>(n) : 1;
}

std::string TelemetryManager::get_compiler_info() {
#if defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__clang__)
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
#if defined(_WIN32)
    gmtime_s(&tm_buf, &now_time);
#else
    gmtime_r(&now_time, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string TelemetryManager::generate_uuid() {
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

uint64_t TelemetryManager::get_peak_memory_usage() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<uint64_t>(pmc.PeakPagefileUsage);
    }
    return 0;
#elif defined(__linux__)
    {
        std::ifstream status("/proc/self/status");
        std::string line;
        while (std::getline(status, line)) {
            if (line.rfind("VmHWM:", 0) == 0) {
                std::istringstream iss(line.substr(6));
                uint64_t kb = 0;
                iss >> kb;
                if (kb > 0) {
                    return kb * 1024ULL;
                }
                break;
            }
        }
    }

    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss > 0) {
        return static_cast<uint64_t>(usage.ru_maxrss) * 1024ULL;
    }
    return 0;
#else
    return 0;
#endif
}

} // namespace chronon3d::telemetry
