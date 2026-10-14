#pragma once

// Effect stack: ordered list of post-processing effects applied to a layer.
// Order is preserved: blur then tint != tint then blur.
//
// Parameter structs (BlurParams, GlowParams, etc.) and the EffectParams variant
// type are now in <chronon3d/effects/effect_params.hpp>.
//
// effect_type_for<T> specialisations are generated automatically by the
// X-macro catalog in <chronon3d/effects/effect_catalog_data.hpp>.

#include <chronon3d/effects/effect_instance.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/effects/presets/glow_presets.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <algorithm>
#include <variant>
#include <vector>

namespace chronon3d {

// Unify with the modular effects system
using EffectInstance = effects::EffectInstance;

// ── EffectStack struct (forward-declarable wrapper around std::vector) ─────
// Previously a "using" alias, now a proper struct so layer.hpp can break its
// #include dependency on the heavy effects headers.  Default copy/move/assign
// are implicitly generated from the vector member.
struct EffectStack {
private:
    friend struct Layer;
    std::vector<EffectInstance> instances;

public:

    // ── container protocol ─────────────────────────────────────────────
    auto        begin()          { return instances.begin(); }
    auto        begin()    const { return instances.begin(); }
    auto        end()            { return instances.end();   }
    auto        end()      const { return instances.end();   }
    auto        size()     const { return instances.size();  }
    bool        empty()    const { return instances.empty(); }
    EffectInstance& operator[](std::size_t i)       { return instances[i]; }
    const EffectInstance& operator[](std::size_t i) const { return instances[i]; }
    void        push_back(EffectInstance e)           { instances.push_back(std::move(e)); }

    // Range-insert for iterator pairs
    template <typename It>
    auto insert(decltype(instances)::const_iterator pos, It first, It last) {
        return instances.insert(pos, first, last);
    }
    // emplace_back forwarding
    template <typename... Args>
    auto& emplace_back(Args&&... args) {
        return instances.emplace_back(std::forward<Args>(args)...);
    }
    void clear() { instances.clear(); }
};

// ── Temporal-effect predicate (canonical, single sede) ─────────────────────
// An enabled effect whose output varies per frame by contract: Echo reads
// history frames, animated Noise samples (seed, frame). Any layer carrying
// such an effect is never pixel-static, even with frozen transforms and
// topology — frame-invariant cache keys or surface reuse would freeze one
// frame's pixels for all frames. All static/dynamic classifiers
// (SceneHasher, builder static analysis) and cache-policy decisions
// (EffectStackNode factory/policy, optimizer merge guard) must consult
// this predicate; keep them in sync when a new temporal effect appears.
[[nodiscard]] inline bool effect_stack_has_temporal_effect(const EffectStack& stack) noexcept {
    for (const auto& effect : stack) {
        if (!effect.enabled) continue;
        if (effect.effect_type == effects::EffectType::Echo) return true;
        if (effect.effect_type == effects::EffectType::Noise) {
            const auto* noise = std::get_if<NoiseParams>(&effect.params);
            if (noise != nullptr && noise->animated) return true;
        }
    }
    return false;
}

} // namespace chronon3d
