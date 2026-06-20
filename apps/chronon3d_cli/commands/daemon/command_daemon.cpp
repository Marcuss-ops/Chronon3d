#include "../../commands.hpp"
#include "../../daemon/daemon_service.hpp"

#include <spdlog/spdlog.h>

namespace chronon3d::cli {

int command_daemon(const CompositionRegistry& registry,
                   const std::string& assets_root,
                   const std::string& build_command) {
    DaemonOptions options;
    options.assets_root = assets_root;
    options.build_command = build_command;
    options.watch_dirs = {"src", "include", "apps", "content"};

    DaemonService daemon(registry, std::move(options));
    daemon.run();

    return 0;
}

} // namespace chronon3d::cli
