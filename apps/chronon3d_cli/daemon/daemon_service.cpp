#include "daemon_service.hpp"

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>

#include "utils/common/render_error_formatter.hpp"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace chronon3d::cli {

// ── Helpers ────────────────────────────────────────────────────────────────

namespace {

    std::vector<std::string> split_args(const std::string& line) {
        std::vector<std::string> args;
        std::istringstream iss(line);
        std::string token;
        while (iss >> token) args.push_back(token);
        return args;
    }

    std::string format_output_path(const std::string& pattern, i32 frame) {
        std::string result = pattern;

        // Replace "####" with zero-padded 4-digit frame number.
        auto pos = result.find("####");
        if (pos != std::string::npos) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%04d", frame);
            result.replace(pos, 4, buf);
        }

        // Replace "#" with raw frame number.
        pos = result.find('#');
        if (pos != std::string::npos) {
            result.replace(pos, 1, std::to_string(frame));
        }

        return result;
    }

} // anonymous namespace

// ── Construction ──────────────────────────────────────────────────────────────

DaemonService::DaemonService(const CompositionRegistry& registry,
                             DaemonOptions options)
    : m_registry(registry)
    , m_options(std::move(options))
{
    Config config = Config::from_environment();

    if (!m_options.assets_root.empty()) {
        m_engine = std::make_unique<RenderEngine>(
            std::move(config), m_options.assets_root);
    } else {
        m_engine = std::make_unique<RenderEngine>(std::move(config));
    }

    m_engine->set_composition_registry(&m_registry);

    spdlog::info("🔥 Engine initialised. FB pool warm, font engines loaded.");
    spdlog::info("   {} compositions registered.", m_registry.available().size());
}

DaemonService::~DaemonService() = default;

// ── Main Loop ─────────────────────────────────────────────────────────────────

void DaemonService::run() {
    spdlog::info("");
    spdlog::info("╔══════════════════════════════════════════╗");
    spdlog::info("║   🔥 Chronon3d Daemon Mode — Ready      ║");
    spdlog::info("╠══════════════════════════════════════════╣");
    spdlog::info("║  r  <comp> <frame> [out]     Render     ║");
    spdlog::info("║  st                          Stats      ║");
    spdlog::info("║  cc                          Clear cache║");
    spdlog::info("║  rl                          Rebuild    ║");
    spdlog::info("║  h                           Help       ║");
    spdlog::info("║  q                           Quit       ║");
    spdlog::info("╚══════════════════════════════════════════╝");
    spdlog::info("");

    std::string line;
    while (m_running && std::getline(std::cin, line)) {
        // Trim leading / trailing whitespace.
        while (!line.empty() && std::isspace(line.front()))
            line.erase(0, 1);
        while (!line.empty() && std::isspace(line.back()))
            line.pop_back();

        if (line.empty()) continue;
        handle_command(line);
    }

    spdlog::info("");
    spdlog::info("Daemon shutting down. {} frames rendered in {:.1f}ms total.",
                 m_render_count, m_total_render_ms);
}

// ── Command Dispatch ──────────────────────────────────────────────────────────

void DaemonService::handle_command(const std::string& line) {
    auto args = split_args(line);
    if (args.empty()) return;

    const auto& cmd = args[0];

    if (cmd == "render" || cmd == "r") {
        args.erase(args.begin());
        cmd_render(args);
    } else if (cmd == "reload" || cmd == "rl") {
        cmd_reload();
    } else if (cmd == "clear" || cmd == "cc") {
        cmd_clear_caches();
    } else if (cmd == "status" || cmd == "st") {
        cmd_status();
    } else if (cmd == "help" || cmd == "h") {
        cmd_help();
    } else if (cmd == "quit" || cmd == "q" || cmd == "exit") {
        m_running = false;
    } else {
        spdlog::warn("Unknown command: '{}'.  Type 'h' for help.", cmd);
    }
}

// ── Commands ──────────────────────────────────────────────────────────────────

void DaemonService::cmd_render(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        spdlog::error("Usage: r <comp_id> <frame> [output]");
        return;
    }

    const auto& comp_id = args[0];
    const Frame frame(std::stoi(args[1]));
    std::string output = args.size() > 2
                             ? args[2]
                             : "output/daemon_####.png";
    output = format_output_path(output, static_cast<i32>(frame));

    if (!m_registry.contains(comp_id)) {
        print_render_error(
            graph::NodeExecutionError{
                graph::RenderBackendErrorCode::InvalidInput,
                "composition_registry",
                "unknown composition '" + comp_id + "'"
            },
            comp_id,
            frame);
        return;
    }

    auto comp = m_registry.create(comp_id);
    auto compiled = chronon3d::compile_composition(
        comp, CompositionCompileContext{});
    if (!compiled) {
        spdlog::error("Failed to compile composition '{}': {}",
                      comp_id, compiled.error().message);
        return;
    }

    const auto t0 = profiling::now();
    // Runtime execution accepts only the immutable compiled composition.
    auto fb = m_engine->render_compiled(
        std::move(compiled).value(), frame);
    const auto t1 = profiling::now();

    if (!fb) {
        const auto structured = m_engine->last_render_error();
        if (structured) {
            print_render_error(*structured, comp_id, frame);
        } else {
            print_render_error(
                graph::NodeExecutionError{
                    graph::RenderBackendErrorCode::ExecutionFailure,
                    "render",
                    "renderer returned a null framebuffer without a structured node error"
                },
                comp_id,
                frame);
        }
        return;
    }

    const double render_ms = profiling::duration_ms(t0, t1);

    // ── Save to disk ─────────────────────────────────────────────────
    std::filesystem::path out_path(output);
    if (out_path.has_parent_path()) {
        std::filesystem::create_directories(out_path.parent_path());
    }

    const auto encode_t0 = profiling::now();
    ImageWriteOptions write_opts;
    write_opts.format = image_format_from_path(output);
    if (!save_image(*fb, output, write_opts)) {
        spdlog::error("Failed to save frame to '{}'", output);
        return;
    }
    const double encode_ms = profiling::duration_ms(encode_t0, profiling::now());

    m_render_count++;
    m_total_render_ms += render_ms;

    spdlog::info("✅ {} f{} → {}  |  render {:.1f}ms  encode {:.1f}ms",
                 comp_id, frame, output, render_ms, encode_ms);
}

void DaemonService::cmd_reload() {
    if (m_options.build_command.empty()) {
        spdlog::warn("No build command configured.  Skipping reload.");
        return;
    }

    spdlog::info("🔨 Building: {}", m_options.build_command);
    const auto t0 = profiling::now();
    int ret = std::system(m_options.build_command.c_str());
    const auto t1 = profiling::now();

    if (ret != 0) {
        spdlog::error("Build failed (exit code {}).  Engine state preserved.",
                      ret);
        return;
    }

    spdlog::info("✅ Build OK ({:.1f}s).  New binary built.  Use "
                 "`chronon watch <comp>` for hot-reload, or restart this "
                 "daemon to pick up the new binary.",
                 profiling::duration_ms(t0, t1) / 1000.0);
}

void DaemonService::cmd_clear_caches() {
    const auto t0 = profiling::now();
    m_engine->clear_caches();
    m_engine->reset_counters();
    const auto t1 = profiling::now();

    spdlog::info("🧹 All caches cleared in {:.1f}ms",
                 profiling::duration_ms(t0, t1));
}

void DaemonService::cmd_status() {
    // P1-F Pass D — `engine->renderer()->counters()` was the only OPP-internal
    // escape hatch that ran through RenderEngineAccess.  Counters are no
    // longer exposed on the public SDK surface (the V0.1 facade keeps that
    // internal).  The daemon-side status block drops the counters panel but
    // keeps the per-session tallies (`m_render_count`, `m_total_render_ms`)
    // which are still visible and useful.

    spdlog::info("");
    spdlog::info("═══ Daemon Status ═══");
    spdlog::info("  Frames rendered : {}", m_render_count);
    spdlog::info("  Total render ms : {:.1f}", m_total_render_ms);
    if (m_render_count > 0) {
        spdlog::info("  Avg render ms   : {:.1f}",
                     m_total_render_ms / m_render_count);
    }
    spdlog::info("");
}

void DaemonService::cmd_help() {
    spdlog::info("");
    spdlog::info("Commands (short aliases in parentheses):");
    spdlog::info("  r      <comp> <frame> [out]   Render a single frame");
    spdlog::info("  st                             Show engine statistics");
    spdlog::info("  cc                             Clear all caches (FB pool, fonts, nodes)");
    spdlog::info("  rl                             Rebuild project (requires build command)");
    spdlog::info("  h                              Show this help");
    spdlog::info("  q                              Shutdown daemon");
    spdlog::info("");
}

// ── UNIX-socket IPC (RenderingGen → Chronon) ───────────────────────────────

void DaemonService::run_socket(const std::string& path) {
    ipc::UnixSocketServer server;
    try {
        server.listen(path);
    } catch (const std::system_error& e) {
        spdlog::error("Failed to bind Unix socket '{}': {}", path, e.what());
        return;
    }

    spdlog::info("🔌 Daemon listening on Unix socket '{}'", server.path());
    spdlog::info("   PREFETCH_ASSET | PREPARE_PLAN | RENDER_OVERLAY | STATUS | SHUTDOWN");

    const int rc = server.serve([this](const ipc::Request& req) {
        return handle_ipc(req);
    });

    if (rc != 0) {
        spdlog::error("Unix socket serve loop exited with error {} ({})",
                      rc, std::strerror(rc));
    }
    spdlog::info("Daemon socket shutdown. {} frames rendered in {:.1f}ms total.",
                 m_render_count, m_total_render_ms);
}

ipc::Reply DaemonService::handle_ipc(const ipc::Request& req) {
    switch (req.cmd) {
        case ipc::Command::PrefetchAsset:
            return ipc_prefetch_asset(req.payload);
        case ipc::Command::PreparePlan:
            return ipc_prepare_plan(req.payload);
        case ipc::Command::RenderOverlay:
            return ipc_render_overlay(req.payload);
        case ipc::Command::Status:
            return ipc_status();
        case ipc::Command::Shutdown:
            return ipc::Reply{ipc::Status::Shutdown, "bye"};
        default:
            return ipc::Reply{ipc::Status::BadRequest, "unknown command"};
    }
}

ipc::Reply DaemonService::ipc_prefetch_asset(const std::string& path) {
    if (path.empty()) {
        return ipc::Reply{ipc::Status::BadRequest, "PREFETCH_ASSET requires an asset path"};
    }
    // Register the asset in the engine-owned registry so subsequent renders
    // resolve it from the warm cache instead of re-importing it.
    const auto id = m_engine->assets().import_by_extension(path);
    if (!id) {
        return ipc::Reply{ipc::Status::NotFound,
                          "unrecognized asset extension: '" + path + "'"};
    }
    spdlog::info("📦 PREFETCH_ASSET {} (id {})", path, *id);
    return ipc::Reply{ipc::Status::Ok, "prefetched"};
}

ipc::Reply DaemonService::ipc_prepare_plan(const std::string& comp_id) {
    if (comp_id.empty()) {
        return ipc::Reply{ipc::Status::BadRequest, "PREPARE_PLAN requires a composition id"};
    }
    if (!m_registry.contains(comp_id)) {
        return ipc::Reply{ipc::Status::NotFound, "unknown composition '" + comp_id + "'"};
    }
    try {
        auto comp = m_registry.create(comp_id);
        m_prepared_job =
            std::make_unique<PreparedRenderJob>(m_engine->prepare(comp));
        m_prepared_comp_id = comp_id;
    } catch (const std::exception& e) {
        m_prepared_job.reset();
        m_prepared_comp_id.clear();
        return ipc::Reply{ipc::Status::Error, std::string{"prepare failed: "} + e.what()};
    }
    spdlog::info("🧠 PREPARE_PLAN '{}' — resource plan slots: {}",
                 comp_id, m_prepared_job->resource_plan().slots.size());
    return ipc::Reply{ipc::Status::Ok, "plan ready"};
}

ipc::Reply DaemonService::ipc_render_overlay(const std::string& args) {
    const auto tokens = split_args(args);
    if (tokens.empty()) {
        return ipc::Reply{ipc::Status::BadRequest,
                          "RENDER_OVERLAY requires '<frame> [output]'"};
    }
    if (!m_prepared_job) {
        return ipc::Reply{ipc::Status::Error,
                          "no prepared plan — send PREPARE_PLAN first"};
    }

    Frame frame{0};
    try {
        frame = Frame(std::stoll(tokens[0]));
    } catch (const std::exception&) {
        return ipc::Reply{ipc::Status::BadRequest,
                          "invalid frame number: '" + tokens[0] + "'"};
    }

    std::string output = tokens.size() > 1 ? tokens[1]
                                           : "output/overlay_####.png";
    output = format_output_path(output, static_cast<i32>(frame));

    std::shared_ptr<Framebuffer> fb;
    try {
        fb = m_prepared_job->render(frame);
    } catch (const std::exception& e) {
        return ipc::Reply{ipc::Status::Error,
                          std::string{"render failed: "} + e.what()};
    }
    if (!fb) {
        return ipc::Reply{ipc::Status::Error,
                          "renderer returned a null framebuffer"};
    }

    std::filesystem::path out_path(output);
    if (out_path.has_parent_path()) {
        std::filesystem::create_directories(out_path.parent_path());
    }
    ImageWriteOptions write_opts;
    write_opts.format = image_format_from_path(output);
    if (!save_image(*fb, output, write_opts)) {
        return ipc::Reply{ipc::Status::Error, "failed to save frame to '" + output + "'"};
    }

    m_render_count++;
    return ipc::Reply{ipc::Status::Ok, output};
}

ipc::Reply DaemonService::ipc_status() {
    std::ostringstream ss;
    ss << "frames_rendered=" << m_render_count
       << " total_ms=" << m_total_render_ms
       << " prepared_comp=" << (m_prepared_comp_id.empty() ? "(none)" : m_prepared_comp_id);
    return ipc::Reply{ipc::Status::Ok, ss.str()};
}

} // namespace chronon3d::cli
