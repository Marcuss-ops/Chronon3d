#include <chronon3d/runtime/watch_service.hpp>
#include <thread>
#include <chrono>
#include <cstdlib>

namespace chronon3d::runtime {

void WatchService::watch(const WatchOptions& options, ChangeCallback on_change) {
    auto last_write = get_latest_mtime(options.watch_dirs);
    
    // Initial trigger
    on_change();

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(options.poll_ms));
        auto current = get_latest_mtime(options.watch_dirs);
        
        if (current > last_write) {
            last_write = current;
            
            if (!options.build_command.empty()) {
                int ret = std::system(options.build_command.c_str());
                if (ret == 0) {
                    on_change();
                }
            } else {
                on_change();
            }
        }
    }
}

std::filesystem::file_time_type WatchService::get_latest_mtime(const std::vector<std::string>& dirs) {
    std::filesystem::file_time_type latest = std::filesystem::file_time_type::min();
    for (const auto& dir : dirs) {
        if (std::filesystem::exists(dir)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    auto mtime = std::filesystem::last_write_time(entry);
                    if (mtime > latest) latest = mtime;
                }
            }
        }
    }
    return latest;
}

} // namespace chronon3d::runtime
