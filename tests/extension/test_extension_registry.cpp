#include <doctest/doctest.h>
#include <chronon3d/extension/extension_registry.hpp>

using namespace chronon3d;

namespace {

class TestExtModule : public ExtensionModule {
public:
    explicit TestExtModule(std::string_view id) : m_id(id) {}
    std::string_view id() const override { return m_id; }
    void register_with(ExtensionRegistry&) override { ++s_call_count; }
    static int s_call_count;
private:
    std::string m_id;
};

int TestExtModule::s_call_count = 0;

} // namespace

// ── ExtensionRegistry Contract ───────────────────────────────────────────────
//
// The registry MUST:
//   - Not register duplicate ids silently (registering a second module with
//     the same id should replace or log)
//   - Track module count correctly
//   - Provide access to domain registries
//   - Support initialize_all lifecycle

TEST_CASE("ExtensionRegistry: singleton instance exists") {
    auto& reg = ExtensionRegistry::instance();
    CHECK(reg.module_count() >= 0);
}

TEST_CASE("ExtensionRegistry: register_module increases count") {
    auto& reg = ExtensionRegistry::instance();
    auto before = reg.module_count();

    static int counter = 0;
    std::string id = "test_module_reg_" + std::to_string(++counter);

    reg.register_module(std::make_unique<TestExtModule>(id));
    CHECK(reg.module_count() == before + 1);
    CHECK(reg.has_module(id));
}

TEST_CASE("ExtensionRegistry: has_module returns false for unknown id") {
    auto& reg = ExtensionRegistry::instance();
    CHECK_FALSE(reg.has_module("nonexistent_module_xyz"));
}

TEST_CASE("ExtensionRegistry: module_ids returns all registered ids") {
    auto& reg = ExtensionRegistry::instance();

    static int counter = 0;
    std::string id = "module_ids_test_" + std::to_string(++counter);

    reg.register_module(std::make_unique<TestExtModule>(id));

    auto ids = reg.module_ids();
    bool found = std::find(ids.begin(), ids.end(), id) != ids.end();
    CHECK(found);
}

TEST_CASE("ExtensionRegistry: domain registry accessors return valid references") {
    auto& reg = ExtensionRegistry::instance();

    // These should not throw
    CHECK(&reg.effect_registry() != nullptr);
    CHECK(&reg.shape_registry() != nullptr);
    CHECK(&reg.source_registry() != nullptr);
    CHECK(&reg.sampler_registry() != nullptr);
    CHECK(&reg.graph_build_registry() != nullptr);
    CHECK(&reg.graph_node_registry() != nullptr);
}

TEST_CASE("ExtensionRegistry: initialize_all can be called multiple times") {
    auto& reg = ExtensionRegistry::instance();
    // Should not crash on subsequent calls
    reg.initialize_all();
    reg.initialize_all();
    MESSAGE("initialize_all called twice without issue");
}

TEST_CASE("ExtensionRegistry: register_module with null module throws") {
    auto& reg = ExtensionRegistry::instance();
    // The registry must reject null module pointers with an exception
    // rather than silently accepting or crashing.
    CHECK_THROWS_AS(reg.register_module(nullptr), std::invalid_argument);
}

TEST_CASE("ExtensionRegistry: module_count is consistent") {
    auto& reg = ExtensionRegistry::instance();
    CHECK(reg.module_count() == reg.module_ids().size());
}

TEST_CASE("ExtensionRegistry: clear_modules resets everything") {
    auto& reg = ExtensionRegistry::instance();
    reg.clear_modules();

    // After clear, there should be zero modules.
    CHECK(reg.module_count() == 0);
    CHECK(reg.module_ids().empty());
    CHECK_FALSE(reg.has_module("test_module_reg_"));

    // Domain registries remain accessible; singletons retain their builtins
    // while non-singleton registries (source, sampler) are recreated fresh.
    CHECK(&reg.effect_registry() != nullptr);
    CHECK(&reg.shape_registry() != nullptr);
    CHECK(&reg.source_registry() != nullptr);
    CHECK(&reg.sampler_registry() != nullptr);
    CHECK(&reg.graph_build_registry() != nullptr);
    CHECK(&reg.graph_node_registry() != nullptr);

    // After clearing, registering a new module should work.
    static int counter = 0;
    std::string id = "post_clear_test_" + std::to_string(++counter);
    reg.register_module(std::make_unique<TestExtModule>(id));
    CHECK(reg.module_count() == 1);
    CHECK(reg.has_module(id));

    // Clean up so subsequent tests start fresh.
    reg.clear_modules();
}

TEST_CASE("ExtensionRegistry: register_module rejects duplicate id") {
    auto& reg = ExtensionRegistry::instance();
    reg.clear_modules();

    std::string id = "duplicate_test_module";
    reg.register_module(std::make_unique<TestExtModule>(id));
    CHECK(reg.module_count() == 1);

    // Registering a module with the same id must throw.
    CHECK_THROWS_AS(
        reg.register_module(std::make_unique<TestExtModule>(id)),
        std::invalid_argument
    );
    // Module count must not change after the failed registration.
    CHECK(reg.module_count() == 1);

    reg.clear_modules();
}
