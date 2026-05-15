# LilDirk Neon Badge — Design Spec
Date: 2026-05-15

## Goal
Render the LIL DIRK animation from `animation.mp4`: dark crosshatch background,
single red box behind white text, white glow pulse around the box, XYZ tilt.
All pieces modular and reusable.

## Files Changed
- `include/chronon3d/scene/builders/builder_params.hpp` — add `DarkGridBgParams`
- `include/chronon3d/templates/neon_badge_templates.hpp` — NEW: `NeonBadgeParams` + helpers
- `examples/proofs/fake3d/text3d_serious_proof.cpp` — rewrite `LilDirkDemo` using templates

## DarkGridBgParams
```cpp
struct DarkGridBgParams {
    Color bg_color   {0.098f, 0.098f, 0.11f, 1.f};
    Color grid_color {1.f, 1.f, 1.f, 0.045f};
    f32   spacing    {80.f};
    f32   extent     {4000.f};
};
```
`dark_grid_background()` sets up Camera2_5D ortho-front + bg rect + GridPlane XY internally.

## NeonBadgeParams
```cpp
struct NeonBadgeParams {
    std::string text, font_path;
    f32   font_size;
    Color box_color, glow_color;
    Vec2  box_padding;
    f32   box_depth, glow_radius, glow_base, glow_amplitude, glow_speed;
    bool  enable_tilt, enable_bars;
    f32   tilt_x_amp, tilt_y_amp, tilt_z_amp, bar_wipe_duration, bar_height;
};
```

## neon_badge() layer stack
1. `nb_box`   — FakeBox3D (box_color), `.with_glow({pulse_intensity, glow_radius, glow_color})`, tilt
2. `nb_bar_t` — FakeBox3D top bar, easeOutBack wipe-in, same tilt
3. `nb_bar_b` — FakeBox3D bottom bar, easeOutBack wipe-in, same tilt
4. `nb_text`  — FakeExtrudedText depth=0, white, same tilt

Pulse: `intensity = glow_base + glow_amplitude * sin(time * glow_speed * 2π)`

## LilDirkDemo result
```cpp
templates::dark_grid_background(s, ctx, {});
templates::neon_badge(s, ctx, {.text="LIL DIRK", .box_color={0.86,0.08,0.12,1}, .glow_color=white});
```
