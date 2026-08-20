struct FontEngine::Impl {
    const chronon3d::assets::AssetResolver* m_resolver{nullptr};
    std::unique_ptr<chronon3d::content::text::TypewriterLayoutCache>
        typewriter_layout_cache;
    ~Impl() = default;

    explicit Impl(const chronon3d::assets::AssetResolver* resolver)
        : m_resolver(resolver),
          typewriter_layout_cache(
              std::make_unique<chronon3d::content::text::TypewriterLayoutCache>()) {}
};

FontEngine::FontEngine(const chronon3d::assets::AssetResolver& resolver)
    : m_impl(std::make_unique<Impl>(&resolver)),
      m_text_layout_cache(std::make_unique<TextLayoutCache>()) {}

FontEngine::~FontEngine() = default;
FontEngine::FontEngine(FontEngine&&) noexcept = default;
FontEngine& FontEngine::operator=(FontEngine&&) noexcept = default;

std::optional<GlyphRun> FontEngine::shape_text(
    std::string_view, const FontSpec&, float, const TextShaping&) const {
    return std::nullopt;
}

float FontEngine::measure_text(
    std::string_view, const FontSpec&, float, const TextShaping&) const {
    return 0.0f;
}

FontEngine::FontMetrics FontEngine::get_font_metrics(
    const FontSpec&, float) const {
    return FontMetrics{};
}

void FontEngine::clear_cache() {}
size_t FontEngine::glyph_bbox_cache_size() const { return 0; }
bool FontEngine::can_load(const FontSpec&) { return false; }

TextLayoutCache& FontEngine::text_layout_cache() noexcept {
    return *m_text_layout_cache;
}
