namespace {

GpuGlyphAtlasError atlas_error(
    GpuGlyphAtlasError::Code code, std::string message) {
    return GpuGlyphAtlasError{code, std::move(message)};
}

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
    std::lock_guard lock(m_mutex);
    m_cache.attach(registry, backend);
}

Result<std::size_t, GpuGlyphAtlasError> GpuGlyphAtlas::page_for_glyph(
    GlyphRepresentation representation,
    const DistanceFieldProfile& profile,
    std::uint32_t width,
    std::uint32_t height) {
    for (std::size_t page_index = 0; page_index < m_pages.size(); ++page_index) {
        auto& page = m_pages[page_index];
        if (page.representation != representation || page.profile != profile) {
            continue;
        }
        const std::uint32_t next_x = page.cursor_x + width;
        const bool wraps = next_x > kPageSize && page.cursor_x != 0;
        const std::uint32_t candidate_x = wraps ? 0 : page.cursor_x;
        const std::uint32_t candidate_y = wraps
            ? page.cursor_y + page.row_height
            : page.cursor_y;
        if (candidate_x + width <= kPageSize && candidate_y + height <= kPageSize) {
            return page_index;
        }
    }

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

    // A page is a residency commitment. GpuAssetCache::acquire validates the
    // budget, attachment and backend allocation before this page is published.
    const auto result = m_cache.acquire(asset_key, desc, {});
    if (!result.ok()) {
        const auto code = result.error.find("budget") != std::string::npos
            ? GpuGlyphAtlasError::Code::BudgetExceeded
            : result.error.find("not attached") != std::string::npos
                ? GpuGlyphAtlasError::Code::NotAttached
                : GpuGlyphAtlasError::Code::AllocationFailed;
        ++m_stats.failures;
        return atlas_error(code, "glyph atlas page allocation failed: " + result.error);
    }

    AtlasPage page;
    page.handle = result.handle;
    page.representation = representation;
    page.profile = profile;
    m_pages.push_back(std::move(page));
    ++m_stats.page_count;
    return m_pages.size() - 1;
}

GpuGlyphAcquireResult GpuGlyphAtlas::acquire(
    GpuGlyphKey key,
    std::uint32_t width, std::uint32_t height,
    std::span<const float> coverage,
    GpuGlyphMetrics metrics,
    const DistanceFieldProfile& profile) {
    std::lock_guard lock(m_mutex);

    if (!m_cache.backend()) {
        ++m_stats.failures;
        return atlas_error(
            GpuGlyphAtlasError::Code::NotAttached,
            "GPU glyph atlas is not attached to a backend");
    }
    if (width == 0 || height == 0 || width > std::numeric_limits<std::uint16_t>::max() ||
        height > std::numeric_limits<std::uint16_t>::max()) {
        ++m_stats.failures;
        return atlas_error(
            GpuGlyphAtlasError::Code::InvalidDimensions,
            "glyph dimensions must be non-zero and fit GlyphLocation");
    }

    const PixelFormat format =
        (key.representation == GlyphRepresentation::Mtsdf || key.representation == GlyphRepresentation::Msdf)
            ? PixelFormat::Rgba8Unorm
            : PixelFormat::R8Unorm;
    const std::size_t expected_coverage =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (coverage.size() != expected_coverage) {
        ++m_stats.failures;
        return atlas_error(
            GpuGlyphAtlasError::Code::InvalidCoverage,
            "glyph coverage size does not match glyph dimensions");
    }

    if (const auto it = m_locations.find(key); it != m_locations.end()) {
        ++m_stats.hits;
        return it->second;
    }
    ++m_stats.misses;

    if (width > kPageSize || height > kPageSize) {
        GpuAssetKey asset_key;
        asset_key.content_digest = assets::sha256_string(glyph_key_string(key));
        asset_key.format = format;
        asset_key.width = width;
        asset_key.height = height;

        SurfaceDesc desc{width, height, format,
                         ResourceUsage::Storage, LifetimeClass::JobPersistent, 0};
        desc.bytes = tight_surface_bytes(format, width, height);
        desc.text_atlas_encoding =
            key.representation == GlyphRepresentation::Mtsdf
                ? TextAtlasEncoding::MTSDF
                : key.representation == GlyphRepresentation::Coverage
                    ? TextAtlasEncoding::Coverage
                    : TextAtlasEncoding::PremultipliedRGBA;
        const auto result = m_cache.acquire(asset_key, desc, coverage);
        if (!result.ok()) {
            ++m_stats.failures;
            const auto code = result.error.find("budget") != std::string::npos
                ? GpuGlyphAtlasError::Code::BudgetExceeded
                : GpuGlyphAtlasError::Code::AllocationFailed;
            return atlas_error(
                code, "large glyph allocation/upload failed: " + result.error);
        }

        GlyphLocation loc;
        loc.atlas_page = 0;
        loc.atlas_x = 0;
        loc.atlas_y = 0;
        loc.width = static_cast<std::uint16_t>(width);
        loc.height = static_cast<std::uint16_t>(height);
        loc.local_x = static_cast<std::int16_t>(metrics.x_offset);
        loc.local_y = static_cast<std::int16_t>(metrics.y_offset);
        loc.plane_left = static_cast<float>(metrics.x_offset);
        loc.plane_top = static_cast<float>(metrics.y_offset);
        loc.plane_right = static_cast<float>(metrics.x_offset + static_cast<int>(width));
        loc.plane_bottom = static_cast<float>(metrics.y_offset + static_cast<int>(height));
        loc.advance_x = metrics.advance_x;
        m_locations.emplace(key, loc);
        m_metrics.emplace(key, metrics);
        ++m_stats.entries;
        m_stats.total_glyph_bytes += static_cast<std::size_t>(width) * height *
            (format == PixelFormat::Rgba8Unorm ? 4 : 1);
        return loc;
    }

    auto page_result = page_for_glyph(key.representation, profile, width, height);
    if (!page_result.has_value()) {
        return page_result.error();
    }
    const std::size_t page_index = page_result.value();
    auto& page = m_pages[page_index];
    if (page.cursor_x + width > kPageSize && page.cursor_x != 0) {
        page.cursor_x = 0;
        page.cursor_y += page.row_height;
        page.row_height = 0;
    }
    const std::uint32_t px = page.cursor_x;
    const std::uint32_t py = page.cursor_y;

    const PixelFormat page_format =
        (key.representation == GlyphRepresentation::Mtsdf ||
         key.representation == GlyphRepresentation::Msdf)
            ? PixelFormat::Rgba8Unorm
            : PixelFormat::R8Unorm;
    SurfaceDesc page_desc{kPageSize, kPageSize, page_format,
                          ResourceUsage::Storage,
                          LifetimeClass::JobPersistent,
                          tight_surface_bytes(page_format, kPageSize, kPageSize)};
    page_desc.text_atlas_encoding = page.profile.representation == GlyphRepresentation::Mtsdf
        ? TextAtlasEncoding::MTSDF
        : page.profile.representation == GlyphRepresentation::Coverage
            ? TextAtlasEncoding::Coverage
            : TextAtlasEncoding::PremultipliedRGBA;

    auto uploaded = m_cache.backend()->upload_surface_region(
        page.handle, page_desc,
        static_cast<std::int32_t>(px), static_cast<std::int32_t>(py),
        width, height, coverage);
    if (!uploaded.ok()) {
        ++m_stats.failures;
        return atlas_error(
            GpuGlyphAtlasError::Code::UploadFailed,
            "glyph atlas upload failed: " + uploaded.error().message);
    }

    page.cursor_x += width;
    page.row_height = std::max(page.row_height, height);

    GlyphLocation loc;
    loc.atlas_page = static_cast<std::uint16_t>(page_index);
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

    m_locations.emplace(key, loc);
    m_metrics.emplace(key, metrics);
    ++m_stats.entries;
    m_stats.total_glyph_bytes += static_cast<std::size_t>(width) * height *
        (format == PixelFormat::Rgba8Unorm ? 4 : 1);
    return loc;
}

Result<std::size_t, GpuGlyphAtlasError> GpuGlyphAtlas::prepare_batch(
    std::span<const GpuGlyphKey> keys,
    std::span<const std::uint32_t> widths,
    std::span<const std::uint32_t> heights,
    std::span<const float> coverage_data,
    std::span<const GpuGlyphMetrics> metrics,
    const DistanceFieldProfile& profile) {
    if (keys.size() != widths.size() || keys.size() != heights.size() ||
        keys.size() != metrics.size()) {
        return atlas_error(
            GpuGlyphAtlasError::Code::InvalidDimensions,
            "glyph batch metadata spans have different lengths");
    }
    std::size_t coverage_offset = 0;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto w = widths[i];
        const auto h = heights[i];
        if (h != 0 && w > std::numeric_limits<std::size_t>::max() / h) {
            return atlas_error(
                GpuGlyphAtlasError::Code::InvalidDimensions,
                "glyph batch dimensions overflow");
        }
        const auto count = static_cast<std::size_t>(w) * h;
        if (coverage_offset > coverage_data.size() ||
            count > coverage_data.size() - coverage_offset) {
            return atlas_error(
                GpuGlyphAtlasError::Code::InvalidCoverage,
                "glyph batch coverage span is truncated");
        }
        auto result = acquire(
            keys[i], w, h,
            coverage_data.subspan(coverage_offset, count),
            metrics[i], profile);
        if (!result.has_value()) {
            return result.error();
        }
        coverage_offset += count;
    }
    return keys.size();
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
