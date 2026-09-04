namespace {

std::string glyph_key_string(const GpuGlyphKey& key) {
    std::ostringstream out;
    out << key.font_path << '\x1f' << key.glyph_id << '\x1f'
        << key.variation_hash << '\x1f'
        << static_cast<int>(key.representation) << '\x1f'
        << key.generation_profile;
    if (key.representation == GlyphRepresentation::Coverage) {
        out << '\x1f' << key.font_size;
    }
    return out.str();
}

} // namespace

void GpuGlyphAtlas::attach(RenderSurfaceRegistry& registry,
                           graph::RenderBackend& backend) noexcept {
    m_cache.attach(registry, backend);
}

GpuGlyphAtlas::AtlasPage& GpuGlyphAtlas::current_page_or_new(
    GlyphRepresentation representation,
    const DistanceFieldProfile& profile) {
    for (auto& p : m_pages) {
        if (p.representation == representation &&
            p.profile == profile &&
            !(p.cursor_x + 64 > kPageSize &&
              p.cursor_y + p.row_height + 64 > kPageSize)) {
            return p;
        }
    }

    AtlasPage page;
    page.representation = representation;
    page.profile = profile;

    const PixelFormat page_format =
        (representation == GlyphRepresentation::Mtsdf || representation == GlyphRepresentation::Msdf)
            ? PixelFormat::Rgba8Unorm
            : PixelFormat::R8Unorm;

    GpuAssetKey asset_key;
    asset_key.content_digest = assets::sha256_string(
        "atlas_page_" + std::to_string(m_pages.size()) + "_" +
        std::to_string(static_cast<int>(representation)));
    asset_key.format = page_format;
    asset_key.width = kPageSize;
    asset_key.height = kPageSize;

    SurfaceDesc desc{kPageSize, kPageSize, page_format,
                     ResourceUsage::Storage, LifetimeClass::JobPersistent, 0};
    desc.text_atlas_encoding =
        representation == GlyphRepresentation::Mtsdf
            ? TextAtlasEncoding::MTSDF
            : representation == GlyphRepresentation::Coverage
                ? TextAtlasEncoding::Coverage
                : TextAtlasEncoding::PremultipliedRGBA;

    const auto result = m_cache.acquire(asset_key, desc, {});
    page.handle = result.handle;
    m_pages.push_back(std::move(page));
    ++m_stats.page_count;

    return m_pages.back();
}

GlyphLocation GpuGlyphAtlas::acquire(GpuGlyphKey key,
                                     std::uint32_t width, std::uint32_t height,
                                     std::span<const float> coverage,
                                     GpuGlyphMetrics metrics,
                                     const DistanceFieldProfile& profile) {
    std::lock_guard lock(m_mutex);

    {
        const auto it = m_locations.find(key);
        if (it != m_locations.end()) {
            ++m_stats.hits;
            return it->second;
        }
    }
    ++m_stats.misses;

    const PixelFormat format =
        (key.representation == GlyphRepresentation::Mtsdf || key.representation == GlyphRepresentation::Msdf)
            ? PixelFormat::Rgba8Unorm
            : PixelFormat::R8Unorm;

    if (width > kPageSize || height > kPageSize) {
        GpuAssetKey asset_key;
        asset_key.content_digest = assets::sha256_string(glyph_key_string(key));
        asset_key.format = format;
        asset_key.width = width;
        asset_key.height = height;

        SurfaceDesc desc{width, height, format,
                         ResourceUsage::Storage, LifetimeClass::JobPersistent, 0};
        desc.text_atlas_encoding =
            key.representation == GlyphRepresentation::Mtsdf
                ? TextAtlasEncoding::MTSDF
                : key.representation == GlyphRepresentation::Coverage
                    ? TextAtlasEncoding::Coverage
                    : TextAtlasEncoding::PremultipliedRGBA;
        const auto result = m_cache.acquire(asset_key, desc, coverage);

        GlyphLocation loc;
        loc.atlas_page = 0;
        loc.atlas_x = 0;
        loc.atlas_y = 0;
        loc.width = static_cast<std::uint16_t>(width);
        loc.height = static_cast<std::uint16_t>(height);
        loc.plane_left = static_cast<float>(metrics.x_offset);
        loc.plane_top = static_cast<float>(metrics.y_offset);
        loc.plane_right = static_cast<float>(metrics.x_offset + static_cast<int>(width));
        loc.plane_bottom = static_cast<float>(metrics.y_offset + static_cast<int>(height));
        loc.advance_x = metrics.advance_x;
        m_locations[key] = loc;
        m_metrics[key] = metrics;
        ++m_stats.entries;
        return loc;
    }

    auto& page = current_page_or_new(key.representation, profile);
    if (page.cursor_x + width > kPageSize) {
        page.cursor_x = 0;
        page.cursor_y += page.row_height;
        page.row_height = 0;
    }
    if (page.cursor_y + height > kPageSize) {
        AtlasPage new_page;
        new_page.representation = key.representation;
        new_page.profile = profile;

        GpuAssetKey asset_key;
        asset_key.content_digest = assets::sha256_string(
            "atlas_page_" + std::to_string(m_pages.size()) + "_" +
            std::to_string(static_cast<int>(key.representation)));
        asset_key.format = format;
        asset_key.width = kPageSize;
        asset_key.height = kPageSize;

        SurfaceDesc desc{kPageSize, kPageSize, format,
                         ResourceUsage::Storage, LifetimeClass::JobPersistent, 0};
        desc.text_atlas_encoding =
            key.representation == GlyphRepresentation::Mtsdf
                ? TextAtlasEncoding::MTSDF
                : key.representation == GlyphRepresentation::Coverage
                    ? TextAtlasEncoding::Coverage
                    : TextAtlasEncoding::PremultipliedRGBA;
        const auto result = m_cache.acquire(asset_key, desc, {});
        new_page.handle = result.handle;
        m_pages.push_back(std::move(new_page));
        ++m_stats.page_count;
        page = m_pages.back();
    }

    const std::uint32_t px = page.cursor_x;
    const std::uint32_t py = page.cursor_y;

    if (m_cache.backend() && !coverage.empty()) {
        SurfaceDesc page_desc{kPageSize, kPageSize, format,
                              ResourceUsage::Storage,
                              LifetimeClass::JobPersistent,
                              static_cast<std::size_t>(kPageSize) * kPageSize * (format == PixelFormat::Rgba8Unorm ? 4 : 1)};
        page_desc.text_atlas_encoding = page.profile.representation == GlyphRepresentation::Mtsdf
            ? TextAtlasEncoding::MTSDF
            : page.profile.representation == GlyphRepresentation::Coverage
                ? TextAtlasEncoding::Coverage
                : TextAtlasEncoding::PremultipliedRGBA;

        m_cache.backend()->upload_surface_region(
            page.handle, page_desc,
            static_cast<std::int32_t>(px), static_cast<std::int32_t>(py),
            width, height, coverage);
    }

    page.cursor_x += width;
    page.row_height = std::max(page.row_height, height);

    GlyphLocation loc;
    loc.atlas_page = static_cast<std::uint16_t>(m_pages.size() - 1);
    loc.uv_index = static_cast<std::uint16_t>(px | (py << 16));
    loc.atlas_x = static_cast<std::uint16_t>(px);
    loc.atlas_y = static_cast<std::uint16_t>(py);
    loc.local_x = static_cast<std::int16_t>(metrics.x_offset);
    loc.local_y = static_cast<std::int16_t>(metrics.y_offset);
    loc.width = static_cast<std::uint16_t>(width);
    loc.height = static_cast<std::uint16_t>(height);
    loc.plane_left = static_cast<float>(metrics.x_offset);
    loc.plane_top = static_cast<float>(metrics.y_offset);
    loc.plane_right = static_cast<float>(metrics.x_offset + static_cast<int>(width));
    loc.plane_bottom = static_cast<float>(metrics.y_offset + static_cast<int>(height));
    loc.advance_x = metrics.advance_x;

    m_locations[key] = loc;
    m_metrics[key] = metrics;
    ++m_stats.entries;
    m_stats.total_glyph_bytes += static_cast<std::size_t>(width) * height * (format == PixelFormat::Rgba8Unorm ? 4 : 1);

    return loc;
}

void GpuGlyphAtlas::prepare_batch(
    std::span<const GpuGlyphKey> keys,
    std::span<const std::uint32_t> widths,
    std::span<const std::uint32_t> heights,
    std::span<const float> coverage_data,
    std::span<const GpuGlyphMetrics> metrics,
    const DistanceFieldProfile& profile) {
    if (keys.size() != widths.size() || keys.size() != heights.size() ||
        keys.size() != metrics.size()) {
        return;
    }
    std::size_t coverage_offset = 0;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto w = widths[i];
        const auto h = heights[i];
        const auto count = static_cast<std::size_t>(w) * h;
        if (coverage_offset + count > coverage_data.size()) break;
        (void)acquire(keys[i], w, h,
                      coverage_data.subspan(coverage_offset, count),
                      metrics[i],
                      profile);
        coverage_offset += count;
    }
}

std::optional<GpuGlyphMetrics> GpuGlyphAtlas::metrics(GpuGlyphKey key) const noexcept {
    std::lock_guard lock(m_mutex);
    const auto it = m_metrics.find(key);
    if (it == m_metrics.end()) return std::nullopt;
    return it->second;
}

void GpuGlyphAtlas::clear() noexcept {
    std::lock_guard lock(m_mutex);
    m_locations.clear();
    m_metrics.clear();
    m_pages.clear();
    m_stats = {};
    m_cache.clear();
}

void GpuGlyphAtlas::set_budget_bytes(std::size_t budget_bytes) noexcept {
    m_cache.set_budget_bytes(budget_bytes);
}

GpuGlyphAtlasStats GpuGlyphAtlas::stats() const noexcept {
    std::lock_guard lock(m_mutex);
    return m_stats;
}
