// ── Mini plugin with WRONG API version for ExtensionLoader tests ──────────
//
// This plugin uses API version 999 instead of CHRONON_MODULE_API_VERSION,
// which should be rejected by ExtensionLoader::load().

#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>

#include <cstdint>

using namespace chronon3d;

namespace {

class MismatchPluginModule final : public ExtensionModule {
public:
    std::string_view id() const override { return "mismatch_plugin"; }

    void register_with(ExtensionRegistry& registry) override {
        (void)registry;
    }
};

struct ChrononModuleDescriptor {
    std::uint32_t    api_version;
    const char*      id;
    ExtensionModule* (*create)();
};

// Intentional mismatch: API version 999 instead of CHRONON_MODULE_API_VERSION (1)
// clang-format off
CHRONON_MODULE_EXPORT ChrononModuleDescriptor chronon3d_module = {
    .api_version = 999,
    .id          = "mismatch_plugin",
    .create      = []() -> ExtensionModule* {
        return new MismatchPluginModule();
    },
};
} // namespace
