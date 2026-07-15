// Included inside chronon3d::authoring::Text public section.

Text& at(Vec3 position) {
    pending_->params.text.placement = TextPlacement{
        TextPlacementKind::Absolute, {position.x, position.y}};
    return *this;
}

Text& at(Vec2 position) {
    pending_->params.text.placement = TextPlacement{
        TextPlacementKind::Absolute, position};
    return *this;
}

Text& at(f32 x, f32 y) {
    pending_->params.text.placement = TextPlacement{
        TextPlacementKind::Absolute, {x, y}};
    return *this;
}

Text& center() {
    assert(canvas_ && "Text::center(): CanvasInfo must be supplied by Layer");
    place(TextPlacement{TextPlacementKind::CanvasCenter}, TextAnchor::Center);
    auto& layout = pending_->params.text.layout;
    layout.align = TextAlign::Center;
    layout.vertical_align = VerticalAlign::Middle;
    return *this;
}

Text& place(TextPlacement placement) {
    return place(std::move(placement), TextAnchor::Center);
}

Text& place(TextPlacement placement, TextAnchor anchor) {
    assert(canvas_ && "Text::place(): CanvasInfo must be supplied by Layer");
    const Vec2 pin = resolve_placement_origin(
        *canvas_, pending_->params.text.layout.box, placement);
    pending_->params.text.placement = TextPlacement{
        TextPlacementKind::Absolute, pin};
    pending_->params.text.layout.anchor = anchor;
    return *this;
}

Text& box(Vec2 size) {
    pending_->params.text.layout.box = size;
    return *this;
}

Text& anchor_point(TextAnchor value) {
    pending_->params.text.layout.anchor = value;
    return *this;
}

Text& align(TextAlign value) {
    pending_->params.text.layout.align = value;
    return *this;
}

Text& vertical_align(VerticalAlign value) {
    pending_->params.text.layout.vertical_align = value;
    return *this;
}

Text& pixel_ink_centering() {
    pending_->params.text.layout.centering_mode = TextCenteringMode::PixelInk;
    return *this;
}

Text& layout_box_centering() {
    pending_->params.text.layout.centering_mode = TextCenteringMode::LayoutBox;
    return *this;
}

Text& line_height(f32 value) {
    pending_->params.text.layout.line_height = value;
    return *this;
}

Text& tracking(f32 pixels) {
    pending_->params.text.layout.tracking = pixels;
    return *this;
}

Text& wrap(TextWrap value) {
    pending_->params.text.layout.wrap = value;
    return *this;
}

Text& overflow(TextOverflow value) {
    pending_->params.text.layout.overflow = value;
    return *this;
}

Text& ellipsis(bool value = true) {
    pending_->params.text.layout.ellipsis = value;
    return *this;
}

Text& max_lines(int count) {
    pending_->params.text.layout.max_lines = count;
    return *this;
}

Text& auto_fit(f32 minimum_size, int maximum_lines) {
    auto& layout = pending_->params.text.layout;
    layout.auto_fit = true;
    layout.min_font_size = minimum_size;
    layout.max_lines = maximum_lines;
    return *this;
}

Text& max_font_size(f32 value) {
    pending_->params.text.layout.max_font_size = value;
    return *this;
}
