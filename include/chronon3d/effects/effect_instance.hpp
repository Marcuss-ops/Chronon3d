#pragma once

#include <chronon3d/effects/effect_descriptor.hpp>
#include <any>
#include <cstdint>
#include <utility>
#include <type_traits>

namespace chronon3d::effects {

// ── EffectType enum ─────────────────────────────────────────────────────────
// Maps each concrete param struct to a compile-time tag for O(1) dispatch
// instead of an O(n) std::any_cast chain.

enum class EffectType : uint8_t {
    Unknown = 0,
    Blur,
    Tint,
    Brightness,
    Contrast,
    DropShadow,
    Glow,
    Bloom,
    Fake3DWave,
};

// ── effect_type_for<T> trait ────────────────────────────────────────────────
// Primary template returns Unknown.  Specializations for each concrete param
// type are defined in effect_stack.hpp (which includes this header and defines
// the param structs).  Because the constructors below use a dependent name
// (effect_type_for<std::decay_t<Params>>), the specializations are found at
// template instantiation time — no circular include needed.

template <typename T>
struct effect_type_for {
    static constexpr EffectType value = EffectType::Unknown;
};

// ── EffectInstance ──────────────────────────────────────────────────────────

struct EffectInstance {
    EffectDescriptor descriptor;
    std::any         params;
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

    [[nodiscard]] bool has_params() const { return params.has_value(); }
};

// ── Runtime detection fallback ──────────────────────────────────────────────
// For cases where the concrete type is not known at compile time (e.g.
// deserialization).  Declared here, defined in effect_stack.hpp where the
// param structs are available.

[[nodiscard]] EffectType detect_effect_type(const std::any& params);

} // namespace chronon3d::effects
