// ── Mini plugin shared library for real ExtensionLoader tests ──────────────
//
// This source file is compiled into a .so shared library during the test build.
// ExtensionLoader test cases use it to validate the real C ABI plugin contract.

#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>

#include <cstdint>

using namespace chronon3d;

namespace {

class TestPluginModule final : public ExtensionModule {
public:
    std::string_view id() const override { return "test_plugin"; }

    void register_with(ExtensionRegistry& registry) override {
        (void)registry;
    }
};

// Descriptor struct — must match the layout expected by ExtensionLoader::load().
// The struct is NOT provided in a public header because it is an implementation
// detail of the dlopen/dlsym protocol.  Plugin authors copy this struct into
// their own source file.
struct ChrononModuleDescriptor {
    std::uint32_t    api_version;
    const char*      id;
    ExtensionModule* (*create)();
};

// Plugin descriptor — must be exported as "chronon3d_module"
// to match the symbol name expected by ExtensionLoader::load().
// clang-format off
CHRONON_MODULE_EXPORT ChrononModuleDescriptor chronon3d_module = {
    .api_version = CHRONON_MODULE_API_VERSION,
    .id          = "test_plugin",
    .create      = []() -> ExtensionModule* {
        return new TestPluginModule();
    },
};
} // namespace
