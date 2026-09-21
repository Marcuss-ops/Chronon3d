#pragma once
// ============================================================================
// apps/chronon3d_cli/canary/register_canary.hpp
//
// The canary pack is one family per subfile, one registration function per
// subfile.  `register_canary_compositions()` (register_canary_compositions.cpp)
// is the only caller, and it calls these six in the order below — the order is
// the registry order, so it is part of the pack's observable output.
//
//   motion3d          Motion3DGlowOrbit, Motion3DSentenceOrbit,
//                     Motion3DTextParentCamera, Motion3DSameMotion{Text,Image}
//   text              chronon_text_full_static,
//                     chronon_text_elements_{static,opacity,motion}
//   final3d           logo_reveal, product_orbit, title_card_3d, camera_push,
//                     light_pulse, macbook_product, typewriter_3d_glow,
//                     clean_red, youtube_canary
//   font baselines    black_glow_{inter,poppins,dmsans,georgia,freeserif}
//   apple v3          black_glow_<font>_<motion>, 5 fonts × 5 motions
//   final cards       typewriter_final_poppins,
//                     black_glow_freeserif_smooth_orbit
//
// The shared authoring kit is canary/canary_common.hpp (constants, timing,
// text shape, orbit camera, specs) and canary/canary_text_animation.hpp
// (typewriter and element definitions).
// ============================================================================

namespace chronon3d {

class CompositionRegistry;

/// The Motion3D measured scenes: hero, metrics/bounds, hierarchy, same-motion pair.
void register_canary_motion3d_compositions(CompositionRegistry &registry);

/// The Chronon text-element contract canaries (static identity, opacity stagger,
/// opacity + per-element motion delta) plus the plain full-run reference.
void register_canary_text_compositions(CompositionRegistry &registry);

/// The nine Final3D production composition IDs.
void register_canary_final3d_compositions(CompositionRegistry &registry);

/// The five static font/glow baselines: one shaped word, one halo, no motion.
void register_canary_font_baseline_compositions(CompositionRegistry &registry);

/// The Apple V3 choreography matrix: the five baselines' fonts, five motions.
void register_canary_apple_v3_compositions(CompositionRegistry &registry);

/// The two final card variants: the typewriter card and the smooth 3D orbit recut.
void register_canary_final_card_compositions(CompositionRegistry &registry);

} // namespace chronon3d
