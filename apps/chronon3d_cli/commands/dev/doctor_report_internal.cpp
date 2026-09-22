// ═══════════════════════════════════════════════════════════════════════════
// doctor_report_internal.cpp — the `chronon doctor` engine.
//
// TICKET-HOTSPOT-DECOMPOSITION A3: this TU owns what used to be textually
// included into doctor_report.cpp as three `*.inc` files
// (doctor_report_helpers.inc + doctor_report_checks.inc + the engine half of
// doctor_report_api.inc).
//
//   doctor_report_helpers.inc  opens an anonymous namespace: constants, the
//                              check accumulator and the host probes. It is
//                              closed by the last line of checks.inc, so the
//                              two includes are one internal region and must
//                              stay adjacent and in this order.
//   doctor_report_checks.inc   the seven collect_* passes over that region.
//
// The public surface is `run_doctor` (declared in doctor_report.hpp) plus the
// two status-name switches shared with doctor_report.cpp through
// doctor_report_internal.hpp. Everything else stays internal to this TU.
// ═══════════════════════════════════════════════════════════════════════════

#include "doctor_report.hpp"
#include "doctor_report_internal.hpp"

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_graph/backend_registry.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#ifdef CHRONON3D_HAS_C_API
#include <chronon/abi.h>
#endif

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace chronon3d::cli {

// ── the one dependency shared with doctor_report.cpp ───────────────────────
namespace doctor_detail {

const char* doctor_status_name_impl(DoctorStatus status) noexcept {
    switch (status) {
        case DoctorStatus::Pass: return "pass";
        case DoctorStatus::Warn: return "warn";
        case DoctorStatus::Fail: return "fail";
        case DoctorStatus::Skip: return "skip";
    }
    return "skip";
}

const char* doctor_status_label_impl(DoctorStatus status) noexcept {
    switch (status) {
        case DoctorStatus::Pass: return "PASS";
        case DoctorStatus::Warn: return "WARN";
        case DoctorStatus::Fail: return "FAIL";
        case DoctorStatus::Skip: return "SKIP";
    }
    return "SKIP";
}

} // namespace doctor_detail

// ── the internal region: helpers.inc opens the anonymous namespace, checks.inc
//    closes it. Kept adjacent on purpose.
namespace {

constexpr const char* kRenderPlanSchema = "chronon.render-plan.v2";
constexpr const char* kFontRegular = "assets/fonts/Inter-Regular.ttf";
constexpr const char* kFontBold = "assets/fonts/Inter-Bold.ttf";

// The status-name switches live in doctor_report_internal.cpp under
// doctor_detail (declared in doctor_report_internal.hpp): they are the one
// cross-TU dependency of the engine, so they are not re-declared here.

void add_check(DoctorReport& report, std::string id, DoctorStatus status,
               std::string message) {
    report.checks.push_back(DoctorCheck{std::move(id), status, std::move(message)});
}

class LogSilencer {
public:
    LogSilencer() : previous_(spdlog::get_level()) {
        spdlog::set_level(spdlog::level::off);
    }
    ~LogSilencer() { spdlog::set_level(previous_); }
    LogSilencer(const LogSilencer&) = delete;
    LogSilencer& operator=(const LogSilencer&) = delete;

private:
    spdlog::level::level_enum previous_;
};

bool command_succeeds(const char* command) {
    return std::system(command) == 0;
}

bool encoder_available(const char* name) {
    const std::string command =
        "ffmpeg -hide_banner -h encoder=" + std::string(name) +
        " > /dev/null 2>&1";
    return command_succeeds(command.c_str());
}

std::string host_arch() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#else
    return "unknown";
#endif
}

std::string read_cpu_model_name() {
    std::ifstream in("/proc/cpuinfo");
    if (!in.is_open()) return "unknown";
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = line.substr(0, colon);
        const std::size_t key_end = key.find_last_not_of(" \t");
        if (key_end == std::string::npos) continue;
        if (key.substr(0, key_end + 1) != "model name" &&
            key.substr(0, key_end + 1) != "Model name") {
            continue;
        }
        std::string value = line.substr(colon + 1);
        const std::size_t first = value.find_first_not_of(" \t");
        return first == std::string::npos ? "unknown" : value.substr(first);
    }
    return "unknown";
}

std::uint64_t host_ram_bytes() noexcept {
#if defined(__linux__)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page_size);
    }
#endif
    return 0;
}

std::string human_bytes(std::uint64_t bytes) {
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    std::ostringstream out;
    out.precision(1);
    out << std::fixed << static_cast<double>(bytes) / kGiB << " GiB";
    return out.str();
}

bool directory_writable(const std::filesystem::path& dir) {
    std::error_code ec;
    const auto probe = dir / ".chronon-doctor-write-probe";
    {
        std::ofstream out(probe, std::ios::binary);
        if (!out) return false;
        out << "probe";
    }
    std::filesystem::remove(probe, ec);
    return true;
}
void collect_engine_checks(DoctorReport& report) {
#ifdef CHRONON3D_HAS_C_API
    const char* version = chronon_version_string();
    add_check(report, "engine.version",
              version && *version ? DoctorStatus::Pass : DoctorStatus::Warn,
              version && *version ? version : "version string unavailable");

    const std::uint32_t abi = chronon_abi_version();
    add_check(report, "engine.abi",
              abi == 2 ? DoctorStatus::Pass : DoctorStatus::Fail,
              "ABI " + std::to_string(abi));
#else
    add_check(report, "engine.version", DoctorStatus::Skip,
              "C ABI not built (CHRONON3D_BUILD_C_API=OFF)");
    add_check(report, "engine.abi", DoctorStatus::Skip,
              "C ABI not built (CHRONON3D_BUILD_C_API=OFF)");
#endif

    add_check(report, "engine.render_plan_schema", DoctorStatus::Pass,
              kRenderPlanSchema);

    const std::string git_sha = telemetry::TelemetryManager::get_git_commit();
    add_check(report, "engine.git_sha",
              git_sha.empty() || git_sha == "unknown" ? DoctorStatus::Warn
                                                       : DoctorStatus::Pass,
              git_sha);
}

void collect_system_checks(DoctorReport& report) {
    add_check(report, "system.os", DoctorStatus::Pass,
              telemetry::TelemetryManager::get_os_name());
    add_check(report, "system.arch", DoctorStatus::Pass, host_arch());
    add_check(report, "system.cpu_model", DoctorStatus::Pass,
              read_cpu_model_name());

    const auto threads = std::thread::hardware_concurrency();
    add_check(report, "system.logical_threads",
              threads > 0 ? DoctorStatus::Pass : DoctorStatus::Warn,
              threads > 0 ? std::to_string(threads) : "unknown");

    const std::uint64_t ram = host_ram_bytes();
    if (ram > 0) {
        add_check(report, "system.ram_bytes", DoctorStatus::Pass,
                  human_bytes(ram));
    } else {
        add_check(report, "system.ram_bytes", DoctorStatus::Skip,
                  "physical memory not queryable on this platform");
    }
}

void collect_backend_checks(DoctorReport& report) {
    graph::BackendRegistry registry;
    registry.register_backend(
        graph::BackendType::Software,
        graph::BackendCapabilities{.graphics = true,
                                   .max_texture_width = 16384,
                                   .max_texture_height = 16384},
        []() -> std::unique_ptr<graph::RenderBackend> { return nullptr; });

#ifdef CHRONON3D_ENABLE_VULKAN
    registry.register_backend(
        graph::BackendType::Vulkan,
        graph::BackendCapabilities{.graphics = true, .compute = true},
        []() -> std::unique_ptr<graph::RenderBackend> { return nullptr; });
#endif

    add_check(report, "backend.software",
              registry.contains(graph::BackendType::Software)
                  ? DoctorStatus::Pass : DoctorStatus::Fail,
              registry.contains(graph::BackendType::Software)
                  ? "software backend registered"
                  : "software backend not registered");

    if (registry.contains(graph::BackendType::Vulkan)) {
        add_check(report, "backend.vulkan", DoctorStatus::Pass,
                  "Vulkan backend registered");
    } else {
        add_check(report, "backend.vulkan", DoctorStatus::Skip,
                  "Vulkan backend not built (CHRONON3D_ENABLE_VULKAN=OFF)");
    }
}

void collect_encoder_checks(DoctorReport& report) {
    const bool ffmpeg_ok =
        command_succeeds("ffmpeg -version > /dev/null 2>&1");
    add_check(report, "encoder.ffmpeg",
              ffmpeg_ok ? DoctorStatus::Pass : DoctorStatus::Fail,
              ffmpeg_ok ? "ffmpeg executable found on PATH"
                        : "ffmpeg not found on PATH");

    if (!ffmpeg_ok) {
        add_check(report, "encoder.h264", DoctorStatus::Skip,
                  "skipped: ffmpeg not found");
        add_check(report, "encoder.h265", DoctorStatus::Skip,
                  "skipped: ffmpeg not found");
        add_check(report, "encoder.av1", DoctorStatus::Skip,
                  "skipped: ffmpeg not found");
        add_check(report, "encoder.nvenc", DoctorStatus::Skip,
                  "skipped: ffmpeg not found");
        return;
    }

    const bool h264_ok = encoder_available("libx264");
    add_check(report, "encoder.h264",
              h264_ok ? DoctorStatus::Pass : DoctorStatus::Fail,
              h264_ok ? "libx264 software encoder available"
                      : "libx264 encoder unavailable");

    const bool h265_ok = encoder_available("libx265");
    add_check(report, "encoder.h265",
              h265_ok ? DoctorStatus::Pass : DoctorStatus::Skip,
              h265_ok ? "libx265 software encoder available"
                      : "libx265 encoder unavailable");

    const bool av1_ok = encoder_available("libaom-av1");
    add_check(report, "encoder.av1",
              av1_ok ? DoctorStatus::Pass : DoctorStatus::Skip,
              av1_ok ? "libaom-av1 software encoder available"
                     : "libaom-av1 encoder unavailable");

    const bool nvenc_ok = encoder_available("h264_nvenc");
    add_check(report, "encoder.nvenc",
              nvenc_ok ? DoctorStatus::Pass : DoctorStatus::Skip,
              nvenc_ok ? "h264_nvenc hardware encoder available"
                       : "h264_nvenc encoder unavailable (no NVIDIA/NVENC)");
}

void collect_asset_checks(DoctorReport& report, const DoctorOptions& options) {
    std::string root = options.assets_root;
    if (root.empty()) {
        if (const char* env = std::getenv("CHRONON3D_CLI_ASSETS_ROOT")) {
            root = env;
        }
    }
    if (root.empty()) {
        add_check(report, "assets.font_regular", DoctorStatus::Skip,
                  "assets root not provided (use --assets-root)");
        add_check(report, "assets.font_bold", DoctorStatus::Skip,
                  "assets root not provided (use --assets-root)");
        return;
    }

    assets::AssetResolver resolver;
    try {
        resolver.mount(std::filesystem::path{root});
    } catch (const std::exception& error) {
        add_check(report, "assets.font_regular", DoctorStatus::Fail,
                  std::string{"cannot mount assets root: "} + error.what());
        add_check(report, "assets.font_bold", DoctorStatus::Skip,
                  "skipped: assets root mount failed");
        return;
    }

    const auto regular = resolver.resolve(kFontRegular);
    add_check(report, "assets.font_regular",
              regular ? DoctorStatus::Pass : DoctorStatus::Fail,
              regular ? "resolved " + std::string(kFontRegular)
                      : "missing " + std::string(kFontRegular));

    const auto bold = resolver.resolve(kFontBold);
    add_check(report, "assets.font_bold",
              bold ? DoctorStatus::Pass : DoctorStatus::Fail,
              bold ? "resolved " + std::string(kFontBold)
                   : "missing " + std::string(kFontBold));
}

void collect_filesystem_checks(DoctorReport& report) {
    std::error_code ec;
    const auto temp = std::filesystem::temp_directory_path(ec);
    if (ec) {
        add_check(report, "filesystem.temp_writable", DoctorStatus::Fail,
                  "cannot resolve temp directory: " + ec.message());
    } else {
        add_check(report, "filesystem.temp_writable",
                  directory_writable(temp) ? DoctorStatus::Pass
                                           : DoctorStatus::Fail,
                  directory_writable(temp) ? "temp directory writable"
                                           : "temp directory not writable");
    }

    const auto cwd = std::filesystem::current_path(ec);
    if (ec) {
        add_check(report, "filesystem.output_writable", DoctorStatus::Fail,
                  "cannot resolve current directory: " + ec.message());
    } else {
        add_check(report, "filesystem.output_writable",
                  directory_writable(cwd) ? DoctorStatus::Pass
                                          : DoctorStatus::Fail,
                  directory_writable(cwd) ? "output directory writable"
                                          : "output directory not writable");
    }

    const auto space = std::filesystem::space(
        ec ? std::filesystem::path{"."} : cwd, ec);
    if (ec) {
        add_check(report, "filesystem.free_disk_bytes", DoctorStatus::Fail,
                  "cannot query free disk: " + ec.message());
    } else {
        add_check(report, "filesystem.free_disk_bytes",
                  space.available > 0 ? DoctorStatus::Pass : DoctorStatus::Fail,
                  human_bytes(space.available) + " available");
    }
}

void collect_deep_check(DoctorReport& report, const DoctorOptions& options) {
    if (!options.deep) {
        add_check(report, "deep.render_smoke", DoctorStatus::Skip,
                  "skipped: pass --deep to run the render smoke test");
        return;
    }

#ifdef CHRONON3D_HAS_C_API
    static constexpr const char* kSmokePlan = R"JSON({
  "schema": "chronon.render-plan.v2",
  "version": 2,
  "canvas": {"width": 320, "height": 180, "fps_num": 30, "fps_den": 1, "duration_frames": 1},
  "layers": [{"id": "bg", "type": "color", "color": [0.1, 0.2, 0.3, 1.0]}],
  "output": {"path": "doctor_smoke.png"}
})JSON";

    LogSilencer silence_logs;

    chronon_engine_config config{};
    config.struct_size = sizeof(config);
    config.abi_version = chronon_abi_version();
    config.assets_root = nullptr;
    config.flags = 0;

    chronon_error_info error{};
    error.struct_size = sizeof(error);

    chronon_engine* engine = nullptr;
    chronon_status status = chronon_engine_create_v2(&config, &engine, &error);
    if (status != CHRONON_OK || !engine) {
        add_check(report, "deep.render_smoke", DoctorStatus::Fail,
                  "engine create failed: " + std::string(chronon_status_name(status)));
        return;
    }

    chronon_plan* plan = nullptr;
    status = chronon_plan_compile_json(engine, kSmokePlan, &plan);
    if (status != CHRONON_OK || !plan) {
        add_check(report, "deep.render_smoke", DoctorStatus::Fail,
                  "plan compile failed: " + std::string(chronon_status_name(status)) +
                      " (" + std::string(chronon_engine_last_error(engine)) + ")");
        chronon_engine_destroy(engine);
        return;
    }

    chronon_frame_buffer frame{};
    status = chronon_render_frame(engine, plan, 0, &frame);
    if (status == CHRONON_OK) {
        add_check(report, "deep.render_smoke", DoctorStatus::Pass,
                  "compiled + rendered 1 frame (" + std::to_string(frame.width) +
                      "x" + std::to_string(frame.height) + ")");
        chronon_buffer_free(engine, &frame);
    } else {
        add_check(report, "deep.render_smoke", DoctorStatus::Fail,
                  "render frame failed: " + std::string(chronon_status_name(status)));
    }

    chronon_plan_destroy(plan);
    chronon_engine_destroy(engine);
#else
    add_check(report, "deep.render_smoke", DoctorStatus::Skip,
              "skipped: C ABI not built (CHRONON3D_BUILD_C_API=OFF)");
#endif
}

} // namespace

DoctorReport run_doctor(const DoctorOptions& options) {
    DoctorReport report;
    collect_engine_checks(report);
    collect_system_checks(report);
    collect_backend_checks(report);
    collect_encoder_checks(report);
    collect_asset_checks(report, options);
    collect_filesystem_checks(report);
    collect_deep_check(report, options);

    report.ready = true;
    for (const auto& check : report.checks) {
        if (check.status == DoctorStatus::Fail) {
            report.ready = false;
            break;
        }
    }
    return report;
}

} // namespace chronon3d::cli
