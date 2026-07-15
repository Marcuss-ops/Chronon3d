// ==============================================================================
// `chronon watch <comp> --frame N --props-file props.json -o preview.png`
// watches source files, runs an incremental build, then re-execs the freshly
// built CLI as a subprocess. No plugin ABI, shared library or browser runtime.
// ==============================================================================
#include "../../command_registry.hpp"
#include "../../commands.hpp"

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace chronon3d::cli {

namespace {

std::filesystem::path get_current_exe_path() {
#if defined(__linux__)
    std::error_code ec;
    auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : path;
#else
    return {};
#endif
}

std::string shell_quote(std::string_view value) {
    std::string quoted{"'"};
    for (const char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += '\'';
    return quoted;
}

std::filesystem::path find_build_root(const std::filesystem::path& binary) {
    std::filesystem::path current = binary.parent_path();
    std::error_code ec;
    while (!current.empty()) {
        if (std::filesystem::exists(current / "CMakeCache.txt", ec) && !ec) {
            return current;
        }
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return {};
}

std::string default_build_command(const std::filesystem::path& binary) {
    const auto build_root = find_build_root(binary);
    if (build_root.empty()) return {};
    return "cmake --build " + shell_quote(build_root.string()) +
           " --target chronon3d_cli";
}

std::map<std::filesystem::path, std::filesystem::file_time_type>
snapshot_mtimes(const std::vector<std::filesystem::path>& roots) {
    std::map<std::filesystem::path, std::filesystem::file_time_type> snapshot;
    for (const auto& root : roots) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) continue;
        for (std::filesystem::recursive_directory_iterator it(
                 root, std::filesystem::directory_options::skip_permission_denied, ec);
             !ec && it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {
            const auto& entry = *it;
            if (!entry.is_regular_file(ec)) continue;
            const std::string rel =
                std::filesystem::relative(entry.path(), root, ec).string();
            if (rel.empty()) continue;
            if (rel.rfind("build/", 0) == 0) continue;
            if (rel.rfind("build-debug/", 0) == 0) continue;
            if (rel.rfind("build-release/", 0) == 0) continue;
            if (rel.rfind(".git/", 0) == 0) continue;
            if (rel.rfind("vcpkg/", 0) == 0) continue;
            snapshot.emplace(entry.path(), entry.last_write_time(ec));
        }
    }
    return snapshot;
}

bool mtimes_changed(
    const std::map<std::filesystem::path, std::filesystem::file_time_type>& before,
    const std::map<std::filesystem::path, std::filesystem::file_time_type>& after) {
    if (before.size() != after.size()) return true;
    for (const auto& [path, time] : before) {
        const auto it = after.find(path);
        if (it == after.end() || it->second != time) return true;
    }
    return false;
}

int run_build(const std::string& command) {
    spdlog::info("🔨 Build: {}", command);
    const int result = std::system(command.c_str());
    if (result != 0) spdlog::error("❌ Build failed (exit {})", result);
    return result;
}

int run_render_subprocess(const std::filesystem::path& binary,
                          const std::string& comp_id,
                          int frame,
                          const std::filesystem::path& output,
                          const std::string& props_file) {
    std::ostringstream command;
    command << shell_quote(binary.string())
            << " render " << shell_quote(comp_id)
            << " --frame " << frame
            << " -o " << shell_quote(output.string());
    if (!props_file.empty()) {
        command << " --props-file " << shell_quote(props_file);
    }
    spdlog::info("🖼  Render: {}", command.str());
    return std::system(command.str().c_str());
}

} // namespace

void register_watch_commands(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<WatchArgs>();
    state->build_command.clear();
    auto& args = *state;

    auto* cmd = app.add_subcommand(
        "watch",
        "Watch source files, build incrementally, and re-render a selected frame");
    cmd->add_option("<comp_id>", args.comp_id, "Composition to render")->required();
    cmd->add_option("--frame", args.frame, "Frame to render")->default_val(0);
    cmd->add_option("-o,--output", args.output, "Output PNG path")
        ->default_val("/tmp/preview.png");
    cmd->add_option("--props-file", args.props_file,
                    "Flat JSON props forwarded to the render subprocess");
    cmd->add_option("--watch-dirs", args.watch_dirs,
                    "Directories to watch (default: src include apps)")
        ->take_all();
    cmd->add_option("--build", args.build_command,
                    "Custom incremental build command (default: auto-detect CMake build root)");
    cmd->add_option("--poll-ms", args.poll_ms, "Poll interval in milliseconds")
        ->default_val(500);
    cmd->add_option("--chronon-binary", args.chronon_binary,
                    "CLI binary path (default: /proc/self/exe on Linux)");
    cmd->add_flag("--no-build", args.no_build,
                  "Skip the build step and only re-render on changes");
    cmd->allow_windows_style_options();

    cmd->callback([state, &ctx]() {
        const auto& watch = *state;

        std::filesystem::path binary = watch.chronon_binary;
        if (binary.empty()) binary = get_current_exe_path();
        if (binary.empty() || !std::filesystem::exists(binary)) {
            fmt::print(stderr,
                       "Error: could not determine the CLI binary. "
                       "Pass --chronon-binary <path>.\n");
            ctx.exit_code = 1;
            return;
        }

        std::vector<std::filesystem::path> watch_dirs = watch.watch_dirs;
        if (watch_dirs.empty()) {
            watch_dirs = {"src", "include", "apps"};
        }

        std::string build_command = watch.build_command;
        if (!watch.no_build && build_command.empty()) {
            build_command = default_build_command(binary);
            if (build_command.empty()) {
                fmt::print(stderr,
                           "Error: could not locate CMakeCache.txt above the CLI binary. "
                           "Pass --build <command> or --no-build.\n");
                ctx.exit_code = 1;
                return;
            }
        }

        spdlog::info("👁  Chronon3D Watch — comp={} frame={} output={}",
                     watch.comp_id, watch.frame, watch.output.string());
        spdlog::info("   watch dirs: [{}]", fmt::join(watch_dirs, ", "));
        spdlog::info("   build:      {}", watch.no_build ? "(skipped)" : build_command);
        spdlog::info("   binary:     {}", binary.string());
        if (!watch.props_file.empty()) {
            spdlog::info("   props:      {}", watch.props_file);
        }
        spdlog::info("   press Ctrl+C to stop");

        auto previous = snapshot_mtimes(watch_dirs);
        spdlog::info("📸 Initial snapshot: {} files", previous.size());
        if (!watch.no_build && run_build(build_command) != 0) {
            ctx.exit_code = 1;
            return;
        }
        run_render_subprocess(binary, watch.comp_id, watch.frame,
                              watch.output, watch.props_file);

        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(watch.poll_ms));
            auto current = snapshot_mtimes(watch_dirs);
            if (!mtimes_changed(previous, current)) continue;
            spdlog::info("📝 Change detected ({} → {} files)",
                         previous.size(), current.size());
            previous = std::move(current);

            if (!watch.no_build && run_build(build_command) != 0) continue;
            run_render_subprocess(binary, watch.comp_id, watch.frame,
                                  watch.output, watch.props_file);
        }
    });
}

} // namespace chronon3d::cli
