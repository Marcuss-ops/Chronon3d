// Included inside chronon3d::authoring::Text private section.

friend class Layer;
friend class testing::TextRunBuilderInspector;

[[nodiscard]] PendingTextRun& mutable_pending() noexcept {
    return *pending_;
}

void apply_text_style(const chronon3d::TextStyle& style) {
    auto& spec = pending_->params;
    spec.style.font.font_path = style.font_path;
    spec.style.font.font_family = style.font_family;
    spec.style.font.font_weight = style.font_weight;
    spec.style.font.font_style = style.font_style;
    spec.style.font.font_size = style.size;
    spec.style.color = style.color;
    spec.frame.anchor = style.anchor;
    spec.frame.align = style.align;
    spec.frame.vertical_align = style.vertical_align;
    spec.frame.centering_mode = style.centering_mode;
    spec.frame.line_height = style.line_height;
    spec.frame.tracking = style.tracking;
    spec.frame.max_lines = style.max_lines;
    spec.frame.ellipsis = style.ellipsis;
    spec.frame.auto_fit = style.auto_fit || style.auto_scale;
    spec.frame.min_font_size = style.min_size;
    spec.frame.max_font_size = style.max_size;
    spec.frame.overflow = style.overflow;
    spec.frame.wrap = style.wrap;
    spec.style.paint = style.paint;
    spec.style.shadows = style.shadows;
    spec.style.box_style = style.box_style;
    spec.style.material = style.material;
    pending_->params.shaping.direction = style.shaping.direction;
    pending_->params.shaping.language = style.shaping.language;
    if (style.shaping.script != 0) {
        pending_->params.shaping.script = style.shaping.script;
    }
    if (style.pre_shaped) {
        spec.document.defaults.content.pre_shaped = style.pre_shaped;
    }
}

PendingTextRun* pending_;
const CanvasInfo* canvas_;
const StyleRegistry* style_registry_;
const MotionRegistry* motion_registry_;
ResolutionOutcome last_style_outcome_{ResolutionOutcome::NotAttempted};
ResolutionOutcome last_motion_outcome_{ResolutionOutcome::NotAttempted};
