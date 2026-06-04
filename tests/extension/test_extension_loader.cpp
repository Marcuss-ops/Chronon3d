#include <doctest/doctest.h>
#include <chronon3d/extension/extension_loader.hpp>
#include <chronon3d/extension/extension_registry.hpp>

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
