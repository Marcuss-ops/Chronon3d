#pragma once

#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/core/composition/composition_registration.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <map>

namespace chronon3d {

namespace effects {
    class EffectRegistry;
}
namespace registry {
    class ShapeRegistry;
    class SourceRegistry;
    class SamplerRegistry;
}
namespace graph {
    class GraphBuildRegistry;
    class GraphNodeRegistry;
}

/// Umbrella registry that owns `ExtensionModule` instances and provides
/// access to all domain-specific registries.
///
/// Singleton — use `ExtensionRegistry::instance()` to access.
///
/// Lifecycle:
/// 1. Register modules via `register_module()`.
/// 2. Call `initialize_all()` once (calls `register_with()` on each module).
/// 3. Access domain registries via accessor methods.
class ExtensionRegistry {
public:
    static ExtensionRegistry& instance();

    ExtensionRegistry();
    ~ExtensionRegistry();

    // ── Module management ──────────────────────────────────────────────

    /// Register a feature module.  Ownership is transferred.
    void register_module(std::unique_ptr<ExtensionModule> module);

    /// Initialize all registered modules (calls `register_with()` on each).
    /// Safe to call multiple times — modules are only initialized once.
    void initialize_all();

    /// Number of registered modules.
    [[nodiscard]] std::size_t module_count() const;

    /// Remove all modules from the registry.
    /// Used primarily in tests to reset state between test cases.
    void clear_modules();

    /// Check if a module with the given id is registered.
    [[nodiscard]] bool has_module(std::string_view id) const;

    /// Get the ids of all registered modules.
    [[nodiscard]] std::vector<std::string> module_ids() const;

    // ── Domain registry accessors ──────────────────────────────────────

    [[nodiscard]] effects::EffectRegistry& effect_registry();
    [[nodiscard]] registry::ShapeRegistry& shape_registry();
    [[nodiscard]] registry::SourceRegistry& source_registry();
    [[nodiscard]] registry::SamplerRegistry& sampler_registry();
    [[nodiscard]] graph::GraphBuildRegistry& graph_build_registry();
    [[nodiscard]] graph::GraphNodeRegistry& graph_node_registry();

    // ── Composition registration (from modules) ─────────────────────

    /// Register a composition factory.  Called by ExtensionModule::register_with().
    /// The factory is added to the builtin composition entries and will be
    /// available when a CompositionRegistry is next constructed.
    void register_composition(std::string_view id, detail::CompositionFactory factory);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace chronon3d
