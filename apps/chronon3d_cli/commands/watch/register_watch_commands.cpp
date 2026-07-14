// ==============================================================================
// TICKET-V3-CLI-UNIFICATION-WATCH-SUPERVISOR (Blocco 4.1, Commit 1 of 3)
//
// `chronon watch <comp> --frame N -o /tmp/preview.png` — supervisor process
// that watches source files, rebuilds, and re-execs the CLI binary as a
// subprocess to render the selected frame.
//
// Per audit §17 verbatim "Come completarlo senza plugin dinamici":
//   watch process
//   → rileva modifica
//   → build incrementale
//   → avvia il nuovo CLI come subprocess
//   → renderizza frame selezionato
//   → aggiorna output
//
// Architecture (no .so, no plugin ABI, no browser, no hot-reload of own code):
//   1. Snapshot mtimes of all files under `--watch-dirs` (default: src, include, apps)
//   2. Sleep `--poll-ms` (default 500), re-snapshot
//   3. If mtimes differ: run `--build` (default `bash build-fast.sh`) then
//      fork a subprocess that re-execs the SAME binary (resolved via
//      `/proc/self/exe` on Linux) with `chronon render <comp> --frame N -o <output>`.
//   4. Loop forever.  Ctrl+C kills the supervisor (the subprocess is independent).
//
// Why subprocess re-exec instead of in-process hot-reload: the audit
// explicitly forbids `.so` / plugin ABI / browser mechanisms.  Subprocess
// re-exec is the canonical Linux idiom and gives correct inotify-style
// behavior without any dynamic-loader magic.  The supervisor never
// reloads its own code — only the subprocess picks up the freshly-built
// binary.  The daemon (`chronon3d_cli daemon`) keeps the warm-render-shell
// role and is NOT used by the watch.
//
// `--props-file` is a V0 forward-point: the audit shows
// `chronon watch ProductLaunch --props-file props.json -o /tmp/preview.png`
// as the aspirational flow, but the per-composition props decoder
// (TICKET-ADD-LOADER-FOR-CHRONON-JSON, P1) is not yet landed.  The flag
// is accepted but not applied to the subprocess RenderJob — forward-point
// ticket TICKET-WATCH-PROPS-FILE.
#include "../../command_registry.hpp"
#include "../../commands.hpp"

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace chronon3d::cli {

namespace {

// Resolve the path to the currently-running binary.  Linux-only via
// /proc/self/exe; other platforms require explicit --chronon-binary.
std::filesystem::path get_current_exe_path() {
#if defined(__linux__)
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return {};
    return p;
#else
    return {};
#endif
}

// Recursively snapshot the mtime of every regular file under each root.
// Skips `build*/`, `.git/`, and `vcpkg/` to keep the scan focused on
// user source.  Returns a deterministic map keyed by absolute path so
// set-equality is straightforward.
std::map<std::filesystem::path, std::filesystem::file_time_type>
snapshot_mtimes(const std::vector<std::filesystem::path>& roots) {
    std::map<std::filesystem::path, std::filesystem::file_time_type> snap;
    for (const auto& root : roots) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) continue;
        for (std::filesystem::recursive_directory_iterator it(
                 root, std::filesystem::directory_options::skip_permission_denied, ec);
             !ec && it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {
            const auto& entry = *it;
            if (!entry.is_regular_file(ec)) continue;
            // Skip generated / vendored / build outputs.
            const std::string rel =
                std::filesystem::relative(entry.path(), root, ec).string();
            if (rel.empty()) continue;
            if (rel.rfind("build/", 0) == 0) continue;
            if (rel.rfind("build-debug/", 0) == 0) continue;
            if (rel.rfind("build-release/", 0) == 0) continue;
            if (rel.rfind(".git/", 0) == 0) continue;
            if (rel.rfind("vcpkg/", 0) == 0) continue;
            snap.emplace(entry.path(), entry.last_write_time(ec));
        }
    }
    return snap;
}

// True iff the two snapshots differ (any file added, removed, or
// re-touched).  Per audit §17, a single mtime change is enough to
// trigger a rebuild.
bool mtimes_changed(const std::map<std::filesystem::path,
                                    std::filesystem::file_time_type>& a,
                    const std::map<std::filesystem::path,
                                    std::filesystem::file_time_type>& b) {
    if (a.size() != b.size()) return true;
    for (const auto& [p, t] : a) {
        auto it = b.find(p);
        if (it == b.end()) return true;
        if (it->second != t) return true;
    }
    return false;
}

int run_build(const std::string& build_cmd) {
    if (build_cmd.empty()) return 0;
    spdlog::info("🔨 Build: {}", build_cmd);
    int ret = std::system(build_cmd.c_str());
    if (ret != 0) {
        spdlog::error("❌ Build failed (exit {})", ret);
    }
    return ret;
}

int run_render_subprocess(const std::filesystem::path& chronon_binary,
                          const std::string& comp_id,
                          int frame,
                          const std::filesystem::path& output) {
    // Quote the binary path and the output path so spaces/special
    // characters are tolerated.  We use `std::system` because the
    // supervisor never reaps the subprocess state (it just waits for
    // exit + logs the status).
    std::ostringstream cmd;
    cmd << '"' << chronon_binary.string() << '"'
        << " render " << comp_id
        << " --frame " << frame
        << " -o " << '"' << output.string() << '"';
    spdlog::info("🖼  Render: {}", cmd.str());
    int ret = std::system(cmd.str().c_str());
    return ret;
}

}  // namespace

void register_watch_commands(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<WatchArgs>();
    auto& args = *state;

    auto* cmd = app.add_subcommand(
        "watch",
        "Watch source files, rebuild, and re-render a selected frame on each change");
    cmd->add_option("<comp_id>", args.comp_id,
                    "Composition to render")
        ->required();
    cmd->add_option("--frame", args.frame,
                    "Frame to render (default 0)")
        ->default_val(0);
    cmd->add_option("-o,--output", args.output,
                    "Output PNG path (default /tmp/preview.png)")
        ->default_val("/tmp/preview.png");
    cmd->add_option("--watch-dirs", args.watch_dirs,
                    "Directories to watch (default: src include apps)")
        ->take_all();
    cmd->add_option("--build", args.build_command,
                    "Build command (default: bash build-fast.sh)")
        ->default_val("bash build-fast.sh");
    cmd->add_option("--poll-ms", args.poll_ms,
                    "Poll interval in ms (default 500)")
        ->default_val(500);
    cmd->add_option("--chronon-binary", args.chronon_binary,
                    "Path to chronon3d_cli binary (default: /proc/self/exe on Linux)")
        ->default_val("");
    cmd->add_flag("--no-build", args.no_build,
                  "Skip the build step (render-only on every change)");
    // TICKET-WATCH-PROPS-FILE (V0 forward-point) — accepted but not yet
    // applied to the subprocess RenderJob; per-composition props decoder
    // (TICKET-ADD-LOADER-FOR-CHRONON-JSON, P1) lands separately.
    cmd->add_option("--props-file", args.props_file,
                    "(V0 forward-point) props JSON to apply to the subprocess render");
    cmd->allow_windows_style_options();

    cmd->callback([state, &ctx]() {
        const auto& a = *state;

        // Resolve the chronon3d_cli binary path.
        std::filesystem::path binary = a.chronon_binary;
        if (binary.empty()) {
            binary = get_current_exe_path();
        }
        if (binary.empty() || !std::filesystem::exists(binary)) {
            fmt::print(stderr,
                "Error: could not determine chronon3d_cli binary path. "
                "Pass --chronon-binary <path>.\n");
            ctx.exit_code = 1;
            return;
        }

        // Resolve watch dirs (default: src, include, apps).
        std::vector<std::filesystem::path> watch_dirs = a.watch_dirs;
        if (watch_dirs.empty()) {
            watch_dirs = {std::filesystem::path("src"),
                          std::filesystem::path("include"),
                          std::filesystem::path("apps")};
        }

        // TICKET-WATCH-PROPS-FILE (V0 forward-point) — close the UX trap:
        // --props-file is accepted but NOT yet applied to the subprocess
        // RenderJob.  Print a one-time warning so users who pass it are
        // not silently misled.  Wait until TICKET-ADD-LOADER-FOR-CHRONON-JSON
        // (P1) lands the per-composition props decoder.
        if (!a.props_file.empty()) {
            spdlog::warn("--props-file is a V0 forward-point "
                         "(TICKET-WATCH-PROPS-FILE): accepted but not yet "
                         "applied to subprocess render.");
        }

        // Banner.
        spdlog::info("👁  Chronon3D Watch — comp={} frame={} output={}",
                     a.comp_id, a.frame, a.output.string());
        spdlog::info("   watch dirs: [{}]", fmt::join(watch_dirs, ", "));
        spdlog::info("   build:      {}",
                     a.no_build ? "(skipped)" : a.build_command);
        spdlog::info("   binary:     {}", binary.string());
        spdlog::info("   press Ctrl+C to stop");
        spdlog::info("");

        // Initial snapshot + initial render.
        auto prev = snapshot_mtimes(watch_dirs);
        spdlog::info("📸 Initial snapshot: {} files", prev.size());
        if (!a.no_build) {
            if (run_build(a.build_command) != 0) {
                ctx.exit_code = 1;
                return;
            }
        }
        run_render_subprocess(binary, a.comp_id, a.frame, a.output);

        // Poll loop.
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(a.poll_ms));
            auto curr = snapshot_mtimes(watch_dirs);
            if (!mtimes_changed(prev, curr)) continue;
            spdlog::info("📝 Change detected ({} → {} files)",
                         prev.size(), curr.size());
            prev = curr;

            if (!a.no_build) {
                if (run_build(a.build_command) != 0) continue;
            }
            run_render_subprocess(binary, a.comp_id, a.frame, a.output);
        }
    });
}

}  // namespace chronon3d::cli
