#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// LayerBuilder — Shape Commands
//
// 2D and 3D shape builder methods.
//
// This header is #included inside class LayerBuilder { … };
// ═════════════════════════════════════════════════════════════════════════════

    // ── 2D Shapes ──
    LayerBuilder& rect(std::string name, RectParams p);
    LayerBuilder& rounded_rect(std::string name, RoundedRectParams p);
    LayerBuilder& circle(std::string name, CircleParams p);
    LayerBuilder& line(std::string name, LineParams p);
    LayerBuilder& path(std::string name, PathParams p);
    LayerBuilder& arrow(std::string name, ArrowParams p);
    LayerBuilder& star(std::string name, StarParams p);
    LayerBuilder& badge(std::string name, BadgeParams p);
    LayerBuilder& speech_bubble(std::string name, SpeechBubbleParams p);
    LayerBuilder& callout(std::string name, CalloutParams p);
    LayerBuilder& progress_bar(std::string name, ProgressBarParams p);
    LayerBuilder& timeline_bar(std::string name, TimelineBarParams p);
    LayerBuilder& image(std::string name, ImageParams p);
    LayerBuilder& tiled_image(std::string name, ImageParams p);
    LayerBuilder& grid_background(std::string name, GridBackgroundParams p);
    LayerBuilder& text(std::string name, TextParams p);
    LayerBuilder& shape(std::string_view id, std::string name, registry::ShapeParams params);

    // ── 3D Shapes (Delegated) ──
    LayerBuilder& fake_box3d(std::string name, FakeBox3DParams p);
    LayerBuilder& grid_plane(std::string name, GridPlaneParams p);
