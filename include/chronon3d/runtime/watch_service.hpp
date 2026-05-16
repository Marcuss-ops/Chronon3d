#pragma once

#include <string>
#include <vector>
#include <functional>
#include <filesystem>

namespace chronon3d::runtime {

struct WatchOptions {
    std::vector<std::string> watch_dirs;
    std::string build_command;
    int poll_ms = 500;
};

class WatchService {
public:
    using ChangeCallback = std::function<void()>;

    static void watch(const WatchOptions& options, ChangeCallback on_change);

private:
    static std::filesystem::file_time_type get_latest_mtime(const std::vector<std::string>& dirs);
};

} // namespace chronon3d::runtime
