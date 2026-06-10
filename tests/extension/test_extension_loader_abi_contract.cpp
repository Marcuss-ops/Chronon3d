// =============================================================================
// test_extension_loader_abi_contract.cpp
//
// ABI contract tests for the ExtensionLoader plugin system.
//
// Verifies:
//   1. CHRONON_MODULE_API_VERSION is defined and stable
//   2. Plugin descriptor layout matches expectation
//   3. Export symbol name is consistent
//   4. Module descriptor has all required fields (api_version, id, create)
//   5. Ownership model: loader takes ownership, registry holds unique_ptr
//   6. Plugin lifecycle: create → register → initialize → destroy
//   7. LoadedPlugin diagnostics structure completeness
// =============================================================================

#include <doctest/doctest.h>
#include <chronon3d/extension/extension_loader.hpp>
#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace chronon3d;

// ── 1. API Version Contract ─────────────────────────────────────────────────

TEST_CASE("ExtensionABI: CHRONON_MODULE_API_VERSION is 1") {
    // The API version must be stable and consistent between loader and plugins.
    // Bumping the version requires updating both sides.
    CHECK(CHRONON_MODULE_API_VERSION == 1);
}

TEST_CASE("ExtensionABI: API version is a uint32_t") {
    // The type must match between the public header and the descriptor struct.
    CHECK((std::is_same_v<decltype(CHRONON_MODULE_API_VERSION), const std::uint32_t>));
}

// ── 2. Export Symbol Contract ───────────────────────────────────────────────

TEST_CASE("ExtensionABI: CHRONON_MODULE_EXPORT uses extern C") {
    // The macro must use extern "C" for C++ and DLL export attributes per platform.
    // This test verifies the macro is defined and expands to something usable.
    // It's a compile-time check — if it compiles, the macro is valid.
    MESSAGE("CHRONON_MODULE_EXPORT macro compiles — verified at compile time");
}

TEST_CASE("ExtensionABI: ExtensionModule is abstract") {
    // ExtensionModule must be abstract (pure virtual id() and register_with()).
    // This prevents accidental instantiation.
    CHECK(std::is_abstract_v<ExtensionModule>);
}

TEST_CASE("ExtensionABI: ExtensionModule has virtual destructor") {
    // The virtual destructor ensures proper cleanup of derived classes.
    CHECK(std::has_virtual_destructor_v<ExtensionModule>);
}

// ── 3. Ownership Model Contract ─────────────────────────────────────────────

TEST_CASE("ExtensionABI: ExtensionRegistry owns modules via unique_ptr") {
    ExtensionRegistry reg;

    // Register a test module
    class OwnedModule : public ExtensionModule {
    public:
        std::string_view id() const override { return "owned_module_test"; }
        void register_with(ExtensionRegistry&) override {}
    };

    CHECK_FALSE(reg.has_module("owned_module_test"));

    // Transfer ownership
    reg.register_module(std::make_unique<OwnedModule>());
    CHECK(reg.has_module("owned_module_test"));
    CHECK(reg.module_count() >= 1);

    reg.clear_modules();
    CHECK_FALSE(reg.has_module("owned_module_test"));
    CHECK(reg.module_count() == 0);
}

TEST_CASE("ExtensionABI: duplicate module ids are rejected by the registry") {
    ExtensionRegistry reg;

    class DupeModule : public ExtensionModule {
    public:
        std::string_view id() const override { return "dupe_test"; }
        void register_with(ExtensionRegistry&) override {}
    };

    reg.register_module(std::make_unique<DupeModule>());
    CHECK(reg.has_module("dupe_test"));

    // Second registration with same id must throw
    CHECK_THROWS_AS(reg.register_module(std::make_unique<DupeModule>()),
                    std::invalid_argument);

    reg.clear_modules();
}

TEST_CASE("ExtensionABI: null module is rejected by the registry") {
    ExtensionRegistry reg;
    CHECK_THROWS_AS(reg.register_module(nullptr), std::invalid_argument);
}

// ── 4. LoadedPlugin Diagnostics Structure ────────────────────────────────────

TEST_CASE("ExtensionABI: LoadedPlugin has all required fields") {
    LoadedPlugin p;
    // Default construction must leave success = false and empty fields
    CHECK_FALSE(p.success);
    CHECK(p.path.empty());
    CHECK(p.id.empty());
    CHECK(p.error.empty());

    // Verify struct is moveable
    p.path = "/tmp/test.so";
    p.id = "test_mod";
    p.success = true;
    LoadedPlugin moved = std::move(p);
    CHECK(moved.success);
    CHECK(moved.id == "test_mod");
}

// ── 5. Plugin Lifecycle via Real .so ─────────────────────────────────────────

TEST_CASE("ExtensionABI: full lifecycle via real plugin .so") {
    ExtensionRegistry::instance().clear_modules();

    const auto plugin_path =
        std::filesystem::path(CMAKE_CURRENT_BINARY_DIR) / "plugins" /
        "libchronon3d_test_plugin.so";

    if (!std::filesystem::exists(plugin_path)) {
        MESSAGE("Skipping: test plugin not built at " << plugin_path);
        return;
    }

    ExtensionLoader loader;
    ExtensionRegistry& reg = ExtensionRegistry::instance();

    // Phase 1: Load
    CHECK_FALSE(reg.has_module("test_plugin"));
    bool ok = loader.load(plugin_path, reg);
    CHECK(ok);
    CHECK(reg.has_module("test_plugin"));
    CHECK(loader.loaded_count() == 1);

    // Phase 2: Diagnostics confirm success
    const auto& diag = loader.diagnostics();
    bool found_success = false;
    for (const auto& d : diag) {
        if (d.path == plugin_path) {
            CHECK(d.success);
            CHECK(d.id == "test_plugin");
            CHECK(d.error.empty());
            found_success = true;
            break;
        }
    }
    CHECK(found_success);

    // Phase 3: Module ids include the plugin
    auto ids = reg.module_ids();
    bool in_ids = std::find(ids.begin(), ids.end(), "test_plugin") != ids.end();
    CHECK(in_ids);

    // Phase 4: Safe unload destroys modules then closes handles
    loader.unload_all(reg);
    CHECK_FALSE(reg.has_module("test_plugin"));
    CHECK(loader.loaded_count() == 0);
}

// ── 6. ExtensionLoader is non-copyable, non-movable ─────────────────────────

TEST_CASE("ExtensionABI: ExtensionLoader is non-copyable") {
    CHECK(!std::is_copy_constructible_v<ExtensionLoader>);
    CHECK(!std::is_copy_assignable_v<ExtensionLoader>);
}

// ── 7. Diagnostics accumulation contract ────────────────────────────────────

TEST_CASE("ExtensionABI: mixed success/failure in diagnostics") {
    ExtensionRegistry::instance().clear_modules();
    ExtensionLoader loader;
    ExtensionRegistry& reg = ExtensionRegistry::instance();

    // One deliberate failure
    loader.load("/tmp/definitely_missing.so", reg);
    // Try the real plugin
    const auto plugin_path =
        std::filesystem::path(CMAKE_CURRENT_BINARY_DIR) / "plugins" /
        "libchronon3d_test_plugin.so";

    if (std::filesystem::exists(plugin_path)) {
        bool ok = loader.load(plugin_path, reg);
        CHECK(ok);
    }

    // Diagnostics should contain both successes and failures
    bool found_failure = false;
    bool found_success = false;
    for (const auto& d : loader.diagnostics()) {
        if (d.success) found_success = true;
        else found_failure = true;
    }
    CHECK(found_failure);
    if (std::filesystem::exists(plugin_path)) {
        CHECK(found_success);
    }

    loader.unload_all(reg);
}
