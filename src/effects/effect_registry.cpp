#include <chronon3d/effects/effect_registry.hpp>
#include <chronon3d/effects/effect_instance.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <stdexcept>
#include <utility>

namespace chronon3d::effects {

namespace {

std::unique_ptr<graph::RenderGraphNode> generic_effect_factory(const EffectInstance& effect) {
    chronon3d::EffectStack stack;
    stack.push_back(effect);
    return std::make_unique<graph::EffectStackNode>(std::move(stack));
}

void register_builtin_effects(EffectRegistry& registry) {
    registry.register_effect(EffectDescriptor{
        .id = "blur.gaussian",
        .display_name = "Gaussian Blur",
        .category = EffectCategory::Blur,
        .stage = EffectStage::LayerPostTransform,
        .description = "Soft blur for layer content",
        .builtin = true,
        .temporal = false,
        .factory = generic_effect_factory
    });

    registry.register_effect(EffectDescriptor{
        .id = "color.tint",
        .display_name = "Tint",
        .category = EffectCategory::Color,
        .stage = EffectStage::Adjustment,
        .description = "Blend layer content toward a target color",
        .builtin = true,
        .temporal = false,
        .factory = generic_effect_factory
    });

    registry.register_effect(EffectDescriptor{
        .id = "color.brightness",
        .display_name = "Brightness",
        .category = EffectCategory::Color,
        .stage = EffectStage::Adjustment,
        .description = "Shift layer luminance",
        .builtin = true,
        .temporal = false,
        .factory = generic_effect_factory
    });

    registry.register_effect(EffectDescriptor{
        .id = "color.contrast",
        .display_name = "Contrast",
        .category = EffectCategory::Color,
        .stage = EffectStage::Adjustment,
        .description = "Scale layer contrast around mid-gray",
        .builtin = true,
        .temporal = false,
        .factory = generic_effect_factory
    });

    registry.register_effect(EffectDescriptor{
        .id = "light.drop_shadow",
        .display_name = "Drop Shadow",
        .category = EffectCategory::Light,
        .stage = EffectStage::LayerPostTransform,
        .description = "Project a soft shadow behind the layer",
        .builtin = true,
        .temporal = false,
        .factory = generic_effect_factory
    });

    registry.register_effect(EffectDescriptor{
        .id = "light.glow",
        .display_name = "Glow",
        .category = EffectCategory::Light,
        .stage = EffectStage::LayerPostTransform,
        .description = "Add halo-style glow around content",
        .builtin = true,
        .temporal = false,
        .factory = generic_effect_factory
    });
}

} // namespace

EffectRegistry& EffectRegistry::instance() {
    static EffectRegistry registry;
    return registry;
}

EffectRegistry::EffectRegistry() {
    register_builtin_effects(*this);
}

void EffectRegistry::register_effect(EffectDescriptor descriptor) {
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

bool EffectRegistry::contains(std::string_view id) const {
    return m_effects.find(id) != m_effects.end();
}

const EffectDescriptor& EffectRegistry::get(std::string_view id) const {
    auto it = m_effects.find(id);
    if (it == m_effects.end()) {
        throw std::runtime_error("Unknown effect: " + std::string{id});
    }
    return it->second;
}

std::vector<std::string> EffectRegistry::available() const {
    std::vector<std::string> ids;
    ids.reserve(m_effects.size());
    for (const auto& [id, _] : m_effects) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<EffectDescriptor> EffectRegistry::list() const {
    std::vector<EffectDescriptor> descriptors;
    descriptors.reserve(m_effects.size());
    for (const auto& [_, descriptor] : m_effects) {
        descriptors.push_back(descriptor);
    }
    return descriptors;
}

std::unique_ptr<graph::RenderGraphNode> EffectRegistry::create_node(const EffectInstance& effect) const {
    const auto& desc = get(effect.descriptor.id);
    if (desc.factory) {
        return desc.factory(effect);
    }
    return nullptr;
}

} // namespace chronon3d::effects
