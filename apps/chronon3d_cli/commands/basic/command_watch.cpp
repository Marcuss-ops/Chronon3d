#include "../../commands.hpp"
#include "../../utils/watch/watch_service.hpp"
#include <spdlog/spdlog.h>
#include <cstdlib>

namespace chronon3d::cli {

int command_watch(const CompositionRegistry& registry, const std::string& comp_id) {
    if (!registry.contains(comp_id)) {
        spdlog::error("Unknown composition: {}", comp_id);
        return 1;
    }

    spdlog::info("Watching for changes in src/, include/, apps/...");
    
    WatchOptions options;
    options.watch_dirs = {"src", "include", "apps"};
    options.build_command = "bash tools/chronon-linux.sh";

    WatchService::watch(options, [&]() {
        spdlog::info("Change detected! Rendering...");
        RenderArgs args;
        args.comp_id = comp_id;
        args.frames = "0";
        args.output = "output/watch_####.png";
        args.pipeline.diagnostic = true;
        command_render(registry, args);
    });

    return 0;
}

} // namespace chronon3d::cli
