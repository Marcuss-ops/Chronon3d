#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// LayerBuilder — Video & Precomp Commands
//
// Video source layers and precomposition references.
//
// This header is #included inside class LayerBuilder { … };
// ═════════════════════════════════════════════════════════════════════════════

    // ── Video ──
    LayerBuilder& video(video::VideoSource source);
    LayerBuilder& video(std::string path);
    LayerBuilder& video_size(Vec2 size);

    // ── Precomp ──
    LayerBuilder& precomp(std::string comp_name);
