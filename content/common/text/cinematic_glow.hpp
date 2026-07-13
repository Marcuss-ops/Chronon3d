#pragma once

// ── cinematic_glow — AE cinematic glow configuration + apply helper ───────
//
// P1 refactor — extracted from the 381-LoC `content/common/text_reveal_helpers.hpp`
// monolith (Step 1 of 4).  Single-responsibility: AE cinematic glow config
// (CinematicGlowPreset struct + apply_cinematic_glow() helper).
//
// Refactor lineage: this file (cinematic_glow) is one of 4 single-responsibility
// TUs split from the original 381-LoC header-only `content/common/text_reveal_helpers.hpp`
// (Step 1 of 4).  Sibling TUs: `glyph_layout` + `text_reveal` + `typewriter_block`,
// all sharing the flat `chronon3d::content::text_reveal` namespace.
//
// Namespace: chronon3d::content::text_reveal (single flat namespace per
// Cat-3 minimal-surface — preserves the 12 existing callers' `using`
// declarations).

#include <chronon3d/core/types/types.hpp>  // f32, Vec2, Color (canonical SDK types header)
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/text_glow_spec.hpp>  // TextGlowPresets

namespace chronon3d::content::text_reveal {

// CinematicGlowPreset — AE cinematic glow configuration
//
// Replaces the locally-copied CinematicGlowOpts struct that was duplicated
// in cinematic_text_camera.cpp after the original glow::apply_ae_glow was
// removed from text_helpers.
//
// Drop-shadow fields are preserved with safe defaults (off); they were unused
// at migrated call sites but kept for symmetry with the old GlowTextOpts.
struct CinematicGlowPreset {
    f32   inner_radius    {4.0f};
    f32   mid_radius      {14.0f};
    f32   bloom_radius    {34.0f};
    f32   inner_intensity {0.55f};
    f32   mid_intensity   {0.22f};
    f32   bloom_intensity {0.08f};
    bool  micro_shadow    {true};
    Vec2  shadow_offset   {0.0f, 4.0f};
    Color shadow_color    {0.0f, 0.02f, 0.12f, 0.15f};
    f32   shadow_blur     {10.0f};
    bool  drop_shadow     {false};
    Vec2  drop_offset     {0.0f, 8.0f};
    Color drop_color      {0.0f, 0.0f, 0.0f, 0.20f};
    f32   drop_blur       {18.0f};
};

// apply_cinematic_glow — apply the AE cinematic glow configuration to a layer.
//
// Default arg is the canonical CinematicGlowPreset{} (the default-initialized
// preset) — preserves the pre-split single-arg-callable API.  Per AGENTS.md
// `### C++ default-arg uniqueness per TU`, this default arg appears ONLY in
// the .hpp declaration; the .cpp definition is bare (no default arg).
void apply_cinematic_glow(LayerBuilder& l, const CinematicGlowPreset& opts = {});

} // namespace chronon3d::content::text_reveal
