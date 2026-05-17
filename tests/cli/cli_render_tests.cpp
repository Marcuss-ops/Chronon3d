#include <doctest/doctest.h>
#include <filesystem>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>

namespace {

std::string find_cli_path() {
    std::vector<std::string> candidates = {
        "build/chronon/linux-debug/apps/chronon3d_cli/chronon3d_cli",
        "build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli",
        "build/chronon/linux-relwithdebinfo/apps/chronon3d_cli/chronon3d_cli",
        "apps/chronon3d_cli/chronon3d_cli",
        "../apps/chronon3d_cli/chronon3d_cli",
        "../../apps/chronon3d_cli/chronon3d_cli",
        "chronon3d_cli"
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    // Try scanning the build directory recursively if not found in common paths
    if (std::filesystem::exists("build")) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator("build")) {
            if (entry.is_regular_file() && entry.path().filename() == "chronon3d_cli") {
                return entry.path().string();
            }
        }
    }
    return "chronon3d_cli"; // Fallback to PATH search
}

int run_command(const std::string& cmd) {
    return std::system(cmd.c_str());
}

} // namespace

TEST_CASE("Test 16.1 — CLI list output") {
    std::string cli = find_cli_path();
    std::string cmd = cli + " list";
    int code = run_command(cmd);
    // Should run successfully
    CHECK(code == 0);
}

TEST_CASE("Test 16.2 — CLI info output with valid ID") {
    std::string cli = find_cli_path();
    std::string cmd = cli + " info BackgroundGrid";
    int code = run_command(cmd);
    CHECK(code == 0);
}

TEST_CASE("Test 16.3 — CLI render produces correct file output") {
    std::string cli = find_cli_path();
    const std::filesystem::path out_path = "output/debug/cli_render_out.png";
    std::filesystem::remove(out_path);

    std::string cmd = cli + " render BackgroundGrid -o " + out_path.string() + " --frames 0";
    int code = run_command(cmd);
    CHECK(code == 0);
    CHECK(std::filesystem::exists(out_path));
}

TEST_CASE("Test 16.4 — CLI render validation output if invalid TOML is provided") {
    std::string cli = find_cli_path();
    // Providing a non-existent or invalid TOML file
    std::string cmd = cli + " render non_existent_file_xyz.specscene --frames 0";
    int code = run_command(cmd);
    // Should fail gracefully with non-zero exit code
    CHECK(code != 0);
}

TEST_CASE("Test 16.5 — CLI render handles missing/invalid composition names gracefully") {
    std::string cli = find_cli_path();
    std::string cmd = cli + " render UnknownCompositionXYZ --frames 0";
    int code = run_command(cmd);
    CHECK(code != 0);
}

TEST_CASE("Test 16.6 — CLI render respects settings overrides") {
    std::string cli = find_cli_path();

    // Compile a custom specscene with overrides
    const std::filesystem::path toml_path = "output/debug/cli_override.toml";
    std::filesystem::create_directories(toml_path.parent_path());

    std::ofstream out(toml_path);
    out << R"(
[scene]
name = "CliOverrideScene"
width = 64
height = 64
duration = 5

[[layer]]
id = "box"
position = [32.0, 32.0, 0.0]
is_3d = false

[[layer.visuals]]
type = "rect"
size = [20.0, 20.0]
color = [0.0, 1.0, 0.0, 1.0] # Green
pos = [0.0, 0.0, 0.0]
)";
    out.close();

    const std::filesystem::path out_img = "output/debug/cli_override_out.png";
    std::filesystem::remove(out_img);

    // Render with overrides: --graph option and explicit output file
    std::string cmd = cli + " render " + toml_path.string() + " -o " + out_img.string() + " --frames 0 --graph";
    int code = run_command(cmd);
    CHECK(code == 0);
    CHECK(std::filesystem::exists(out_img));

    // Clean up
    std::filesystem::remove(toml_path);
    std::filesystem::remove(out_img);
}
