// ── Plugin whose create() function throws an exception ───────────────────
//
// Used to test that ExtensionLoader gracefully handles exceptions thrown
// during module construction.

#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>

#include <cstdint>
#include <stdexcept>

using namespace chronon3d;

namespace {

struct ChrononModuleDescriptor {
    std::uint32_t    api_version;
    const char*      id;
    ExtensionModule* (*create)();
};

// create() deliberately throws
// clang-format off
CHRONON_MODULE_EXPORT ChrononModuleDescriptor chronon3d_module = {
    .api_version = CHRONON_MODULE_API_VERSION,
    .id          = "throws_plugin",
    .create      = []() -> ExtensionModule* {
        throw std::runtime_error("intentional failure from create()");
    },
};
} // namespace
