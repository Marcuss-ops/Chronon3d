#pragma once

#include <chronon3d/core/composition/composition_registration.hpp>

#include <memory>
#include <string>
#include <string_view>

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

/// Umbrella registry providing access to all domain-specific registries.
///
/// Singleton — use `ExtensionRegistry::instance()` to access.
class ExtensionRegistry {
public:
    static ExtensionRegistry& instance();

    ExtensionRegistry();
    ~ExtensionRegistry();

    // ── Domain registry accessors ──────────────────────────────────────

    [[nodiscard]] effects::EffectRegistry& effect_registry();
    [[nodiscard]] registry::ShapeRegistry& shape_registry();
    [[nodiscard]] registry::SourceRegistry& source_registry();
    [[nodiscard]] registry::SamplerRegistry& sampler_registry();
    [[nodiscard]] graph::GraphBuildRegistry& graph_build_registry();
    [[nodiscard]] graph::GraphNodeRegistry& graph_node_registry();

    // ── Composition registration ───────────────────────────────────────

    /// Register a composition factory.  The factory is added to the builtin
    /// composition entries and will be available when a CompositionRegistry
    /// is next constructed.
    void register_composition(std::string_view id, detail::CompositionFactory factory);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace chronon3d
