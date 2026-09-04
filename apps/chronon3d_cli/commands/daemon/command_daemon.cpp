#include "../../commands.hpp"
#include "../../daemon/daemon_service.hpp"

#include <spdlog/spdlog.h>

namespace chronon3d::cli {

int command_daemon(const CompositionRegistry& registry,
                   const std::string& assets_root,
                   const std::string& build_command,
                   const std::string& socket_path,
                   graph::BackendPreference backend,
                   std::uint32_t gpu_device_id) {
    DaemonOptions options;
    options.assets_root = assets_root;
    switch (backend) {
        case graph::BackendPreference::Software: options.backend = "software"; break;
        case graph::BackendPreference::GPU: options.backend = "vulkan"; break;
        case graph::BackendPreference::Auto: options.backend = "auto"; break;
    }
    options.gpu_device_id = gpu_device_id;
    options.build_command = build_command;
    options.watch_dirs = {"src", "include", "apps", "content"};

    DaemonService daemon(registry, std::move(options));
    if (!socket_path.empty()) {
        daemon.run_socket(socket_path);
    } else {
        daemon.run();
    }

    return 0;
}

} // namespace chronon3d::cli
