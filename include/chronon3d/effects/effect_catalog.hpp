#pragma once

// ── Effect Catalog ──────────────────────────────────────────────────────────
//
// Central registry of all built-in effects, generated from the X-macro
// effect_catalog.def file.
//
// This single header provides:
//   1. effect_type_for<T> specialisations   (params → EffectType)
//   2. effect_params_for<E> specialisations (EffectType → params type)
//   3. effect_type_v<T> variable template
//   4. detect_effect_type() via std::visit  (replaces fragile params.index())
//   5. effect_param_type_index() via std::visit (replaces manual switch)
//   6. EffectCatalogEntry struct            (static metadata per effect)
//   7. builtin_effect_catalog()             (span of all catalog entries)
//
// Usage: add a new effect by adding one CHRONON_EFFECT(...) line to
// effect_catalog.def — no other mapping changes needed.

#include <chronon3d/effects/effect_type.hpp>
#include <chronon3d/effects/effect_traits.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/effects/effect_descriptor.hpp>
#include <chronon3d/effects/effect_category.hpp>
#include <chronon3d/effects/effect_stage.hpp>
#include <chronon3d/effects/effect_ids.hpp>
#include <variant>
#include <typeindex>
#include <span>
#include <string_view>
#include <utility>

namespace chronon3d::effects {

// =============================================================================
// 1. effect_type_for<T> specialisations — generated from X-macro
// =============================================================================

#define CHRONON_EFFECT(val, enum_name, params_type, id_str, display_name,   \
                       category, stage, domain, alpha, dirty, bounds,        \
                       temporal, fusible)                                    \
    template <>                                                              \
    struct effect_type_for<::chronon3d::params_type> {                       \
        static constexpr EffectType value = EffectType::enum_name;           \
    };

#include "effect_catalog.def"
#undef CHRONON_EFFECT

// Convenience variable template
template <class Params>
inline constexpr EffectType effect_type_v =
    effect_type_for<std::remove_cvref_t<Params>>::value;

// =============================================================================
// 2. effect_params_for<E> specialisations — generated from X-macro
// =============================================================================

template <EffectType E>
struct effect_params_for;

#define CHRONON_EFFECT(val, enum_name, params_type, id_str, display_name,   \
                       category, stage, domain, alpha, dirty, bounds,        \
                       temporal, fusible)                                    \
    template <>                                                              \
    struct effect_params_for<EffectType::enum_name> {                        \
        using type = ::chronon3d::params_type;                               \
    };

#include "effect_catalog.def"
#undef CHRONON_EFFECT

// =============================================================================
// 3. detect_effect_type — standard visitor, NOT variant.index()
// =============================================================================

/// Runtime EffectType detection via std::visit.
/// Replaces the fragile `switch (params.index())` approach.
[[nodiscard]] inline EffectType detect_effect_type(const EffectParams& params) {
    return std::visit([](const auto& value) -> EffectType {
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return EffectType::Unknown;
        } else {
            return effect_type_v<T>;
        }
    }, params);
}

// =============================================================================
// 4. effect_param_type_index — std::type_index via visitor
// =============================================================================

/// Returns std::type_index for the active variant alternative.
/// Replaces the manual switch in EffectInstance::param_type_index().
[[nodiscard]] inline std::type_index effect_param_type_index(const EffectParams& params) {
    return std::visit([](const auto& value) -> std::type_index {
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return typeid(void);
        } else {
            return typeid(T);
        }
    }, params);
}

// =============================================================================
// 5. EffectCatalogEntry — static metadata for each effect type
// =============================================================================

struct EffectCatalogEntry {
    EffectType      type;
    std::string_view id;
    std::string_view display_name;
    EffectCategory  category;
    EffectStage     stage;
    EffectDomain    domain;
    AlphaPolicy     alpha_policy;
    DirtyPolicy     dirty_policy;
    BoundsPolicy    bounds_policy;
    bool            temporal;
    bool            fusible_color;

    /// Convert to an EffectDescriptor suitable for registration.
    [[nodiscard]] EffectDescriptor to_descriptor() const;
};

/// Returns a span of all built-in catalog entries.
/// The registry calls this to populate itself automatically.
[[nodiscard]] std::span<const EffectCatalogEntry> builtin_effect_catalog() noexcept;

} // namespace chronon3d::effects
