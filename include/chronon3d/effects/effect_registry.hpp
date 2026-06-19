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

/// Catalog of registered effects.  Create an instance, register builtins
/// plus any extensions, then freeze() it before handing it off to the
/// pipeline for query-only use.
class EffectCatalog {
public:
    EffectCatalog();

    void register_effect(EffectDescriptor descriptor);

    /// Prevent further registrations — call after builtins + extensions.
    void freeze();

    [[nodiscard]] bool contains(std::string_view id) const;
    [[nodiscard]] const EffectDescriptor* find(std::string_view id) const;
    [[nodiscard]] const EffectDescriptor& get(std::string_view id) const;
    [[nodiscard]] std::vector<std::string> available() const;
    [[nodiscard]] std::vector<EffectDescriptor> list() const;

    [[nodiscard]] std::unique_ptr<graph::RenderGraphNode> create_node(const EffectInstance& effect) const;

private:
    std::map<std::string, EffectDescriptor, std::less<>> m_effects;
    bool m_frozen{false};
};

/// Backward-compatible alias for migration.
using EffectRegistry = EffectCatalog;

} // namespace chronon3d::effects
