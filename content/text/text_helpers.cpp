#include "text_helpers.hpp"

namespace chronon3d::content::text {

void apply_text(LayerBuilder& l, const std::string& name, const TextDef& d,
                Vec2 sz, Vec3 pos) {
    l.text(name, TextParams{
        .text = d.text,
        .size = sz,
        .pos = pos,
        .font_family = "Inter",
        .font_weight = 800,
        .font_style = "normal",
        .font_size = d.size,
        .color = d.color,
        .align = d.align,
        .vertical_align = d.vertical_align,
        .line_height = d.line_height,
        .tracking = d.tracking,
    });
}

} // namespace chronon3d::content::text
