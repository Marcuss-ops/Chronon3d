#include <chronon3d/extension/extension_registry.hpp>
#include <chronon3d/effects/effect_registry.hpp>
#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/registry/source_registry.hpp>
#include <chronon3d/registry/sampler_registry.hpp>
#include <chronon3d/render_graph/builder/graph_build_registry.hpp>
#include <chronon3d/render_graph/registry/graph_node_registry.hpp>
#include <chronon3d/core/composition/composition_registration.hpp>

#include <memory>

namespace chronon3d {

struct ExtensionRegistry::Impl {
    // Registries that have no singleton (constructed and owned here).
    std::unique_ptr<registry::SourceRegistry>  source_reg;
    std::unique_ptr<registry::SamplerRegistry> sampler_reg;
};

ExtensionRegistry& ExtensionRegistry::instance() {
    static ExtensionRegistry s_instance;
    return s_instance;
}

ExtensionRegistry::ExtensionRegistry()
    : m_impl(std::make_unique<Impl>())
{
}

ExtensionRegistry::~ExtensionRegistry() = default;

// ── Domain registry accessors ──────────────────────────────────────────

effects::EffectRegistry& ExtensionRegistry::effect_registry() {
    return effects::EffectRegistry::instance();
}

registry::ShapeRegistry& ExtensionRegistry::shape_registry() {
    return registry::ShapeRegistry::instance();
}

registry::SourceRegistry& ExtensionRegistry::source_registry() {
    if (!m_impl->source_reg) {
        m_impl->source_reg  = std::make_unique<registry::SourceRegistry>();
    }
    return *m_impl->source_reg;
}

registry::SamplerRegistry& ExtensionRegistry::sampler_registry() {
    if (!m_impl->sampler_reg) {
        m_impl->sampler_reg = std::make_unique<registry::SamplerRegistry>();
    }
    return *m_impl->sampler_reg;
}

graph::GraphBuildRegistry& ExtensionRegistry::graph_build_registry() {
    return graph::GraphBuildRegistry::instance();
}

graph::GraphNodeRegistry& ExtensionRegistry::graph_node_registry() {
    return graph::GraphNodeRegistry::instance();
}

void ExtensionRegistry::register_composition(std::string_view id, detail::CompositionFactory factory) {
    detail::add_builtin_composition(id, std::move(factory));
}

} // namespace chronon3d
