#pragma once
// ── Shared helpers for cinematic title reveal compositions ────────────
// Both tilt_sweep_title (2D keyframe) and tilt_sweep_title_v2 (3D camera)
// share the same artist-name text params and near-black background.

#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

namespace chronon3d::content::anims {

// ── make_artist_name_text ──────────────────────────────────────────────
// Shared text params for the hero artist name. Both TiltSweepTitle and
// TiltSweepTitleV2 use Georgia Bold, 118pt, PixelInk centering, 1.0f
// tracking in a 900×180 box.  The colour defaults to cinematic off-white;
// V2 overrides it with pure white.
inline TextParams make_artist_name_text(const char* name, Color color) {
    return TextParams{
        .text           = name,
        .size           = {900.0f, 180.0f},
        .pos            = {0.0f, 0.0f, 0.0f},
        .font_path      = "assets/fonts/Georgia_Bold.ttf",
        .font_family    = "Georgia",
        .font_weight    = 700,
        .font_size      = 118.0f,
        .color          = color,
        .anchor         = TextAnchor::Center,
        .centering_mode = TextCenteringMode::PixelInk,
        .align          = TextAlign::Center,
        .vertical_align = VerticalAlign::Middle,
        .tracking       = 1.0f,
        .wrap           = TextWrap::None,
    };
}

// ── add_cinematic_bg ───────────────────────────────────────────────────
// Near-black fullscreen background. Used by both TiltSweepTitle (with
// vignette) and TiltSweepTitleV2 (without).  When `centered` is false the
// rect is placed at origin (2D canvas convention); when true it is
// centred at (960, 540) for 1080p (3D camera scenes prefer this).
inline void add_cinematic_bg(SceneBuilder& s, bool centered = false,
                              bool with_vignette = false) {
    s.layer("background", [centered, with_vignette](LayerBuilder& l) {
        l.rect("bg", {
            .size  = {1920.0f, 1080.0f},
            .color = {0.006f, 0.006f, 0.008f, 1.0f},
            .pos   = centered ? Vec3{960.0f, 540.0f, 0.0f} : Vec3{0.0f, 0.0f, 0.0f}
        });
        if (with_vignette) {
            l.vignette(0.45f, 0.55f, 0.60f);
        }
    });
}

} // namespace chronon3d::content::anims
