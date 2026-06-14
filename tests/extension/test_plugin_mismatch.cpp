// ── Mini plugin with WRONG API version for ExtensionLoader tests ──────────
//
// This plugin uses API version 999 instead of CHRONON_MODULE_API_VERSION,
// which should be rejected by ExtensionLoader::load().

#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>

#include <cstdint>

using namespace chronon3d;

// Descriptor struct — must match the layout expected by ExtensionLoader::load().
// The struct is NOT provided in a public header because it is an implementation
// detail of the dlopen/dlsym protocol.
struct ChrononModuleDescriptor {
    std::uint32_t    api_version;
    const char*      id;
    ExtensionModule* (*create)();
};

namespace {

class MismatchPluginModule final : public ExtensionModule {
public:
    std::string_view id() const override { return "mismatch_plugin"; }

    void register_with(ExtensionRegistry& registry) override {
        (void)registry;
    }
};

} // namespace

// Intentional mismatch: API version 999 instead of CHRONON_MODULE_API_VERSION (1)
// Use extern "C" block to avoid "initialized and declared extern" warning on GCC 15+.
// clang-format off
extern "C" {
__attribute__((visibility("default")))
ChrononModuleDescriptor chronon3d_module = {
    .api_version = 999,
    .id          = "mismatch_plugin",
    .create      = []() -> ExtensionModule* {
        return new MismatchPluginModule();
    },
};
}
