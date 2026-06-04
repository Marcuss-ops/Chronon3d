#include <doctest/doctest.h>
#include <chronon3d/extension/extension_loader.hpp>
#include <chronon3d/extension/extension_registry.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace chronon3d;

// ── Helpers ──────────────────────────────────────────────────────────────────

/// Minimal in-process ExtensionModule for testing (no shared library needed).
class TestModule : public ExtensionModule {
public:
    explicit TestModule(std::string_view module_id) : m_id(module_id) {}
    std::string_view id() const override { return m_id; }
    void register_with(ExtensionRegistry&) override { ++s_call_count; }
    static int s_call_count;
private:
    std::string m_id;
};

int TestModule::s_call_count = 0;

// ── Tests ────────────────────────────────────────────────────────────────────

TEST_CASE("ExtensionLoader: diagnostics starts empty") {
    ExtensionLoader loader;
    CHECK(loader.diagnostics().empty());
    CHECK(loader.loaded_count() == 0);
}

TEST_CASE("ExtensionLoader: load non-existent file returns false") {
    ExtensionLoader loader;
    ExtensionRegistry reg;
    bool ok = loader.load("/tmp/nonexistent_plugin_12345.so", reg);
    CHECK_FALSE(ok);
    CHECK(loader.diagnostics().size() == 1);
    CHECK_FALSE(loader.diagnostics()[0].success);
    CHECK_FALSE(loader.diagnostics()[0].error.empty());
    CHECK(loader.loaded_count() == 0);
}

TEST_CASE("ExtensionLoader: load_all with non-existent directory returns 0") {
    ExtensionLoader loader;
    ExtensionRegistry reg;
    std::size_t count = loader.load_all("/tmp/nonexistent_dir_12345", reg);
    CHECK(count == 0);
    CHECK(loader.diagnostics().empty());
}

TEST_CASE("ExtensionLoader: load_all with empty directory returns 0") {
    // Create a temp directory with no shared libraries.
    auto tmp = std::filesystem::temp_directory_path() / "chronon3d_loader_test_empty";
    std::filesystem::create_directories(tmp);

    ExtensionLoader loader;
    ExtensionRegistry reg;
    std::size_t count = loader.load_all(tmp, reg);
    CHECK(count == 0);

    std::filesystem::remove_all(tmp);
}

TEST_CASE("ExtensionLoader: load_all skips non-library files") {
    auto tmp = std::filesystem::temp_directory_path() / "chronon3d_loader_test_skip";
    std::filesystem::create_directories(tmp);

    // Create a text file with a .so extension — but it's not a valid shared library
    {
        std::ofstream ofs(tmp / "fake_plugin.so");
        ofs << "not a real shared library";
    }
    // Also create a .txt file
    {
        std::ofstream ofs(tmp / "readme.txt");
        ofs << "hello";
    }

    ExtensionLoader loader;
    ExtensionRegistry reg;
    std::size_t count = loader.load_all(tmp, reg);
    CHECK(count == 0); // fake_plugin.so fails to load as a valid library

    std::filesystem::remove_all(tmp);
}

TEST_CASE("ExtensionLoader: unload_all on empty loader is safe") {
    ExtensionLoader loader;
    loader.unload_all(); // should not crash
    CHECK(loader.loaded_count() == 0);
}

TEST_CASE("ExtensionLoader: diagnostics accumulates across multiple calls") {
    ExtensionLoader loader;
    ExtensionRegistry reg;

    loader.load("/tmp/no1.so", reg);
    loader.load("/tmp/no2.so", reg);
    loader.load("/tmp/no3.so", reg);

    CHECK(loader.diagnostics().size() == 3);
    for (const auto& d : loader.diagnostics()) {
        CHECK_FALSE(d.success);
    }
}

TEST_CASE("ExtensionLoader: load_all with no shared libraries in dir returns 0") {
    auto tmp = std::filesystem::temp_directory_path() / "chronon3d_loader_test_nosofiles";
    std::filesystem::create_directories(tmp);

    // Create a file with wrong extension
    {
        std::ofstream ofs(tmp / "something.bin");
        ofs << "data";
    }

    ExtensionLoader loader;
    ExtensionRegistry reg;
    std::size_t count = loader.load_all(tmp, reg);
    CHECK(count == 0);

    std::filesystem::remove_all(tmp);
}

TEST_CASE("ExtensionModule: API version constant is defined") {
    CHECK(CHRONON_MODULE_API_VERSION == 1);
}

// ── Real plugin loading tests (uses compiled .so) ────────────────────────────

// NOTE: Tests using ExtensionRegistry::instance() must call clear_modules()
// first because the registry is a singleton that persists across test cases.

TEST_CASE("ExtensionLoader: load real mini plugin from .so") {
    // Reset singleton state before this test.
    ExtensionRegistry::instance().clear_modules();

    // Path to the compiled test_plugin shared library.
    // CMake builds it into ${CMAKE_CURRENT_BINARY_DIR}/plugins/
    const auto plugin_path =
        std::filesystem::path(CMAKE_CURRENT_BINARY_DIR) / "plugins" / "libchronon3d_test_plugin.so";

    INFO("plugin path: " << plugin_path);
    REQUIRE(std::filesystem::exists(plugin_path));

    ExtensionLoader loader;
    ExtensionRegistry& reg = ExtensionRegistry::instance();

    CHECK_FALSE(reg.has_module("test_plugin"));

    bool ok = loader.load(plugin_path, reg);
    CHECK(ok);
    CHECK(reg.has_module("test_plugin"));
    CHECK(loader.loaded_count() == 1);

    // Check diagnostics
    const auto& diag = loader.diagnostics();
    CHECK(diag.size() >= 1);
    bool found = false;
    for (const auto& d : diag) {
        if (d.path == plugin_path) {
            CHECK(d.success);
            CHECK(d.id == "test_plugin");
            CHECK(d.error.empty());
            found = true;
            break;
        }
    }
    CHECK(found);

    // Cleanup: destroy modules while .so is still loaded, then close handles
    reg.clear_modules();
    loader.unload_all();
}

TEST_CASE("ExtensionLoader: load_all finds the test plugin in plugins dir") {
    ExtensionRegistry::instance().clear_modules();

    const auto plugins_dir =
        std::filesystem::path(CMAKE_CURRENT_BINARY_DIR) / "plugins";

    REQUIRE(std::filesystem::exists(plugins_dir));

    ExtensionLoader loader;
    ExtensionRegistry& reg = ExtensionRegistry::instance();

    std::size_t count = loader.load_all(plugins_dir, reg);
    CHECK(count >= 1);
    CHECK(reg.has_module("test_plugin"));

    reg.clear_modules();
    loader.unload_all();
}

TEST_CASE("ExtensionLoader: duplicate plugin id is rejected") {
    ExtensionRegistry::instance().clear_modules();

    const auto plugin_path =
        std::filesystem::path(CMAKE_CURRENT_BINARY_DIR) / "plugins" / "libchronon3d_test_plugin.so";

    REQUIRE(std::filesystem::exists(plugin_path));

    ExtensionLoader loader;
    ExtensionRegistry& reg = ExtensionRegistry::instance();

    // First load should succeed
    CHECK(loader.load(plugin_path, reg));
    CHECK(reg.has_module("test_plugin"));

    // Second load of the same plugin should be rejected (duplicate id)
    CHECK_FALSE(loader.load(plugin_path, reg));
    CHECK(loader.loaded_count() == 1); // still only 1

    // Check diagnostics for the failure
    const auto& diag = loader.diagnostics();
    bool found_failure = false;
    for (const auto& d : diag) {
        if (!d.success && d.id == "test_plugin") {
            CHECK_FALSE(d.error.empty());
            found_failure = true;
            break;
        }
    }
    CHECK(found_failure);

    reg.clear_modules();
    loader.unload_all();
}

TEST_CASE("ExtensionLoader: real plugin registers via initialize_all") {
    ExtensionRegistry::instance().clear_modules();

    const auto plugin_path =
        std::filesystem::path(CMAKE_CURRENT_BINARY_DIR) / "plugins" / "libchronon3d_test_plugin.so";

    REQUIRE(std::filesystem::exists(plugin_path));

    ExtensionLoader loader;
    ExtensionRegistry& reg = ExtensionRegistry::instance();

    // Load the plugin
    CHECK(loader.load(plugin_path, reg));

    // initialize_all is called inside load(), but verify the module is in the list
    auto ids = reg.module_ids();
    bool found = std::find(ids.begin(), ids.end(), "test_plugin") != ids.end();
    CHECK(found);

    reg.clear_modules();
    loader.unload_all();
}
