#pragma once

// ── TextGlowSpec V2 ──────────────────────────────────────────────────────────
//
// AE-like multi-layer glow for text.  A professional "cinematic" text glow is
// almost always the composition of three passes:
//
//   A. Core text    — sharp, high contrast, full opacity
//   B. Tight glow   — small blur, medium opacity, close to the glyph edge
//   C. Soft bloom   — large blur, low opacity, slightly colored atmospheric
//
// Single-pass glows look "flat", "cloudy", and "foggy" because they smear
// the glyph alpha and lose the sharp core.  TextGlowSpec exposes the three
// passes as named, intuitive parameters and maps them onto GlowParams with
// a populated GlowLayer[] (which the engine already supports via the
// SkiaLike quality path).
//
// Example:
//
//   TextGlowSpec glow;
//   glow.inner_color    = Color{0.87f, 0.92f, 1.0f, 1.0f};  // tight: cool white
//   glow.outer_color    = Color{0.50f, 0.70f, 1.0f, 1.0f};  // bloom: cool blue
//   glow.inner_radius   = 4.0f;
//   glow.mid_radius     = 14.0f;
//   glow.bloom_radius   = 34.0f;
//   glow.inner_intensity = 0.55f;
//   glow.mid_intensity   = 0.22f;
//   glow.bloom_intensity = 0.08f;
//   glow.micro_shadow    = true;            // subtle dark shadow underneath
//   l.text_glow("hero", glow);
//
// TextGlowPresets::ae_cinematic_white() / ae_cinematic_warm() / ae_neon() /
// ae_editorial() provide ready-made recipes.

#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>

namespace chronon3d {

struct TextGlowSpec {
    // ── Colors (tint separate from the text color) ──────────────────────
    Color inner_color{1.0f, 1.0f, 1.0f, 1.0f};        // tight glow (near white)
    Color outer_color{0.50f, 0.70f, 1.0f, 1.0f};      // bloom (cool blue tint)

    // ── 3-layer radii (px) ──────────────────────────────────────────────
    f32   inner_radius{4.0f};     // tight glow (close to glyph edge)
    f32   mid_radius{14.0f};      // medium bloom
    f32   bloom_radius{34.0f};    // atmospheric bloom

    // ── 3-layer intensities (0..1+) ─────────────────────────────────────
    f32   inner_intensity{0.55f};
    f32   mid_intensity{0.22f};
    f32   bloom_intensity{0.08f};

    // ── Threshold / softness / falloff ──────────────────────────────────
    f32   threshold{0.0f};
    f32   softness{1.0f};
    f32   falloff{0.9f};

    // ── Quality / blend ─────────────────────────────────────────────────
    f32   spread{1.0f};
    f32   outer_downscale{0.25f};      // downsample outer bloom for perf
    bool  core_preservation{true};     // keep text sharp (no blurry text)
    bool  use_additive_bloom{true};    // Add vs Screen blend mode

    // ── Optional micro shadow (subtle dark drop shadow under the text) ─
    bool  micro_shadow{false};
    Color micro_shadow_color{0.0f, 0.02f, 0.12f, 0.95f};
    f32   micro_shadow_radius{10.0f};
    f32   micro_shadow_opacity{0.12f};
    Vec2  micro_shadow_offset{0.0f, 4.0f};

    // ── Convert to GlowParams with the 3 GlowLayer entries populated ─────
    [[nodiscard]] GlowParams to_glow_params() const {
        GlowParams p;
        p.color         = inner_color;
        p.quality       = GlowQuality::MultiLayer;
        p.preserve_source = core_preservation;
        p.blend         = use_additive_bloom ? BlendMode::Add : BlendMode::Screen;
        p.additive      = use_additive_bloom;
        p.threshold     = threshold;
        p.softness      = softness;
        p.falloff       = falloff;
        p.spread        = spread;
        p.outer_downscale = outer_downscale;
        p.radius        = bloom_radius;
        p.intensity     = 1.0f;
        // Map AE-like 3-layer fields onto GlowLayer[] entries.  Each layer's
        // radius is in absolute pixels; scale=1.0 means full-res (no
        // downsample) except the outer bloom which uses outer_downscale.
        p.layers = {
            {inner_radius, inner_intensity, 1.0f, inner_color},
            {mid_radius,   mid_intensity,   1.0f, outer_color},
            {bloom_radius, bloom_intensity, outer_downscale, outer_color},
        };
        // Also keep the legacy core/aura/bloom_strength fields in sync so
        // any code path that reads them instead of layers still gets a
        // sensible value.
        p.core_strength  = inner_intensity;
        p.aura_strength  = mid_intensity;
        p.bloom_strength = bloom_intensity;
        return p;
    }
};

// ── TextGlowPresets ─────────────────────────────────────────────────────────
//
// Ready-made recipes following the "AE-like cinematic text glow" guidelines.
// All presets enable core_preservation and micro_shadow so text stays sharp.

namespace TextGlowPresets {

// Cinematic white-on-dark with a cool blue bloom.  The default for "premium"
// text glow on dark backgrounds (the recipe from the design doc).
[[nodiscard]] inline TextGlowSpec ae_cinematic_white() {
    TextGlowSpec s;
    s.inner_color     = Color{0.87f, 0.92f, 1.0f, 1.0f};   // #DDEBFF
    s.outer_color     = Color{0.50f, 0.70f, 1.0f, 1.0f};   // #7FB3FF cool blue
    s.inner_radius    = 4.0f;
    s.mid_radius      = 14.0f;
    s.bloom_radius    = 34.0f;
    s.inner_intensity = 0.55f;
    s.mid_intensity   = 0.22f;
    s.bloom_intensity = 0.08f;
    s.softness        = 1.05f;
    s.falloff         = 0.95f;
    s.micro_shadow    = true;
    s.micro_shadow_radius  = 10.0f;
    s.micro_shadow_opacity = 0.12f;
    s.micro_shadow_color   = Color{0.0f, 0.02f, 0.12f, 0.95f};
    return s;
}

// Warm gold glow (cinematic gold, "After Effects cinematic title" look).
[[nodiscard]] inline TextGlowSpec ae_cinematic_warm() {
    TextGlowSpec s;
    s.inner_color     = Color{1.0f, 0.94f, 0.80f, 1.0f};
    s.outer_color     = Color{1.0f, 0.72f, 0.22f, 1.0f};
    s.inner_radius    = 5.0f;
    s.mid_radius      = 18.0f;
    s.bloom_radius    = 44.0f;
    s.inner_intensity = 0.60f;
    s.mid_intensity   = 0.25f;
    s.bloom_intensity = 0.10f;
    s.softness        = 1.10f;
    s.falloff         = 0.95f;
    s.micro_shadow    = true;
    s.micro_shadow_color = Color{0.08f, 0.02f, 0.0f, 0.95f};
    s.micro_shadow_opacity = 0.14f;
    return s;
}

// Saturated neon glow for colored text (cyan / magenta / lime).
[[nodiscard]] inline TextGlowSpec ae_neon() {
    TextGlowSpec s;
    s.inner_color     = Color{0.16f, 0.68f, 1.0f, 1.0f};
    s.outer_color     = Color{0.16f, 0.68f, 1.0f, 1.0f};
    s.inner_radius    = 3.0f;
    s.mid_radius      = 12.0f;
    s.bloom_radius    = 32.0f;
    s.inner_intensity = 0.75f;
    s.mid_intensity   = 0.32f;
    s.bloom_intensity = 0.12f;
    s.softness        = 0.95f;
    s.falloff         = 0.85f;
    s.use_additive_bloom = true;
    return s;
}

// Subtle editorial glow with luminance threshold.  For light text on busy
// backgrounds where you only want the brightest parts to bloom.
[[nodiscard]] inline TextGlowSpec ae_editorial() {
    TextGlowSpec s;
    s.inner_color     = Color{0.95f, 0.98f, 1.0f, 1.0f};
    s.outer_color     = Color{0.80f, 0.90f, 1.0f, 1.0f};
    s.inner_radius    = 3.0f;
    s.mid_radius      = 10.0f;
    s.bloom_radius    = 24.0f;
    s.inner_intensity = 0.42f;
    s.mid_intensity   = 0.18f;
    s.bloom_intensity = 0.05f;
    s.threshold       = 0.14f;
    s.softness        = 1.30f;
    s.falloff         = 1.12f;
    s.use_additive_bloom = false;        // Screen blend for subtle look
    s.micro_shadow    = true;
    s.micro_shadow_opacity = 0.08f;
    return s;
}

} // namespace TextGlowPresets

} // namespace chronon3d
