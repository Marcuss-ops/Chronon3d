#pragma once

#include <chronon3d/effects/effect_descriptor.hpp>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

namespace chronon3d::graph {
    class RenderGraphNode;
}

namespace chronon3d::effects {

struct EffectInstance;

class EffectRegistry {
public:
    static EffectRegistry& instance();

    EffectRegistry();

    void register_effect(EffectDescriptor descriptor);

    [[nodiscard]] bool contains(std::string_view id) const;
    [[nodiscard]] const EffectDescriptor& get(std::string_view id) const;
    [[nodiscard]] std::vector<std::string> available() const;
    [[nodiscard]] std::vector<EffectDescriptor> list() const;

    [[nodiscard]] std::unique_ptr<graph::RenderGraphNode> create_node(const EffectInstance& effect) const;

    /// Clear all registered effects (used in tests for clean reset).
    void clear();

private:
    std::map<std::string, EffectDescriptor, std::less<>> m_effects;
};

} // namespace chronon3d::effects
