// Content registration — table-driven per-domain registration.
//
// Each domain (minimalist, special_names, etc.) owns a register_*_compositions()
// function that calls detail::add_builtin_composition() directly.  This file
// is the single call-site that orchestrates all domains.
//
// ExtensionRegistry has been removed — composition registration uses the
// canonical detail::add_builtin_composition() API.
#include "register_content_modules.hpp"

#include <chronon3d/core/composition/composition_registration.hpp>

// ── Per-domain registration headers ──────────────────────────────────────────
// Each domain file exports one register_*_compositions() function.
namespace chronon3d::content::minimalist   { void register_minimalist_compositions(); }
namespace chronon3d::content::special_names { void register_special_name_compositions(); }
namespace chronon3d::content::important_words { void register_important_word_compositions(); }
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
namespace chronon3d::content::shapes       { void register_shape_compositions(); }
namespace chronon3d::content::images       { void register_image_compositions(); }
#endif
namespace chronon3d::content::anims        { void register_anim_compositions(); }
namespace chronon3d::content::effects      { void register_effect_compositions(); }
namespace chronon3d::content::grid         { void register_grid_compositions(); }
namespace chronon3d::content::two_point_five_d { void register_2d5_compositions(); }

namespace chronon3d {

void register_content_modules() {
    content::minimalist::register_minimalist_compositions();
    content::special_names::register_special_name_compositions();
    content::important_words::register_important_word_compositions();
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    content::shapes::register_shape_compositions();
    content::images::register_image_compositions();
#endif
    content::anims::register_anim_compositions();
    content::effects::register_effect_compositions();
    content::grid::register_grid_compositions();
    content::two_point_five_d::register_2d5_compositions();
}

} // namespace chronon3d
