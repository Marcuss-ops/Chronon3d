#pragma once
// ── Shared Text Reveal Helpers ─────────────────────────────────────────────
//
// Canonical helpers for per-glyph text reveals (typewriter, stagger, slide-up)
// extracted from text_animations.cpp and cinematic_text_camera.cpp.  Both files
// had near-identical glyph layout + font engine + stagger/typewriter builders;
// this header consolidates them into a single source of truth.
//
// Contents:
//   1. CinematicGlowPreset + apply_cinematic_glow()  — AE cinematic glow config
//   2. GlyphLayoutEngine (shared FontEngine, measure, layout)
//   3. TextRevealDescriptor + build_text_reveal_line() — per-glyph reveal builder
//
// Namespace: chronon3d::content::text_reveal

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/text/text_glow_spec.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/runtime/render_runtime.hpp>

#include <string>
#include <vector>

namespace chronon3d::content::text_reveal {

// ═════════════════════════════════════════════════════════════════════════════
// 1. CinematicGlowPreset — AE cinematic glow configuration
// ═════════════════════════════════════════════════════════════════════════════
//
// Replaces the locally-copied CinematicGlowOpts struct that was duplicated
// in cinematic_text_camera.cpp after the original glow::apply_ae_glow was
// removed from text_helpers.
//
// Drop-shadow fields are preserved with safe defaults (off); they were unused
// at migrated call sites but kept for symmetry with the old GlowTextOpts.
//
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

inline void apply_cinematic_glow(LayerBuilder& l, const CinematicGlowPreset& opts = {}) {
    auto glow = TextGlowPresets::ae_cinematic_white();
    glow.inner_radius    = opts.inner_radius;
    glow.mid_radius      = opts.mid_radius;
    glow.bloom_radius    = opts.bloom_radius;
    glow.inner_intensity = opts.inner_intensity;
    glow.mid_intensity   = opts.mid_intensity;
    glow.bloom_intensity = opts.bloom_intensity;
    l.glow(glow.to_glow_params());
    if (opts.micro_shadow && glow.micro_shadow) {
        l.drop_shadow(opts.shadow_offset, opts.shadow_color, opts.shadow_blur);
    }
    if (opts.drop_shadow) {
        l.drop_shadow(opts.drop_offset, opts.drop_color, opts.drop_blur);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. GlyphLayoutEngine — shared FontEngine, measure, layout
// ═════════════════════════════════════════════════════════════════════════════
//
// Replaces the per-file FontEngine singletons (cinematic_font_engine,
// anim_font_engine) with a single accessor.  Font path is a parameter
// so callers control whether to use Poppins-Bold, Poppins-Regular, etc.
//

// ── Shared font engine accessor ─────────────────────────────────────────
// WP-8 PR 8.0 — takes the AssetResolver explicitly; caller-supplied.
// Process-global fallbacks REMOVED in PR 8.0.  Function-local static
// re-creates the engine when the resolver pointer changes; safe because
// AssetResolver is engine-local so the pointer identity is stable for
// the runtime lifetime.
inline FontEngine& shared_glyph_engine(
    const chronon3d::assets::AssetResolver& resolver) {
    static const chronon3d::assets::AssetResolver* s_resolver{nullptr};
    static std::unique_ptr<FontEngine>           s_engine;
    if (s_resolver != &resolver) {
        s_engine = std::make_unique<FontEngine>(resolver);
        s_resolver = &resolver;
    }
    return *s_engine;
}

// ── Per-glyph position result ───────────────────────────────────────────
struct GlyphPos {
    std::string ch;
    f32         center_x{0.0f};
    f32         width{0.0f};
};

// ── measure_text_width ──────────────────────────────────────────────────
// Returns total advance width INCLUDING tracking, matching layout_glyphs output.
// WP-8 PR 8.1 — caller-supplied resolver (default = the post-bridge
// process-wide channel for content-layer call sites lacking a runtime
// reference; the bridge was retired in WP-8 PR 8.1).
inline f32 measure_text_width(const std::string& text, f32 font_size,
                               const FontSpec& spec, f32 tracking,
                               const chronon3d::assets::AssetResolver& resolver =
                                   chronon3d::runtime::process_wide_resolver()) {
    FontEngine& eng = shared_glyph_engine(resolver);
    auto run = eng.shape_text(text, spec, font_size);
    if (!run) return 0.0f;
    const size_t n = run->glyphs.size();
    return run->width + tracking * static_cast<f32>(n > 1 ? n - 1 : 0);
}

// ── layout_glyphs ───────────────────────────────────────────────────────
// Lays out text into per-glyph positions using HarfBuzz shaping.
// ref_offset_x: shared starting X (e.g. -max_width/2 to center-align).
// Returns glyph positions at their FINAL locations — only opacity/position
// animate per frame so the text block stays perfectly stable.
// WP-8 PR 8.1 — resolver parameter; default = process-wide channel (see measure_text_width rationale).
inline std::vector<GlyphPos> layout_glyphs(
    const std::string& text, f32 font_size,
    const FontSpec& spec, f32 tracking,
    f32 ref_offset_x,
    const chronon3d::assets::AssetResolver& resolver =
        chronon3d::runtime::process_wide_resolver()) {
    FontEngine& eng = shared_glyph_engine(resolver);
    auto run = eng.shape_text(text, spec, font_size);
    if (!run || run->glyphs.empty()) return {};

    std::vector<GlyphPos> out;
    out.reserve(run->glyphs.size());

    f32 cursor = ref_offset_x;
    for (size_t gi = 0; gi < run->glyphs.size(); ++gi) {
        const auto& g = run->glyphs[gi];
        size_t start = g.cluster;
        size_t end = text.size();
        for (size_t i = 0; i < run->glyphs.size(); ++i) {
            const auto& o = run->glyphs[i];
            if (o.cluster > start) { end = o.cluster; break; }
        }
        std::string ch = text.substr(start, end - start);
        if (ch.empty()) continue;
        out.push_back({ch, cursor + g.advance_x * 0.5f, g.advance_x});
        cursor += g.advance_x + tracking;
    }
    return out;
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. TextRevealDescriptor + build_text_reveal_line()
// ═════════════════════════════════════════════════════════════════════════════
//
// Replaces both build_stagger_line (cinematic_text_camera.cpp) and
// build_typewriter_line (text_animations.cpp).  All parameters live in the
// descriptor struct; callers that need different behaviours set different
// fields rather than passing a long positional argument list.

struct TextRevealDescriptor {
    // ── Text content ──
    std::string text;
    f32         font_size{72.0f};
    FontSpec    font_spec;          // must set .font_path (REQUIRED)

    // ── Layout ──
    f32 tracking{4.0f};
    f32 ref_offset_x{0.0f};        // set to -max_width/2 for center alignment

    // ── Position ──
    Vec3 base_pos{0.0f, 0.0f, 0.0f};  // y-pos of the text line

    // ── Timing ──
    f32 start_delay{0.0f};         // frames before first char starts
    f32 duration{8.0f};            // frames per char to reach final state
    f32 stagger{2.0f};             // frames between consecutive chars

    // ── Reveal style ──
    bool slide_up{false};           // slide from below during reveal
    f32  slide_up_px{30.0f};       // pixels to slide (used when slide_up=true)
    EasingCurve opacity_easing{Easing::OutCubic};
    EasingCurve position_easing{Easing::OutCubic};  // only used for slide_up

    // ── Canvas pinning ──
    bool  pin_to_center{false};  // call l.pin_to(Anchor::Center) before positioning

    // ── Appearance ──
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    bool  add_shadow{true};
    Vec2  shadow_offset{0.0f, 3.0f};
    Color shadow_color{0.0f, 0.0f, 0.0f, 0.15f};
    f32   shadow_blur{6.0f};

    // ── Glow (optional) ──
    f32  glow_intensity{0.0f};     // 0 = no glow; >0 adds per-char glow
    Color glow_color{0.87f, 0.92f, 1.0f, 1.0f};

    // ── Layer naming ──
    std::string layer_prefix{"ch"};  // prefix for layer names
};

// ── build_text_reveal_line ──────────────────────────────────────────────
//
// Creates one layer per character at its final pre-computed position.
// Only opacity (and optionally position) animate per frame — the text
// block stays perfectly stable.
//
// Single-call replacement for:
//   cinematic_text_camera.cpp: build_stagger_line(s, text, fs, base_pos, ...)
//   text_animations.cpp:      build_typewriter_line(s, text, fs, y_pos, ...)

inline void build_text_reveal_line(SceneBuilder& s,
                                   const TextRevealDescriptor& d) {
    auto chars = layout_glyphs(d.text, d.font_size, d.font_spec,
                               d.tracking, d.ref_offset_x);
    if (chars.empty()) return;

    for (size_t i = 0; i < chars.size(); ++i) {
        const auto& gc = chars[i];
        if (gc.ch.empty() || gc.ch == " ") continue;

        const f32 delay = d.start_delay + static_cast<f32>(i) * d.stagger;
        const f32 end_f = delay + d.duration;
        const f32 cx = gc.center_x;

        s.layer(d.layer_prefix + "_" + std::to_string(i),
                [cx, d, delay, end_f, ch = gc.ch, ch_w = gc.width]
                (LayerBuilder& l) {
            if (d.pin_to_center) {
                l.pin_to(Anchor::Center);
            }
            l.position({cx, d.base_pos.y, d.base_pos.z});

            // Opacity: invisible → visible → held
            {
                auto& op = l.opacity_anim();
                op.key(Frame{0},                                  0.0f, EasingCurve{Easing::Hold});
                op.key(Frame{static_cast<Frame>(delay)},          0.0f, d.opacity_easing);
                op.key(Frame{static_cast<Frame>(end_f)},          1.0f, EasingCurve{Easing::Hold});
            }

            // Optional slide-up
            if (d.slide_up) {
                auto& pos = l.position_anim();
                pos.key(Frame{0},     Vec3{cx, d.base_pos.y + d.slide_up_px, d.base_pos.z}, EasingCurve{Easing::Hold});
                pos.key(Frame{static_cast<Frame>(delay)}, Vec3{cx, d.base_pos.y + d.slide_up_px, d.base_pos.z}, d.position_easing);
                pos.key(Frame{static_cast<Frame>(end_f)}, Vec3{cx, d.base_pos.y, d.base_pos.z}, EasingCurve{Easing::Linear});
            }

            // Optional drop shadow
            if (d.add_shadow) {
                l.drop_shadow(d.shadow_offset, d.shadow_color, d.shadow_blur);
            }

            // Optional glow
            if (d.glow_intensity > 0.01f) {
                const f32 glow_radius = std::max(5.0f, d.font_size * 0.10f);
                l.glow(GlowParams{
                    .enabled         = true,
                    .radius          = glow_radius,
                    .intensity       = d.glow_intensity,
                    .color           = d.glow_color,
                    .preserve_source = true,
                    .additive        = true,
                });
            }

            // Text layer box sized to contain the glyph + glow padding
            const f32 pad = d.glow_intensity > 0.01f ? 40.0f : 12.0f;
            TextSpec ts;
            ts.content.value           = ch;
            ts.layout.box           = {ch_w + pad, d.font_size * 1.8f};
            ts.position            = {0.0f, 0.0f, 0.0f};
            ts.font.font_path      = d.font_spec.font_path;
            ts.font.font_family    = d.font_spec.font_family;
            ts.font.font_weight    = d.font_spec.font_weight;
            ts.font.font_size      = d.font_size;
            ts.appearance.color          = d.color;
            ts.layout.anchor         = TextAnchor::Center;
            ts.layout.align          = TextAlign::Center;
            ts.layout.vertical_align = VerticalAlign::Middle;
            ts.layout.line_height    = 1.10f;
            ts.layout.tracking       = 0.0f;

            l.text("label", ts);
        });
    }
}

// ── Quick factory helpers for common font specs ─────────────────────────

inline FontSpec font_bold() {
    return FontSpec{"assets/fonts/Poppins-Bold.ttf", "Poppins", 700};
}

inline FontSpec font_regular() {
    return FontSpec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
}

} // namespace chronon3d::content::text_reveal
