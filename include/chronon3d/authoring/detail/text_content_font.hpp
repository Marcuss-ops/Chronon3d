// Included inside chronon3d::authoring::Text public section.

Text(PendingTextRun& pending, const CanvasInfo* canvas,
     const StyleRegistry* style_registry = nullptr,
     const MotionRegistry* motion_registry = nullptr) noexcept
    : pending_(&pending),
      canvas_(canvas),
      style_registry_(style_registry),
      motion_registry_(motion_registry) {}

Text(const Text&) = delete;
Text& operator=(const Text&) = delete;
Text(Text&&) = default;
Text& operator=(Text&&) = default;

Text& id(std::string value) {
    pending_->name = std::move(value);
    return *this;
}

Text& content(std::string value) {
    pending_->params.text.content.value = std::move(value);
    return *this;
}

Text& pre_shaped(std::shared_ptr<PlacedGlyphRun> shaped) {
    pending_->params.text.content.pre_shaped = std::move(shaped);
    return *this;
}

Text& font(std::string font_path, f32 size) {
    pending_->params.text.font.font_path = std::move(font_path);
    pending_->params.text.font.font_size = size;
    return *this;
}

Text& font(assets::FontRef ref, f32 size) {
    return font(ref.path(), size);
}

Text& font_family(std::string family) {
    pending_->params.text.font.font_family = std::move(family);
    return *this;
}

Text& weight(int value) {
    pending_->params.text.font.font_weight = value;
    return *this;
}

Text& italic(bool value = true) {
    pending_->params.text.font.font_style = value ? "italic" : "normal";
    return *this;
}

Text& font_size(f32 size) {
    pending_->params.text.font.font_size = size;
    return *this;
}

Text& font_engine(FontEngine* engine) {
    pending_->font_engine = engine;
    return *this;
}
