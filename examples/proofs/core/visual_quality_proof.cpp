#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

// ---------------------------------------------------------------------------
// 1. RoundedRectProof
//    Verifica: angoli tagliati, radius piccolo/grande/clampato, center-based.
// ---------------------------------------------------------------------------
Composition RoundedRectProof() {
    CompositionSpec spec{.name="RoundedRectProof", .width=800, .height=450, .duration=1};
    return Composition{spec, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        // Reference: plain rect (no rounding)
        s.rect("normal", {140, 225, 0}, Color{0.35f, 0.35f, 0.35f, 1}, {140, 100});
        // White card — clear rounding r=24
        s.rounded_rect("white-r24",   {320, 225, 0}, {140, 100}, 24.0f, Color::white());
        // Red — small radius r=8 (barely visible rounding)
        s.rounded_rect("red-r8",      {500, 150, 0}, {140, 80},   8.0f, Color::red());
        // Green — large radius r=40 (almost pill-shaped)
        s.rounded_rect("green-r40",   {500, 300, 0}, {140, 80},  40.0f, Color::green());
        // Blue — radius=999 must be clamped to min(hw,hh)=50
        s.rounded_rect("blue-clamp",  {660, 225, 0}, {100, 100}, 999.0f, Color::blue());
        return s.build();
    }};
}
CHRONON_REGISTER_COMPOSITION("RoundedRectProof", RoundedRectProof)

// ---------------------------------------------------------------------------
// 2. ShadowProof
//    Verifica: shadow sotto/destra, forma rounded, alpha, disabled=no shadow.
// ---------------------------------------------------------------------------
Composition ShadowProof() {
    CompositionSpec spec{.name="ShadowProof", .width=800, .height=450, .duration=1};
    return Composition{spec, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        s.rect("bg", {400, 225, 0}, Color{0.08f, 0.08f, 0.10f, 1}, {800, 450});

        // White card — standard shadow offset bottom-right
        s.rounded_rect("white-card", {220, 200, 0}, {240, 120}, 20.0f, Color::white())
         .with_shadow(DropShadow{.enabled=true, .offset={18, 20},
                                 .color={0, 0, 0, 0.45f}, .radius=18.0f});

        // Red card — stronger shadow
        s.rounded_rect("red-card", {520, 145, 0}, {180, 90}, 16.0f, Color::red())
         .with_shadow(DropShadow{.enabled=true, .offset={12, 12},
                                 .color={0, 0, 0, 0.70f}, .radius=10.0f});

        // Green card — soft diffuse shadow
        s.rounded_rect("green-card", {520, 305, 0}, {180, 90}, 16.0f, Color::green())
         .with_shadow(DropShadow{.enabled=true, .offset={8, 8},
                                 .color={0, 0, 0, 0.25f}, .radius=24.0f});

        // Grey card — no shadow (reference comparison)
        s.rounded_rect("no-shadow", {120, 390, 0}, {120, 50}, 10.0f,
                       Color{0.7f, 0.7f, 0.7f, 1});

        return s.build();
    }};
}
CHRONON_REGISTER_COMPOSITION("ShadowProof", ShadowProof)

// ---------------------------------------------------------------------------
// 3. GlowProof
//    Verifica: glow fuori dalla shape, shape originale non cancellata,
//              intensity=0 non visibile, colore glow corretto.
// ---------------------------------------------------------------------------
Composition GlowProof() {
    CompositionSpec spec{.name="GlowProof", .width=800, .height=450, .duration=1};
    return Composition{spec, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);

        // Blue circle with strong glow
        s.circle("blue-glow", {160, 225, 0}, 50.0f, Color{0.1f, 0.45f, 1.0f, 1})
         .with_glow(Glow{.enabled=true, .radius=40.0f, .intensity=0.8f,
                         .color={0.1f, 0.45f, 1.0f, 1}});

        // Yellow line with glow (glow on line = halo around line endpoints)
        s.line("yellow-line-a", {330, 145, 0}, {580, 145, 0}, Color{1.0f, 0.85f, 0.1f, 1})
         .with_glow(Glow{.enabled=true, .radius=18.0f, .intensity=0.9f,
                         .color={1.0f, 0.85f, 0.1f, 1}});
        s.line("yellow-line-b", {330, 165, 0}, {580, 165, 0}, Color{1.0f, 0.85f, 0.1f, 1});

        // Purple rounded rect with glow
        s.rounded_rect("purple-glow", {470, 320, 0}, {200, 90}, 20.0f,
                       Color{0.65f, 0.25f, 1.0f, 1})
         .with_glow(Glow{.enabled=true, .radius=26.0f, .intensity=0.7f,
                         .color={0.65f, 0.25f, 1.0f, 1}});

        // White circle — no glow (reference)
        s.circle("no-glow", {700, 330, 0}, 35.0f, Color::white());

        // Red circle — intensity=0 (should look identical to no-glow)
        s.circle("zero-intensity", {700, 130, 0}, 35.0f, Color::red())
         .with_glow(Glow{.enabled=true, .radius=30.0f, .intensity=0.0f,
                         .color={1, 0, 0, 1}});

        return s.build();
    }};
}
CHRONON_REGISTER_COMPOSITION("GlowProof", GlowProof)

// ---------------------------------------------------------------------------
// 4. VisualStackOrderProof
//    Verifica: shadow < glow < shape < shape2 < line (Painter's Algorithm).
// ---------------------------------------------------------------------------
Composition VisualStackOrderProof() {
    CompositionSpec spec{.name="VisualStackOrderProof", .width=800, .height=450, .duration=1};
    return Composition{spec, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        s.rect("bg", {400, 225, 0}, Color{0.04f, 0.04f, 0.055f, 1}, {800, 450});

        // Main card: shadow behind, blue glow ring, white fill
        s.rounded_rect("main-card", {400, 225, 0}, {360, 160}, 28.0f,
                       Color{0.95f, 0.95f, 1.0f, 1})
         .with_shadow(DropShadow{.enabled=true, .offset={22, 24},
                                 .color={0, 0, 0, 0.5f}, .radius=22.0f})
         .with_glow(Glow{.enabled=true, .radius=20.0f, .intensity=0.35f,
                         .color={0.2f, 0.5f, 1.0f, 1}});

        // Red semi-transparent circle painted over card
        s.circle("red-over-card",  {340, 225, 0}, 60.0f, Color{1.0f, 0.1f, 0.1f, 0.55f});
        // Blue semi-transparent circle painted over red (blending visible)
        s.circle("blue-over-red",  {460, 225, 0}, 60.0f, Color{0.1f, 0.25f, 1.0f, 0.55f});
        // White line on top of everything
        s.line("top-line", {230, 225, 0}, {570, 225, 0}, Color{1, 1, 1, 1});

        return s.build();
    }};
}
CHRONON_REGISTER_COMPOSITION("VisualStackOrderProof", VisualStackOrderProof)

// ---------------------------------------------------------------------------
// 5. AnimatedVisualQualityProof
//    Verifica: spring + shadow + glow animati insieme su 100 frame.
//    Frame 0: card fuori schermo. Frame 30: in arrivo. Frame 60+: stabile.
// ---------------------------------------------------------------------------
Composition AnimatedVisualQualityProof() {
    CompositionSpec spec{.name="AnimatedVisualQualityProof", .width=800, .height=450, .duration=100};
    return Composition{spec, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        s.rect("bg", {400, 225, 0}, Color{0.035f, 0.035f, 0.05f, 1}, {800, 450});

        // Card enters from above via spring; alpha fades in over first 25 frames
        const auto intro  = sequence(ctx, Frame{0}, Frame{70});
        const f32 card_y  = spring(intro, -100.0f, 225.0f);
        const f32 alpha   = interpolate(ctx.frame, Frame{0},  Frame{25},  0.0f, 1.0f);
        const f32 line_x  = interpolate(ctx.frame, Frame{15}, Frame{70}, 260.0f, 540.0f);

        // Accent circle with glow — positioned relative to card
        s.circle("accent-glow", {260.0f, card_y - 50.0f, 0}, 34.0f,
                 Color{0.1f, 0.45f, 1.0f, 0.8f * alpha})
         .with_glow(Glow{.enabled=true, .radius=30.0f, .intensity=0.7f * alpha,
                         .color={0.1f, 0.45f, 1.0f, 1}});

        // Main card with shadow + subtle glow
        s.rounded_rect("main-card", {400.0f, card_y, 0}, {420, 150}, 26.0f,
                       Color{0.95f, 0.95f, 1.0f, alpha})
         .with_shadow(DropShadow{.enabled=true, .offset={0, 18},
                                 .color={0, 0, 0, 0.45f * alpha}, .radius=22.0f})
         .with_glow(Glow{.enabled=true, .radius=16.0f, .intensity=0.25f * alpha,
                         .color={0.2f, 0.5f, 1.0f, 1}});

        // Animated accent line
        s.line("accent-line",
               {260.0f, card_y + 44.0f, 0}, {line_x, card_y + 44.0f, 0},
               Color{0.38f, 0.68f, 1.0f, alpha});

        // Fake title bars (placeholder until text rendering exists)
        s.rounded_rect("title-bar-1", {400.0f, card_y - 22.0f, 0}, {240, 16}, 8.0f,
                       Color{0.15f, 0.15f, 0.20f, alpha});
        s.rounded_rect("title-bar-2", {400.0f, card_y + 10.0f, 0}, {155, 12}, 6.0f,
                       Color{0.20f, 0.20f, 0.28f, alpha});

        return s.build();
    }};
}
CHRONON_REGISTER_COMPOSITION("AnimatedVisualQualityProof", AnimatedVisualQualityProof)
