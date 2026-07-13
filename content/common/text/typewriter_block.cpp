#include "content/common/text/typewriter_block.hpp"

#include "content/common/text/glyph_layout.hpp"  // measure_text_width
#include "content/common/text/text_reveal.hpp"   // build_text_reveal_line, TextRevealDescriptor, font_regular

#include <algorithm>  // std::max

namespace chronon3d::content::text_reveal {

// build_2line_typewriter — implementation
//
// Per AGENTS.md `### C++ default-arg uniqueness per TU`, the default args
// are declared ONLY in the .hpp; this .cpp definition is bare.
void build_2line_typewriter(SceneBuilder& s,
                             const std::string& line1, f32 size1,
                             const std::string& line2, f32 size2,
                             f32 start_delay_2,
                             f32 line_spacing,
                             bool slide_up,
                             f32 glow_intensity,
                             f32 base_y,
                             f32 tracking,
                             Color text_color,
                             Color shadow_color) {
    auto spec = font_regular();
    f32 w1 = measure_text_width(line1, size1, spec, tracking, *s.font_engine());
    f32 w2 = measure_text_width(line2, size2, spec, tracking, *s.font_engine());
    f32 max_w = std::max(w1, w2);
    f32 ref_x = -max_w * 0.5f;

    auto d1 = TextRevealDescriptor{
        .text = line1, .font_size = size1, .font_spec = spec,
        .tracking = tracking, .ref_offset_x = ref_x,
        .base_pos = {0.0f, base_y - line_spacing * 0.5f, 0.0f},
        .start_delay = 0.0f, .duration = 8.0f, .stagger = 2.0f,
        .slide_up = slide_up, .pin_to_center = true,
        .color = text_color, .add_shadow = true, .shadow_color = shadow_color,
        .glow_intensity = glow_intensity,
        .layer_prefix = "ch_0"
    };
    build_text_reveal_line(s, d1);

    auto d2 = d1;
    d2.text = line2;
    d2.font_size = size2;
    d2.base_pos = {0.0f, base_y + line_spacing * 0.5f, 0.0f};
    d2.start_delay = start_delay_2;
    d2.layer_prefix = "ch_1";
    build_text_reveal_line(s, d2);
}

} // namespace chronon3d::content::text_reveal
