#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#include <chronon3d/runtime/telemetry/jsonl_telemetry_store.hpp>
#include <chronon3d/runtime/telemetry/sqlite_telemetry_store.hpp>
#include <filesystem>
#include <thread>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstdlib>

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

    // 1. JSONL Store (Always enabled)
    auto jsonl_path = (std::filesystem::path(base_dir) / "render_history.jsonl").string();
    auto jsonl_store = std::make_shared<JsonlTelemetryStore>();
    if (jsonl_store->initialize(jsonl_path)) {
        add_store(std::move(jsonl_store));
    }

    // 2. SQLite Store (Uses fallback stub internally if disabled in compile options)
    auto sqlite_path = (std::filesystem::path(base_dir) / "chronon3d_render_history.sqlite").string();
    auto sqlite_store = std::make_shared<SqliteTelemetryStore>();
    if (sqlite_store->initialize(sqlite_path)) {
        add_store(std::move(sqlite_store));
    }
}

bool TelemetryManager::record_run(RenderTelemetryRecord run,
                                  const std::vector<FrameTelemetryRecord>& frames,
                                  const std::vector<PhaseTelemetryRecord>& phases,
                                  const std::vector<CounterTelemetryRecord>& counters) {
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
    return "unknown"; // Default short commit hash placeholder
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
#else
    return 0;
#endif
}

} // namespace chronon3d::telemetry
