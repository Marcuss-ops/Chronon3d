#include "../commands.hpp"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <cstdlib>

namespace chronon3d {
namespace cli {

int command_watch(const CompositionRegistry& registry, const std::string& comp_id) {
    if (!registry.contains(comp_id)) {
        spdlog::error("Unknown composition: {}", comp_id);
        return 1;
    }

    spdlog::info("Watching for changes... (Polling src/ and include/)");
    
    auto get_latest_mtime = []() {
        std::filesystem::file_time_type latest = std::filesystem::file_time_type::min();
        auto check_dir = [&](const std::string& dir) {
            if (std::filesystem::exists(dir)) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
                    if (entry.is_regular_file()) {
                        auto mtime = std::filesystem::last_write_time(entry);
                        if (mtime > latest) latest = mtime;
                    }
                }
            }
        };
        check_dir("src");
        check_dir("include");
        check_dir("apps");
        return latest;
    };

    auto last_write = get_latest_mtime();
    
    RenderArgs args;
    args.comp_id = comp_id;
    args.frames = "0";
    args.output = "output/watch_####.png";
    args.diagnostic = true;
    command_render(registry, args);

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto current = get_latest_mtime();
        if (current > last_write) {
            spdlog::info("Change detected! Rebuilding and rendering...");
            
            int ret = std::system("xmake -y");
            if (ret == 0) {
                command_render(registry, args);
                last_write = current;
            } else {
                spdlog::error("Build failed. Fix errors to continue.");
                last_write = current;
            }
        }
    }
    return 0;
}

} // namespace cli
} // namespace chronon3d
