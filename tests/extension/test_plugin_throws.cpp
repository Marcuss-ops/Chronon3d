// ── Plugin whose create() function throws an exception ───────────────────
//
// Used to test that ExtensionLoader gracefully handles exceptions thrown
// during module construction.

#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>

#include <cstdint>
#include <stdexcept>

using namespace chronon3d;

// Descriptor struct — must match the layout expected by ExtensionLoader::load().
// The struct is NOT provided in a public header because it is an implementation
// detail of the dlopen/dlsym protocol.
struct ChrononModuleDescriptor {
    std::uint32_t    api_version;
    const char*      id;
    ExtensionModule* (*create)();
};

// create() deliberately throws
// Use extern "C" block to avoid "initialized and declared extern" warning on GCC 15+.
// clang-format off
extern "C" {
__attribute__((visibility("default")))
ChrononModuleDescriptor chronon3d_module = {
    .api_version = CHRONON_MODULE_API_VERSION,
    .id          = "throws_plugin",
    .create      = []() -> ExtensionModule* {
        throw std::runtime_error("intentional failure from create()");
    },
};
}
