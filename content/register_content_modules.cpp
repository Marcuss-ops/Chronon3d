// Content registration — table-driven per-domain registration.
//
// Each domain (minimalist, special_names, etc.) owns a register_*_compositions()
// function that takes a CompositionRegistry& and calls registry.add() directly.
// This file is the single call-site that orchestrates all domains.
//
#include "register_content_modules.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_catalog.hpp>
#include <chronon3d/extension/extension_context.hpp>

// ── Per-domain registration headers ──────────────────────────────────────────
// Each domain file exports one register_*_compositions(CompositionRegistry&) function.
namespace chronon3d::content::minimalist   { void register_minimalist_compositions(CompositionRegistry&); }
namespace chronon3d::content::special_names { void register_special_name_compositions(CompositionRegistry&); }
namespace chronon3d::content::important_words { void register_important_word_compositions(CompositionRegistry&); }
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
namespace chronon3d::content::shapes       { void register_shape_compositions(CompositionRegistry&); }
namespace chronon3d::content::images       { void register_image_compositions(CompositionRegistry&); }
#endif
namespace chronon3d::content::anims        { void register_anim_compositions(CompositionRegistry&); }
namespace chronon3d::content::effects      { void register_effect_compositions(CompositionRegistry&); }
namespace chronon3d::content::grid         { void register_grid_compositions(CompositionRegistry&); }
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
namespace chronon3d::content::two_point_five_d { void register_2d5_compositions(CompositionRegistry&); }
#endif
namespace chronon3d::content::backgrounds  { void register_grid_clean_background(CompositionRegistry&); }
namespace chronon3d::content::ae_parity    { void register_ae_parity_compositions(CompositionRegistry&); }

namespace chronon3d {

namespace {

/// ExtensionModule wrapping all content-domain registration.
class ContentExtension final : public ExtensionModule {
public:
    [[nodiscard]] std::string_view name() const override { return "content"; }

    void register_all(ExtensionContext& ctx) override {
        content::minimalist::register_minimalist_compositions(ctx.compositions);
        content::special_names::register_special_name_compositions(ctx.compositions);
        content::important_words::register_important_word_compositions(ctx.compositions);
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
        content::shapes::register_shape_compositions(ctx.compositions);
        content::images::register_image_compositions(ctx.compositions);
#endif
        content::anims::register_anim_compositions(ctx.compositions);
        content::effects::register_effect_compositions(ctx.compositions);
        content::grid::register_grid_compositions(ctx.compositions);
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
        content::two_point_five_d::register_2d5_compositions(ctx.compositions);
#endif
        content::backgrounds::register_grid_clean_background(ctx.compositions);
        content::ae_parity::register_ae_parity_compositions(ctx.compositions);
    }
};

} // namespace

void register_content_modules(ExtensionCatalog& catalog,
                               ExtensionContext& ctx) {
    if (!catalog.contains("content")) {
        catalog.register_module(std::make_unique<ContentExtension>());
    }
    catalog.register_all(ctx);
}

} // namespace chronon3d
