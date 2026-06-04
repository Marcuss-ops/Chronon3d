#pragma once

#include <cstdint>
#include <string_view>

// ── Plugin export macro ──────────────────────────────────────────────────────
// Plugin shared libraries must export a descriptor struct using this macro.
//
// Example:
//   CHRONON_MODULE_EXPORT {
//       .api_version = CHRONON_MODULE_API_VERSION,
//       .id         = "my_plugin",
//       .create     = []() -> std::unique_ptr<ExtensionModule> {
//           return std::make_unique<MyPluginModule>();
//       },
//   };
//
#ifdef _WIN32
  #define CHRONON_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
  #define CHRONON_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

/// API version baked into plugin descriptors.  The loader rejects plugins
/// compiled against a different major version.
static constexpr std::uint32_t CHRONON_MODULE_API_VERSION = 1;

namespace chronon3d {

class ExtensionRegistry;

/// Abstract interface for a self-contained feature module.
///
/// Each module registers what it offers (presets, effects, nodes, passes,
/// assets, exporters, CLI commands, etc.) into the appropriate domain
/// registries via the `ExtensionRegistry` umbrella.
///
/// Usage:
/// ```cpp
/// class MyFeatureModule : public ExtensionModule {
/// public:
///     std::string_view id() const override { return "my_feature"; }
///     void register_with(ExtensionRegistry& registry) override;
/// };
///
/// // In main / init:
/// ExtensionRegistry::instance().register_module(std::make_unique<MyFeatureModule>());
/// ExtensionRegistry::instance().initialize_all();
/// ```
class ExtensionModule {
public:
    virtual ~ExtensionModule() = default;

    /// Unique identifier for this module (e.g. "minimalist", "2d5_motion", "text").
    [[nodiscard]] virtual std::string_view id() const = 0;

    /// Called once during initialization.  The module should register its
    /// offerings into the domain-specific registries available through
    /// the `ExtensionRegistry`.
    virtual void register_with(ExtensionRegistry& registry) = 0;
};

} // namespace chronon3d
