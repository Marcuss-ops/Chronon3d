#pragma once

#include <chronon3d/effects/effect_descriptor.hpp>
#include <any>
#include <utility>

namespace chronon3d::effects {

struct EffectInstance {
    EffectDescriptor descriptor;
    std::any         params;
    bool             enabled{true};

    EffectInstance() = default;

    template <typename Params>
    EffectInstance(EffectDescriptor desc, Params&& value)
        : descriptor(std::move(desc)), params(std::forward<Params>(value)) {}

    [[nodiscard]] bool has_params() const { return params.has_value(); }
};

} // namespace chronon3d::effects
