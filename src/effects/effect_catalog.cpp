#include <chronon3d/effects/effect_catalog.hpp>

namespace chronon3d::effects {

// =============================================================================
// EffectCatalogEntry → EffectDescriptor conversion
// =============================================================================

EffectDescriptor EffectCatalogEntry::to_descriptor() const {
    EffectDescriptor desc;
    desc.id           = std::string{id};
    desc.display_name = std::string{display_name};
    desc.category     = category;
    desc.stage        = stage;
    desc.description  = std::string{display_name};
    desc.builtin      = true;
    desc.temporal     = temporal;
    // Factory is NOT set here — it is assigned by the caller (or by the
    // registry) to avoid coupling the core catalog to the render graph.
    return desc;
}

// =============================================================================
// builtin_effect_catalog — generated from X-macro
// =============================================================================

#define CHRONON_EFFECT(val, enum_name, params_type, id_str, display_name,   \
                       category, stage, domain, alpha, dirty, bounds,        \
                       temporal, fusible)                                    \
    EffectCatalogEntry {                                                     \
        EffectType::enum_name,                                               \
        id_str,                                                              \
        display_name,                                                        \
        category,                                                            \
        stage,                                                               \
        domain,                                                              \
        alpha,                                                               \
        dirty,                                                               \
        bounds,                                                              \
        temporal,                                                            \
        fusible,                                                             \
    },

static const EffectCatalogEntry k_builtin_effects[] = {
    #include <chronon3d/effects/effect_catalog.def>
};

#undef CHRONON_EFFECT

std::span<const EffectCatalogEntry> builtin_effect_catalog() noexcept {
    return k_builtin_effects;
}

} // namespace chronon3d::effects
