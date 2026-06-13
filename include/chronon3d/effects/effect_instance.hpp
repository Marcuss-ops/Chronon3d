#pragma once

#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/effects/effect_descriptor.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/effects/effect_type.hpp>
#include <cstdint>
#include <typeindex>
#include <utility>
#include <type_traits>

namespace chronon3d::effects {

// ── effect_type_for<T> primary template ────────────────────────────────────
// (defined in effect_type.hpp, specializations generated in effect_catalog.hpp)
// Primary template returns Unknown.

// ── EffectInstance ──────────────────────────────────────────────────────────

struct EffectInstance {
    EffectDescriptor descriptor;
    ::chronon3d::EffectParams params;
    EffectType       effect_type{EffectType::Unknown};
    bool             enabled{true};

    EffectInstance() = default;

    // Standard constructor — auto-detects effect_type from the param type
    template <typename Params>
    EffectInstance(EffectDescriptor desc, Params&& value)
        : descriptor(std::move(desc))
        , params(std::forward<Params>(value))
        , effect_type(effect_type_for<std::decay_t<Params>>::value) {}

    // Convenience constructor for legacy/inline effects — auto-detects effect_type
    template <typename Params, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Params>, EffectInstance>>>
    EffectInstance(Params&& value)
        : params(std::forward<Params>(value))
        , effect_type(effect_type_for<std::decay_t<Params>>::value) {}

    [[nodiscard]] bool has_params() const {
        return !std::holds_alternative<std::monostate>(params);
    }

    // Returns std::type_index for the active variant alternative, so the
    // software registry — which indexes processors by type_index — can
    // look up the correct EffectProcessor.
    // Uses std::visit — no manual switch on variant.index().
    [[nodiscard]] std::type_index param_type_index() const {
        return effect_param_type_index(params);
    }
};

// ── Runtime detection via std::visit (defined in effect_catalog.hpp) ──────
// detect_effect_type() uses std::visit instead of params.index() — see
// effect_catalog.hpp for the implementation.

} // namespace chronon3d::effects
