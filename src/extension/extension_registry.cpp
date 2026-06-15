#include <chronon3d/extension/extension_registry.hpp>
#include <chronon3d/effects/effect_registry.hpp>
#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/registry/source_registry.hpp>
#include <chronon3d/registry/sampler_registry.hpp>
#include <chronon3d/render_graph/builder/graph_build_registry.hpp>
#include <chronon3d/render_graph/registry/graph_node_registry.hpp>
#include <chronon3d/core/composition/composition_registration.hpp>

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace chronon3d {

struct ExtensionRegistry::Impl {
    std::vector<std::unique_ptr<ExtensionModule>> modules;
    std::unordered_set<std::string> initialized_ids;
    bool initialized{false};

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

void ExtensionRegistry::register_module(std::unique_ptr<ExtensionModule> module) {
    if (!module) {
        throw std::invalid_argument("ExtensionRegistry::register_module: null module");
    }
    // Check for duplicate id.
    for (const auto& m : m_impl->modules) {
        if (m->id() == module->id()) {
            throw std::invalid_argument(
                std::string("ExtensionRegistry::register_module: duplicate id '") +
                std::string(module->id()) + "'");
        }
    }
    m_impl->modules.push_back(std::move(module));
}

void ExtensionRegistry::initialize_all() {
    // Lazily create registries that don't have a singleton.
    if (!m_impl->source_reg) {
        m_impl->source_reg  = std::make_unique<registry::SourceRegistry>();
        m_impl->sampler_reg = std::make_unique<registry::SamplerRegistry>();
    }

    for (auto& module : m_impl->modules) {
        const std::string id(module->id());
        if (m_impl->initialized_ids.count(id)) continue;
        module->register_with(*this);
        m_impl->initialized_ids.insert(id);
    }
    m_impl->initialized = true;
}

std::size_t ExtensionRegistry::module_count() const {
    return m_impl->modules.size();
}

void ExtensionRegistry::clear_modules() {
    m_impl->modules.clear();
    m_impl->initialized_ids.clear();
    m_impl->initialized = false;

    // Non-singleton registries (owned by ExtensionRegistry) are recreated
    // on next access, which re-registers their builtins automatically.
    m_impl->source_reg.reset();
    m_impl->sampler_reg.reset();

    // Reset external singleton registries so that re-initialization
    // starts from a truly clean state (no effects, shapes, graph
    // nodes, or build passes left over from a previous session).
    effects::EffectRegistry::instance().clear();
    registry::ShapeRegistry::instance().reset();  // clear + re-register builtins
    graph::GraphBuildRegistry::instance().clear();
    graph::GraphNodeRegistry::instance().clear();

    // Clear builtin composition factories registered by modules.
    detail::clear_builtin_compositions();
}

bool ExtensionRegistry::has_module(std::string_view id) const {
    return std::any_of(m_impl->modules.begin(), m_impl->modules.end(),
                       [id](const auto& m) { return m->id() == id; });
}

std::vector<std::string> ExtensionRegistry::module_ids() const {
    std::vector<std::string> ids;
    ids.reserve(m_impl->modules.size());
    for (const auto& m : m_impl->modules) {
        ids.emplace_back(m->id());
    }
    return ids;
}

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
