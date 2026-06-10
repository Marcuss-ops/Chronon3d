#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// LayerBuilder — Mask & Track-Matte Commands
//
// Rect/circle/rounded-rect masks and alpha/luma track mattes.
//
// This header is #included inside class LayerBuilder { … };
// ═════════════════════════════════════════════════════════════════════════════

    // ── Masks ──
    LayerBuilder& mask_rect(RectMaskParams p);
    LayerBuilder& mask_rounded_rect(RoundedRectMaskParams p);
    LayerBuilder& mask_circle(CircleMaskParams p);

    // ── Track matte ──
    LayerBuilder& track_matte_alpha(std::string source_layer_name);
    LayerBuilder& track_matte_alpha_inverted(std::string source_layer_name);
    LayerBuilder& track_matte_luma(std::string source_layer_name);
    LayerBuilder& track_matte_luma_inverted(std::string source_layer_name);
