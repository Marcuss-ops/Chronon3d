#pragma once

#include <chronon3d/math/color.hpp>

namespace chronon3d {

struct Materials {
    static Color studio_orange()      { return Color::from_hex("#FF6B00"); }
    static Color studio_blue()        { return Color::from_hex("#007AFF"); }
    static Color studio_red()         { return Color::from_hex("#FF3B30"); }
    static Color studio_purple()      { return Color::from_hex("#AF52DE"); }
    static Color studio_green()       { return Color::from_hex("#34C759"); }
    
    static Color soft_plastic_white() { return Color{0.95f, 0.95f, 0.95f, 1.0f}; }
    static Color soft_plastic_gray()  { return Color{0.25f, 0.25f, 0.25f, 1.0f}; }
    
    static Color dark_floor_grid()    { return Color{1.0f, 1.0f, 1.0f, 0.15f}; }
    static Color studio_background()  { return Color::from_hex("#111111"); }

    static Color emissive_white()      { return Color{1.0f, 1.0f, 1.0f, 1.0f}; }
    static Color emissive_warm()       { return Color::from_hex("#FFF5E0"); }

    static Color glass_panel_tint()   { return {0.95f, 0.95f, 1.0f, 0.25f}; }
    static Color glass_border()       { return {1.0f, 1.0f, 1.0f, 0.40f}; }
};


} // namespace chronon3d
