#include <doctest/doctest.h>
#include "../../apps/chronon3d_cli/utils/project/chronon_project_render.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace chronon3d::cli {

#if !defined(_WIN32)
TEST_CASE("chronon.json project dispatch preserves subprocess arguments") {
    const auto root = std::filesystem::temp_directory_path() /
        ("chronon-project-dispatch-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "build");
    const auto cleanup = [&] { std::error_code ignored; std::filesystem::remove_all(root, ignored); };

    {
        std::ofstream manifest(root / "chronon.json");
        manifest << nlohmann::json{{"name", "fixture-renderer"}}.dump();
    }

    const auto arg_log = root / "arguments.txt";
    const auto cwd_log = root / "working-directory.txt";
    const auto binary = root / "build" / "fixture-renderer";
    {
        std::ofstream script(binary);
        script << "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$CHRONON_PROJECT_DISPATCH_TEST_LOG\"\n"
                  "pwd > \"$CHRONON_PROJECT_DISPATCH_TEST_CWD\"\n"
                  "exit \"${CHRONON_PROJECT_DISPATCH_TEST_EXIT:-0}\"\n";
    }
    std::filesystem::permissions(
        binary, std::filesystem::perms::owner_read |
                    std::filesystem::perms::owner_write |
                    std::filesystem::perms::owner_exec);

    setenv("CHRONON_PROJECT_DISPATCH_TEST_LOG", arg_log.c_str(), 1);
    setenv("CHRONON_PROJECT_DISPATCH_TEST_CWD", cwd_log.c_str(), 1);
    setenv("CHRONON_PROJECT_DISPATCH_TEST_EXIT", "7", 1);
    std::string diagnostic;
    const auto status = dispatch_project_render(
        root, "Hello World", "output file.mp4", diagnostic);
    unsetenv("CHRONON_PROJECT_DISPATCH_TEST_LOG");
    unsetenv("CHRONON_PROJECT_DISPATCH_TEST_CWD");
    unsetenv("CHRONON_PROJECT_DISPATCH_TEST_EXIT");

    REQUIRE(status.has_value());
    CHECK(*status == 7);
    CHECK(diagnostic.empty());
    std::ifstream args_stream(arg_log);
    const std::string args((std::istreambuf_iterator<char>(args_stream)),
                           std::istreambuf_iterator<char>());
    CHECK(args == "--render\nHello World\n-o\noutput file.mp4\n");
    std::ifstream cwd_stream(cwd_log);
    std::string child_cwd;
    std::getline(cwd_stream, child_cwd);
    CHECK(std::filesystem::path(child_cwd) == std::filesystem::absolute(root));
    cleanup();
}

TEST_CASE("chronon.json project dispatch reports missing build and rejects path names") {
    const auto root = std::filesystem::temp_directory_path() /
        ("chronon-project-dispatch-missing-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    std::string diagnostic;
    {
        std::ofstream manifest(root / "chronon.json");
        manifest << R"({"name":"missing-renderer"})";
    }
    auto status = dispatch_project_render(root, "Comp", "out.mp4", diagnostic);
    REQUIRE(status.has_value());
    CHECK(*status == 1);
    CHECK(diagnostic.find("Build the project first") != std::string::npos);

    diagnostic.clear();
    {
        std::ofstream manifest(root / "chronon.json");
        manifest << R"({"name":"../escape"})";
    }
    status = dispatch_project_render(root, "Comp", "out.mp4", diagnostic);
    REQUIRE(status.has_value());
    CHECK(*status == 1);
    CHECK(diagnostic.find("simple executable name") != std::string::npos);
    std::filesystem::remove_all(root);
}

TEST_CASE("chronon project dispatch leaves ordinary working directories alone") {
    const auto root = std::filesystem::temp_directory_path() /
        ("chronon-project-dispatch-none-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    std::string diagnostic;
    const auto status = dispatch_project_render(root, "Comp", "out.mp4", diagnostic);
    CHECK_FALSE(status.has_value());
    CHECK(diagnostic.empty());
    std::filesystem::remove_all(root);
}
#endif

} // namespace chronon3d::cli
