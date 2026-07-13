#include "content/common/text/text_reveal.hpp"

#include "content/common/text/glyph_layout.hpp"  // layout_glyphs()

#include <chronon3d/runtime/render_runtime.hpp>   // Anchor, GlowParams
#include <chronon3d/text/text_definition.hpp>     // from_text_spec

#include <algorithm>  // std::max
#include <stdexcept>
#include <string>
#include <vector>

namespace chronon3d::content::text_reveal {

// build_text_reveal_line — implementation
//
// Materialises one layer per character at its final pre-computed position
// (positions are pre-computed by layout_glyphs at scene-build time, so the
// text block is visually stable while only opacity / position animate per
// frame).  Fail-loud (std::runtime_error) on missing FontEngine per
// AGENTS.md §honesty.
void build_text_reveal_line(SceneBuilder& s, const TextRevealDescriptor& d) {
    FontEngine* engine = s.font_engine();
    if (!engine) {
        throw std::runtime_error(
            "build_text_reveal_line: SceneBuilder has no FontEngine. "
            "Ensure the renderer or composition wires a FontEngine "
            "before calling build_text_reveal_line(). "
            // truncate text to 60 chars (intentional, no ellipsis — keeps log lines bounded)
            "Text: '" + d.text.substr(0, 60) + "'");
    }
    ShapedGlyphLine line(d.text, d.font_size, d.font_spec,
                         d.tracking, d.ref_offset_x, *engine);
    auto chars = line.layout();

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
                op.add_keyframe(Frame{0},                                  0.0f, EasingCurve{Easing::Hold});
                op.add_keyframe(Frame{static_cast<Frame>(delay)},          0.0f, d.opacity_easing);
                op.add_keyframe(Frame{static_cast<Frame>(end_f)},          1.0f, EasingCurve{Easing::Hold});
            }

            // Optional slide-up
            if (d.slide_up) {
                auto& pos = l.position_anim();
                pos.add_keyframe(Frame{0},     Vec3{cx, d.base_pos.y + d.slide_up_px, d.base_pos.z}, EasingCurve{Easing::Hold});
                pos.add_keyframe(Frame{static_cast<Frame>(delay)}, Vec3{cx, d.base_pos.y + d.slide_up_px, d.base_pos.z}, d.position_easing);
                pos.add_keyframe(Frame{static_cast<Frame>(end_f)}, Vec3{cx, d.base_pos.y, d.base_pos.z}, EasingCurve{Easing::Linear});
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
            TextDefinition def;
            def.content.value           = ch;
            def.frame.placement                = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}};
            def.style.font.font_path          = d.font_spec.font_path;
            def.style.font.font_family        = d.font_spec.font_family;
            def.style.font.font_weight        = d.font_spec.font_weight;
            def.style.font.font_size          = d.font_size;
            def.frame.size              = {ch_w + pad, d.font_size * 1.8f};
            def.frame.anchor           = TextAnchor::Center;
            def.frame.align            = TextAlign::Center;
            def.frame.vertical_align   = VerticalAlign::Middle;
            def.frame.line_height      = 1.10f;
            def.frame.tracking         = 0.0f;
            def.style.color        = d.color;

            l.text("label", def);
        });
    }
}

// font_bold — production Poppins-Bold font spec.
FontSpec font_bold() {
    return FontSpec{"assets/fonts/Poppins-Bold.ttf", "Poppins", 700};
}

// font_regular — production Poppins-Regular font spec.
FontSpec font_regular() {
    return FontSpec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};
}

} // namespace chronon3d::content::text_reveal
