#pragma once

#include <chronon3d/effects/effect_descriptor.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <cstdint>
#include <typeindex>
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
    Saturation,
    HueRotate,
    Invert,
    Vignette,
    FocusInLadder,
};

// ── effect_type_for<T> trait ────────────────────────────────────────────────
// Primary template returns Unknown.  Specializations for each concrete param
// type are defined in effect_params.hpp (included above).  Because the
// constructors below use a dependent name (effect_type_for<std::decay_t<Params>>),
// the specializations are found at template instantiation time.

template <typename T>
struct effect_type_for {
    static constexpr EffectType value = EffectType::Unknown;
};

// ── EffectInstance ──────────────────────────────────────────────────────────

struct EffectInstance {
    EffectDescriptor descriptor;
    EffectParams     params;
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
    // look up the correct EffectProcessor without relying on std::any.
    [[nodiscard]] std::type_index param_type_index() const {
        switch (effect_type) {
            case EffectType::Blur:        return typeid(BlurParams);
            case EffectType::Tint:        return typeid(TintParams);
            case EffectType::Brightness:  return typeid(BrightnessParams);
            case EffectType::Contrast:    return typeid(ContrastParams);
            case EffectType::DropShadow:  return typeid(DropShadowParams);
            case EffectType::Glow:        return typeid(GlowParams);
            case EffectType::Bloom:       return typeid(BloomParams);
            case EffectType::Fake3DWave:  return typeid(Fake3DWaveParams);
            case EffectType::Saturation:  return typeid(SaturationParams);
            case EffectType::HueRotate:   return typeid(HueRotateParams);
            case EffectType::Invert:      return typeid(InvertParams);
            case EffectType::Vignette:       return typeid(VignetteParams);
            case EffectType::FocusInLadder:  return typeid(FocusInLadderParams);
            default:                         return typeid(void);
        }
    }
};

// ── Runtime detection fallback ──────────────────────────────────────────────
// For cases where the concrete type is not known at compile time (e.g.
// deserialization).  Since EffectParams is now a variant, the "type" is
// simply the active alternative, determined by effect_type.

[[nodiscard]] inline EffectType detect_effect_type(const EffectParams& params) {
    switch (params.index()) {
        case 1:  return EffectType::Blur;
        case 2:  return EffectType::Tint;
        case 3:  return EffectType::Brightness;
        case 4:  return EffectType::Contrast;
        case 5:  return EffectType::DropShadow;
        case 6:  return EffectType::Glow;
        case 7:  return EffectType::Bloom;
        case 8:  return EffectType::Fake3DWave;
        case 9:  return EffectType::Saturation;
        case 10: return EffectType::HueRotate;
        case 11: return EffectType::Invert;
        case 12: return EffectType::Vignette;
        case 13: return EffectType::FocusInLadder;
        default: return EffectType::Unknown;
    }
}

} // namespace chronon3d::effects
