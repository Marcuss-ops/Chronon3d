#pragma once

#include <chronon3d/effects/effect_catalog_data.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace chronon3d::effects {

struct AnimatableEffectParam {
    std::string_view effect_id;
    std::string_view param;
    float minimum;
    float maximum;
};

inline constexpr auto kAnimatableEffectParams = std::to_array<AnimatableEffectParam>({
#define CHRONON_ANIM_PARAM(effect_id, params_type, member, minimum, maximum) \
    AnimatableEffectParam{effect_id, #member, minimum, maximum},
#include "effect_param_anims.def"
#undef CHRONON_ANIM_PARAM
});

[[nodiscard]] inline std::optional<EffectParams> make_default_animatable_effect_params(
    std::string_view effect_id) {
#define CHRONON_EFFECT(val, enum_name, params_type, id_str, ...) \
    if (effect_id == id_str && effect_id != ids::ColorCurves && \
        effect_id != ids::DistortMeshWarp) return EffectParams{::chronon3d::params_type{}};
#include "effect_catalog.def"
#undef CHRONON_EFFECT
    if (effect_id == ids::ColorCurves) {
        CurvesParams curves;
        curves.master = {{0.0f, 0.0f}, {1.0f, 1.0f}};
        return EffectParams{std::move(curves)};
    }
    if (effect_id == ids::DistortMeshWarp) {
        MeshWarpParams mesh;
        mesh.offsets.resize(4);
        return EffectParams{std::move(mesh)};
    }
    return std::nullopt;
}

[[nodiscard]] inline const AnimatableEffectParam* find_animatable_effect_param(
    std::string_view effect_id, std::string_view param) noexcept {
    const auto found = std::find_if(kAnimatableEffectParams.begin(), kAnimatableEffectParams.end(),
        [effect_id, param](const AnimatableEffectParam& row) {
            return row.effect_id == effect_id && row.param == param;
        });
    return found == kAnimatableEffectParams.end() ? nullptr : &*found;
}

[[nodiscard]] inline bool is_canonical_effect_id(std::string_view id) noexcept {
    for (const auto& entry : builtin_effect_catalog())
        if (entry.id == id) return true;
    return false;
}

template <class Params>
[[nodiscard]] bool set_animatable_effect_param(
    Params& params, std::string_view effect_id, std::string_view param, float sample) noexcept {
    bool applied = false;
#define CHRONON_ANIM_PARAM(row_id, params_type, member, minimum, maximum)             \
    if constexpr (std::is_same_v<std::remove_cvref_t<Params>, ::chronon3d::params_type>) { \
        if (effect_id == row_id && param == #member) {                                 \
            if constexpr (std::is_same_v<::chronon3d::params_type, ::chronon3d::CurvesParams>) { \
                if (!params.master.empty()) { params.master[0].y = sample; applied = true; } \
            } else {                                                                    \
                using Field = std::remove_cvref_t<decltype(params.member)>;              \
                if constexpr (std::is_integral_v<Field>)                                \
                    params.member = static_cast<Field>(std::round(sample));              \
                else params.member = static_cast<Field>(sample);                         \
                applied = true;                                                         \
            }                                                                           \
        }                                                                               \
    }
#include "effect_param_anims.def"
#undef CHRONON_ANIM_PARAM
    return applied;
}

template <class Params>
[[nodiscard]] std::optional<float> get_animatable_effect_param(
    const Params& params, std::string_view effect_id, std::string_view param) noexcept {
    std::optional<float> result;
#define CHRONON_ANIM_PARAM(row_id, params_type, member, minimum, maximum)              \
    if constexpr (std::is_same_v<std::remove_cvref_t<Params>, ::chronon3d::params_type>) { \
        if (effect_id == row_id && param == #member) {                                 \
            if constexpr (std::is_same_v<::chronon3d::params_type, ::chronon3d::CurvesParams>) { \
                if (!params.master.empty()) result = params.master[0].y;                \
            } else { result = static_cast<float>(params.member); }                     \
        }                                                                               \
    }
#include "effect_param_anims.def"
#undef CHRONON_ANIM_PARAM
    return result;
}

[[nodiscard]] inline std::optional<float> get_animatable_effect_param(
    const EffectParams& params, std::string_view effect_id, std::string_view param) noexcept {
    return std::visit([&](const auto& typed) -> std::optional<float> {
        using T = std::remove_cvref_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, std::monostate>) return std::nullopt;
        else return get_animatable_effect_param(typed, effect_id, param);
    }, params);
}

[[nodiscard]] inline bool set_animatable_effect_param(
    EffectParams& params, std::string_view effect_id, std::string_view param, float sample) noexcept {
    return std::visit([&](auto& typed) {
        using T = std::remove_cvref_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, std::monostate>) return false;
        else return set_animatable_effect_param(typed, effect_id, param, sample);
    }, params);
}

} // namespace chronon3d::effects
