#include "chronon_project_render.hpp"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace chronon3d::cli {
namespace {

bool safe_binary_name(std::string_view name) {
    if (name.empty() || name == "." || name == "..") return false;
    for (const unsigned char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

int run_child(const std::filesystem::path& binary,
              const std::filesystem::path& project_root,
              const std::vector<std::string>& arguments) {
    const std::string binary_string = binary.string();
    std::vector<const char*> argv;
    argv.reserve(arguments.size() + 2);
    argv.push_back(binary_string.c_str());
    for (const auto& argument : arguments) argv.push_back(argument.c_str());
    argv.push_back(nullptr);

#if defined(_WIN32)
    std::vector<const char*> narrow_argv = argv;
    const intptr_t status = _spawnv(_P_WAIT, binary_string.c_str(), narrow_argv.data());
    return status < 0 ? 127 : static_cast<int>(status);
#else
    const pid_t child = fork();
    if (child < 0) return 127;
    if (child == 0) {
        if (::chdir(project_root.c_str()) != 0) {
            _exit(126);
        }
        execv(binary_string.c_str(), const_cast<char* const*>(argv.data()));
        _exit(errno == ENOENT ? 127 : 126);
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return 127;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 127;
#endif
}

} // namespace

std::optional<int> dispatch_project_render(
    const std::filesystem::path& project_root,
    std::string_view composition,
    std::string_view output,
    std::string& diagnostic) {
    std::error_code root_error;
    const auto absolute_root = std::filesystem::absolute(project_root, root_error);
    if (root_error) {
        diagnostic = "Could not resolve project directory: " + root_error.message();
        return 1;
    }

    const auto manifest_path = absolute_root / "chronon.json";
    std::ifstream manifest_stream(manifest_path);
    if (!manifest_stream.is_open()) {
        if (std::filesystem::exists(manifest_path)) {
            diagnostic = "Could not open project manifest: " + manifest_path.string();
            return 1;
        }
        return std::nullopt;
    }

    nlohmann::json manifest;
    try {
        manifest_stream >> manifest;
    } catch (const std::exception& e) {
        diagnostic = "Invalid chronon.json: " + std::string(e.what());
        return 1;
    }
    if (!manifest.is_object() || !manifest.contains("name") ||
        !manifest["name"].is_string()) {
        diagnostic = "chronon.json must contain a string field 'name'";
        return 1;
    }

    const auto name = manifest["name"].get<std::string>();
    if (!safe_binary_name(name)) {
        diagnostic = "chronon.json field 'name' must be a simple executable name";
        return 1;
    }

    auto binary = absolute_root / "build" / name;
#if defined(_WIN32)
    binary += ".exe";
#endif
    std::error_code ec;
    if (!std::filesystem::is_regular_file(binary, ec)) {
        diagnostic = "Build the project first: `cmake -B build && cmake --build build` "
                     "(expected executable: " + binary.string() + ")";
        return 1;
    }
#if !defined(_WIN32)
    if (::access(binary.c_str(), X_OK) != 0) {
        diagnostic = "Project renderer is not executable: " + binary.string();
        return 1;
    }
#endif

    return run_child(binary, absolute_root, {"--render", std::string(composition),
                              "-o", std::string(output)});
}

} // namespace chronon3d::cli
