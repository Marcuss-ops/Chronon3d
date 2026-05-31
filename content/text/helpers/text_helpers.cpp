#include "text_helpers.hpp"

namespace chronon3d::content::text {

void apply_text(LayerBuilder& l, const std::string& name, const TextDef& d,
                Vec2 sz, Vec3 pos) {
    l.text(name, TextParams{
        .text = d.text,
        .size = sz,
        .pos = pos,
        .font_path = d.font_path,
        .font_family = d.font_family,
        .font_weight = d.font_weight,
        .font_style = d.font_style,
        .font_size = d.size,
        .color = d.color,
        .align = d.align,
        .vertical_align = d.vertical_align,
        .line_height = d.line_height,
        .tracking = d.tracking,
        .box_style = d.box_style,
        .paint = d.paint,
        .shadows = d.shadows,
        .auto_fit = d.auto_fit,
        .max_lines = d.max_lines,
        .ellipsis = d.ellipsis,
        .min_font_size = d.min_font_size,
        .max_font_size = d.max_font_size,
        .overflow = d.overflow,
        .wrap = d.wrap,
    });
}

} // namespace chronon3d::content::text
