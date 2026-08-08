// Included inside chronon3d::authoring::Text public section.

Text& color(Color value) {
    pending_->params.style.color = value;
    return *this;
}

Text& paint(TextPaint value) {
    pending_->params.style.paint = std::move(value);
    return *this;
}

Text& shadows(std::vector<TextShadow> values) {
    pending_->params.style.shadows = std::move(values);
    return *this;
}

Text& box_style(TextBoxStyle value) {
    pending_->params.style.box_style = std::move(value);
    return *this;
}

Text& direction(TextDirection value) {
    pending_->params.shaping.direction = value;
    return *this;
}

Text& language(std::string bcp47) {
    pending_->params.shaping.language = std::move(bcp47);
    return *this;
}

Text& script(std::uint32_t value) & {
    pending_->params.shaping.script = value;
    return *this;
}

Text& material(Material material_in) {
    pending_->params.style.material = std::move(material_in).release();
    return *this;
}

Text& animate(Animator animator_in) {
    pending_->params.animation.animators.emplace_back(std::move(animator_in).release());
    return *this;
}
