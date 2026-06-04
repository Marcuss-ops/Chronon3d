#pragma once

#include <string_view>

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
