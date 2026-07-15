#include "daemon_service.hpp"

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include "utils/common/render_error_formatter.hpp"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
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

    const auto t0 = profiling::now();
    // P1-F Pass D — `engine->render(comp, frame)` is the canonical OPP-side
    // entry (replacing the now-removed `render_frame()` which had been
    // `[[deprecated]]` since Pass A).  Returns the `shared_ptr<Framebuffer>`
    // surface exactly as the OPP render loop expects; the SDK façade
    // (chronon3d::sdk::RenderEngine::render) returns Result<RenderOutput, …>
    // for V0.2 consumers.
    auto fb = m_engine->render(comp, frame);
    const auto t1 = profiling::now();

    if (!fb) {
        const auto& structured = m_engine->last_render_error();
        if (structured.has_value()) {
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

} // namespace chronon3d::cli
