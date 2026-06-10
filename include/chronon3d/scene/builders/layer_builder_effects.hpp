#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// LayerBuilder — Effects Commands
//
// Effect-related fluent methods (color correction, blurs, shadows, glows, etc.)
//
// This header is #included inside class LayerBuilder { … };
// ═════════════════════════════════════════════════════════════════════════════

    // ── Effects ──
    LayerBuilder& blur(f32 radius);
    LayerBuilder& tint(Color color, f32 amount = 1.0f);
    LayerBuilder& brightness(f32 v);
    LayerBuilder& contrast(f32 v);
    LayerBuilder& saturation(f32 v);    // 1.0 = unchanged, 0 = greyscale
    LayerBuilder& hue_rotate(f32 deg);  // degrees, 0 = unchanged
    LayerBuilder& invert(f32 amount = 1.0f);
    LayerBuilder& vignette(f32 radius = 0.5f, f32 softness = 0.5f, f32 amount = 1.0f);
    LayerBuilder& drop_shadow(Vec2 offset, Color color = {0,0,0,0.35f}, f32 radius = 12.0f);
    LayerBuilder& glow(GlowParams params);
    LayerBuilder& bloom(f32 threshold = 0.80f, f32 radius = 24.0f, f32 intensity = 0.60f);
    LayerBuilder& fake_3d_wave(Fake3DWaveParams params);
    LayerBuilder& blend(BlendMode mode);
