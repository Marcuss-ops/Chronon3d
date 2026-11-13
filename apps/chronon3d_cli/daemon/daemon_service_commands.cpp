#include "daemon_service.hpp"
#include "daemon_service_internal.hpp"

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/software/software_execution_context.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include "../utils/common/render_error_formatter.hpp"

#include <spdlog/spdlog.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <utility>

namespace chronon3d::cli {

void DaemonService::run() {
    spdlog::info("");
    spdlog::info("╔══════════════════════════════════════════╗");
    spdlog::info("║   🔥 Chronon3d Daemon Mode — Ready      ║");
    spdlog::info("╠══════════════════════════════════════════╣");
    spdlog::info("║  r  <comp> <frame> [out]     Render     ║");
    spdlog::info("║  st                          Stats      ║");
    spdlog::info("║  cs                          Cache stats║");
    spdlog::info("║  cc                          Clear cache║");
    spdlog::info("║  rl                          Rebuild    ║");
    spdlog::info("║  h                           Help       ║");
    spdlog::info("║  q                           Quit       ║");
    spdlog::info("╚══════════════════════════════════════════╝");
    spdlog::info("");

    std::string line;
    while (m_running.load(std::memory_order_acquire) && std::getline(std::cin, line)) {
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(0, 1);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
        if (line.empty()) continue;
        handle_command(line);
    }

    spdlog::info("");
    spdlog::info("Daemon shutting down. {} frames rendered in {:.1f}ms total.",
                 m_render_count.load(std::memory_order_relaxed),
                 m_total_render_ms.load(std::memory_order_relaxed));
}

void DaemonService::handle_command(const std::string& line) {
    auto args = daemon_detail::split_args(line);
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
    } else if (cmd == "cache-stats" || cmd == "cs") {
        cmd_cache_stats();
    } else if (cmd == "help" || cmd == "h") {
        cmd_help();
    } else if (cmd == "quit" || cmd == "q" || cmd == "exit") {
        m_running.store(false, std::memory_order_release);
    } else {
        spdlog::warn("Unknown command: '{}'.  Type 'h' for help.", cmd);
    }
}

void DaemonService::cmd_render(const std::vector<std::string>& args) {
    (void)args;
    spdlog::error(
        "The interactive composition render command was retired with the "
        "legacy Composition executor. Submit a chronon.render-plan.v2 job "
        "through RENDER_JOB instead.");
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
        spdlog::error("Build failed (exit code {}).  Engine state preserved.", ret);
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
    spdlog::info("");
    spdlog::info("═══ Daemon Status ═══");
    const auto frames_rendered = m_render_count.load(std::memory_order_relaxed);
    const auto total_render_ms = m_total_render_ms.load(std::memory_order_relaxed);
    spdlog::info("  Frames rendered : {}", frames_rendered);
    spdlog::info("  Total render ms : {:.1f}", total_render_ms);
    if (frames_rendered > 0) {
        spdlog::info("  Avg render ms   : {:.1f}",
                     total_render_ms / frames_rendered);
    }
    spdlog::info("");
}

std::string DaemonService::format_cache_stats() const {
    std::lock_guard<std::mutex> lock(m_device_sessions_mutex);
    std::string snapshot;
    for (const auto& [device, renderer] : m_device_sessions) {
        if (!renderer) continue;
        auto* runtime = renderer->execution_context().runtime;
        if (runtime == nullptr) continue;
        snapshot += "Device " + std::to_string(device) + ":\n";
        snapshot += cache::format_cache_snapshot(runtime->diagnostics());
    }
    if (!snapshot.empty()) return snapshot;
    if (m_engine) return m_engine->cache_snapshot();
    return "[cache] No active runtime.\n";
}

void DaemonService::cmd_cache_stats() {
    std::cout << format_cache_stats();
    std::cout.flush();
}

void DaemonService::cmd_help() {
    spdlog::info("");
    spdlog::info("Commands (short aliases in parentheses):");
    spdlog::info("  r      <comp> <frame> [out]   Render a single frame");
    spdlog::info("  st                             Show engine statistics");
    spdlog::info("  cs / cache-stats               Show the runtime cache diagnostics");
    spdlog::info("  cc                             Clear all caches (FB pool, fonts, nodes)");
    spdlog::info("  rl                             Rebuild project (requires build command)");
    spdlog::info("  h                              Show this help");
    spdlog::info("  q                              Shutdown daemon");
    spdlog::info("");
}

} // namespace chronon3d::cli
