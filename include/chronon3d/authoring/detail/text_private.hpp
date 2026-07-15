// Included inside chronon3d::authoring::Text private section.

friend class Layer;
friend class testing::TextRunBuilderInspector;

[[nodiscard]] PendingTextRun& mutable_pending() noexcept {
    return *pending_;
}

void apply_text_style(const chronon3d::TextStyle& style) {
    auto& spec = pending_->params.text;
    spec.font.font_path = style.font_path;
    spec.font.font_family = style.font_family;
    spec.font.font_weight = style.font_weight;
    spec.font.font_style = style.font_style;
    spec.font.font_size = style.size;
    spec.appearance.color = style.color;
    spec.layout.anchor = style.anchor;
    spec.layout.align = style.align;
    spec.layout.vertical_align = style.vertical_align;
    spec.layout.centering_mode = style.centering_mode;
    spec.layout.line_height = style.line_height;
    spec.layout.tracking = style.tracking;
    spec.layout.max_lines = style.max_lines;
    spec.layout.ellipsis = style.ellipsis;
    spec.layout.auto_fit = style.auto_fit || style.auto_scale;
    spec.layout.min_font_size = style.min_size;
    spec.layout.max_font_size = style.max_size;
    spec.layout.overflow = style.overflow;
    spec.layout.wrap = style.wrap;
    spec.appearance.paint = style.paint;
    spec.appearance.shadows = style.shadows;
    spec.appearance.box_style = style.box_style;
    spec.appearance.material = style.material;
    pending_->params.direction = style.shaping.direction;
    pending_->params.language = style.shaping.language;
    if (style.shaping.script != 0) {
        pending_->params.script = style.shaping.script;
    }
    if (style.pre_shaped) {
        spec.content.pre_shaped = style.pre_shaped;
    }
}

PendingTextRun* pending_;
const CanvasInfo* canvas_;
const StyleRegistry* style_registry_;
const MotionRegistry* motion_registry_;
ResolutionOutcome last_style_outcome_{ResolutionOutcome::NotAttempted};
ResolutionOutcome last_motion_outcome_{ResolutionOutcome::NotAttempted};
