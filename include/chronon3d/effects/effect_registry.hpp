#pragma once

#include <chronon3d/effects/effect_descriptor.hpp>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::effects {

class EffectRegistry {
public:
    EffectRegistry();

    void register_effect(EffectDescriptor descriptor);

    [[nodiscard]] bool contains(std::string_view id) const;
    [[nodiscard]] const EffectDescriptor& get(std::string_view id) const;
    [[nodiscard]] std::vector<std::string> available() const;
    [[nodiscard]] std::vector<EffectDescriptor> list() const;

private:
    std::map<std::string, EffectDescriptor, std::less<>> m_effects;
};

} // namespace chronon3d::effects
