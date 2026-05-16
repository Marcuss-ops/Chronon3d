#include "../commands.hpp"
#include <chronon3d/runtime/watch_service.hpp>
#include <spdlog/spdlog.h>
#include <cstdlib>

namespace chronon3d::cli {

int command_watch(const CompositionRegistry& registry, const std::string& comp_id) {
    if (!registry.contains(comp_id)) {
        spdlog::error("Unknown composition: {}", comp_id);
        return 1;
    }

    spdlog::info("Watching for changes in src/, include/, apps/...");
    
    runtime::WatchOptions options;
    options.watch_dirs = {"src", "include", "apps"};
    
#ifdef _WIN32
    const char* preset_env = std::getenv("CHRONON_PRESET");
    const std::string preset = preset_env ? preset_env : "win-release";
    const bool is_debug = preset.find("debug") != std::string::npos;
    options.build_command = std::string("powershell -ExecutionPolicy Bypass -File tools\\chronon-win.ps1 -Configuration ") +
                            (is_debug ? "Debug" : "Release") + " -SkipInstall -SkipCacheInstall";
#else
    options.build_command = "bash tools/chronon-linux.sh";
#endif

    runtime::WatchService::watch(options, [&]() {
        spdlog::info("Change detected! Rendering...");
        RenderArgs args;
        args.comp_id = comp_id;
        args.frames = "0";
        args.output = "output/watch_####.png";
        args.diagnostic = true;
        command_render(registry, args);
    });

    return 0;
}

} // namespace chronon3d::cli
