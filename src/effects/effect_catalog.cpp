#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/effects/effect_catalog_data.hpp>
#include <chronon3d/effects/effect_instance.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace chronon3d::effects {

namespace {

std::unique_ptr<graph::RenderGraphNode> generic_effect_factory(const EffectInstance& effect) {
    chronon3d::EffectStack stack;
    stack.push_back(effect);
    return std::make_unique<graph::EffectStackNode>(std::move(stack));
}

void register_builtin_effects(EffectCatalog& catalog) {
    for (const auto& entry : builtin_effect_catalog()) {
        EffectDescriptor desc = entry.to_descriptor();
        desc.factory = generic_effect_factory;
        catalog.register_effect(std::move(desc));
    }
}

} // namespace

EffectCatalog::EffectCatalog() {
    register_builtin_effects(*this);
}

void EffectCatalog::freeze() {
    m_frozen = true;
}

void EffectCatalog::register_effect(EffectDescriptor descriptor) {
    if (m_frozen) {
        throw std::runtime_error("EffectCatalog is frozen — cannot register new effects");
    }
    if (descriptor.id.empty()) {
        throw std::runtime_error("Effect id cannot be empty");
    }
    if (m_effects.contains(descriptor.id)) {
        throw std::runtime_error("Duplicate effect: " + descriptor.id);
    }
    if (descriptor.display_name.empty()) {
        descriptor.display_name = descriptor.id;
    }
    const std::string id = descriptor.id;
    m_effects.emplace(id, std::move(descriptor));
}

bool EffectCatalog::contains(std::string_view id) const {
    return m_effects.find(id) != m_effects.end();
}

const EffectDescriptor* EffectCatalog::find(std::string_view id) const {
    auto it = m_effects.find(id);
    return (it != m_effects.end()) ? &it->second : nullptr;
}

const EffectDescriptor& EffectCatalog::get(std::string_view id) const {
    auto it = m_effects.find(id);
    if (it == m_effects.end()) {
        throw std::runtime_error("Unknown effect: " + std::string{id});
    }
    return it->second;
}

std::vector<std::string> EffectCatalog::available() const {
    std::vector<std::string> ids;
    ids.reserve(m_effects.size());
    std::ranges::copy(m_effects | std::views::keys, std::back_inserter(ids));
    return ids;
}

std::vector<EffectDescriptor> EffectCatalog::list() const {
    std::vector<EffectDescriptor> descriptors;
    descriptors.reserve(m_effects.size());
    std::ranges::copy(m_effects | std::views::values, std::back_inserter(descriptors));
    return descriptors;
}

std::unique_ptr<graph::RenderGraphNode> EffectCatalog::create_node(const EffectInstance& effect) const {
    const auto& desc = get(effect.descriptor.id);
    if (desc.factory) {
        return desc.factory(effect);
    }
    return nullptr;
}

} // namespace chronon3d::effects
