namespace {

GpuGlyphAtlasError atlas_error(
    GpuGlyphAtlasError::Code code, std::string message) {
    return GpuGlyphAtlasError{code, std::move(message)};
}

} // namespace

void GpuGlyphAtlas::attach(RenderSurfaceRegistry& registry,
                           graph::RenderBackend& backend) noexcept {
    std::unique_lock lock(m_mutex);
    m_cache.attach(registry, backend);
}

// Called with m_mutex already in unique_lock by the caller (acquire).
// Must NOT lock again — caller owns the write transaction.
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

    const PixelFormat page_format = glyph_representation_is_rgba(representation)
        ? PixelFormat::Rgba8Unorm
        : PixelFormat::R8Unorm;
    GpuAssetKey asset_key;
    // FIXING.md H-02/U-02 — page identity is (page_index, representation), not a content hash.
    {
        const std::uint64_t page_hash = XXH3_64bits_withSeed(
            &representation, sizeof(representation),
            static_cast<std::uint64_t>(m_pages.size()) * 0x9e3779b97f4a7c15ULL);
        asset_key.content_digest = assets::digest_from_u64(page_hash);
    }
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
    // A newly allocated page must be initialized before region uploads.  The
    // generic asset cache validates upload spans against the full surface,
    // so provide one zero page here; this happens once per atlas page, never
    // once per glyph or frame.  Without it the page was published
    // uninitialized and the first MTSDF glyph preparation fell back with an
    // "upload size does not match surface" error.
    const std::size_t page_pixels = static_cast<std::size_t>(kPageSize) * kPageSize;
    std::vector<float> zero_page(page_pixels, 0.0f);
    const auto result = m_cache.acquire(asset_key, desc, zero_page);
    if (!result.ok()) {
        const auto code = result.error.find("budget") != std::string::npos
            ? GpuGlyphAtlasError::Code::BudgetExceeded
            : result.error.find("not attached") != std::string::npos
                ? GpuGlyphAtlasError::Code::NotAttached
                : GpuGlyphAtlasError::Code::AllocationFailed;
        ++m_stats.failures;
        spdlog::warn("GpuGlyphAtlas::page_for_glyph: page allocation failed ({}): {}", static_cast<int>(code), result.error);
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
    // FIXING.md S-01 + AUDIT-2026-09-09 N-02 — fast-path read: hit is
    // shared_lock only (multiple readers concurrent). Miss upgrades to
    // unique_lock for the write path (page alloc + location insert); the
    // per-glyph driver upload now runs OUTSIDE the lock, so a miss burst
    // no longer serializes behind device calls.
    {
        std::shared_lock read_lock(m_mutex);
        if (const auto it = m_locations.find(key); it != m_locations.end()) {
            // AUDIT-2026-09-10 M-1 — the hit counter must not go dark under
            // exactly the traffic it exists to measure. Count the fast-path
            // hit with a relaxed atomic (no shared→exclusive upgrade) and
            // fold it into stats().hits at snapshot time.
            m_fast_path_hits.fetch_add(1, std::memory_order_relaxed);
            return it->second;
        }
        // Also validate fast-path invariants under read lock so we can
        // fail without upgrading when possible (not-attached, invalid dims).
        // Dimensions/coverage checks remain under write lock below to avoid
        // duplicate branching complexity.
        (void)read_lock;
    }
    std::unique_lock lock(m_mutex);
    // Re-check under write lock (another thread may have inserted).
    if (const auto it = m_locations.find(key); it != m_locations.end()) {
        ++m_stats.hits;
        return it->second;
    }

    if (!m_cache.backend()) {
        ++m_stats.failures;
        spdlog::warn("GpuGlyphAtlas::acquire: not attached (glyph_id={} rep={})", key.glyph_id, static_cast<int>(key.representation));
        return atlas_error(
            GpuGlyphAtlasError::Code::NotAttached,
            "GPU glyph atlas is not attached to a backend");
    }
    if (width == 0 || height == 0 || width > std::numeric_limits<std::uint16_t>::max() ||
        height > std::numeric_limits<std::uint16_t>::max()) {
        ++m_stats.failures;
        spdlog::warn("GpuGlyphAtlas::acquire: invalid dimensions {}x{} (glyph_id={})", width, height, key.glyph_id);
        return atlas_error(
            GpuGlyphAtlasError::Code::InvalidDimensions,
            "glyph dimensions must be non-zero and fit GlyphLocation");
    }

    const PixelFormat format = glyph_representation_is_rgba(key.representation)
        ? PixelFormat::Rgba8Unorm
        : PixelFormat::R8Unorm;
    const std::size_t pixel_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    // Rgba8 pages use one float-sized element per pixel as a transport
    // cell; its four bytes are copied verbatim into the Vulkan texel.
    const std::size_t expected_coverage = pixel_count;
    if (coverage.size() != expected_coverage) {
        ++m_stats.failures;
        spdlog::warn("GpuGlyphAtlas::acquire: coverage mismatch {} vs {} (glyph_id={})", coverage.size(), expected_coverage, key.glyph_id);
        return atlas_error(
            GpuGlyphAtlasError::Code::InvalidCoverage,
            "glyph coverage size does not match glyph dimensions");
    }

    ++m_stats.misses;

    if (width > kPageSize || height > kPageSize) {
        GpuAssetKey asset_key;
        asset_key.content_digest = assets::digest_from_u64(glyph_key_hash64(key));
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
            spdlog::warn("GpuGlyphAtlas::acquire: large glyph allocation failed ({}x{}, glyph_id={}): {}", width, height, key.glyph_id, result.error);
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
    // AUDIT-2026-09-09 N-02 — RESERVE the region under the lock (cursor
    // advances here, not after the upload) so concurrent misses can never
    // claim the same page area. On upload failure the reserved region is
    // leaked inside the page: bounded by one glyph per failure, and the
    // glyph is simply not cached (fail-loud to the caller) — the same
    // observable behaviour as before.
    page.cursor_x += width;
    page.row_height = std::max(page.row_height, height);

    const PixelFormat page_format = glyph_representation_is_rgba(key.representation)
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

    // AUDIT-2026-09-09 N-02 — the per-glyph driver upload runs OUTSIDE the
    // atlas mutex: the only state touched while unlocked is the backend
    // (which serializes its own surface API internally, see GpuAssetCache)
    // operating on a copied handle + desc + the caller-owned coverage span.
    // The rare page-creation device call inside page_for_glyph stays under
    // the lock (once per ~2048px page, not once per glyph).
    const RenderSurfaceHandle page_handle = page.handle;
    lock.unlock();
    auto uploaded = m_cache.backend()->upload_surface_region(
        page_handle, page_desc,
        static_cast<std::int32_t>(px), static_cast<std::int32_t>(py),
        width, height, coverage);
    lock.lock();

    if (!uploaded.ok()) {
        ++m_stats.failures;
        spdlog::warn("GpuGlyphAtlas::acquire: upload failed (page={}, {}x{} @ {},{}): {}", page_index, width, height, px, py, uploaded.error().message);
        return atlas_error(
            GpuGlyphAtlasError::Code::UploadFailed,
            "glyph atlas upload failed: " + uploaded.error().message);
    }

    // Another thread may have published the same key while this upload was
    // in flight: keep the first location, the duplicate region is wasted
    // space (bounded, same policy as the reservation note above).
    if (const auto it = m_locations.find(key); it != m_locations.end()) {
        ++m_stats.hits;
        return it->second;
    }
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
    std::shared_lock lock(m_mutex);
    const auto it = m_metrics.find(key);
    if (it == m_metrics.end()) return std::nullopt;
    return it->second;
}

std::optional<GlyphLocation> GpuGlyphAtlas::location(
    GpuGlyphKey key) const noexcept {
    std::shared_lock lock(m_mutex);
    const auto it = m_locations.find(key);
    if (it == m_locations.end()) return std::nullopt;
    return it->second;
}

std::optional<RenderSurfaceHandle> GpuGlyphAtlas::page_handle(
    std::uint16_t atlas_page) const noexcept {
    std::shared_lock lock(m_mutex);
    if (atlas_page >= m_pages.size()) return std::nullopt;
    const auto handle = m_pages[atlas_page].handle;
    if (handle == kInvalidRenderSurfaceHandle) return std::nullopt;
    return handle;
}

void GpuGlyphAtlas::clear() noexcept {
    std::unique_lock lock(m_mutex);
    m_locations.clear();
    m_metrics.clear();
    m_pages.clear();
    m_stats = {};
    m_fast_path_hits.store(0, std::memory_order_relaxed);
    m_cache.clear();
}

void GpuGlyphAtlas::set_budget_bytes(std::size_t budget_bytes) noexcept {
    m_cache.set_budget_bytes(budget_bytes);
}

GpuGlyphAtlasStats GpuGlyphAtlas::stats() const noexcept {
    std::shared_lock lock(m_mutex);
    auto snapshot = m_stats;
    snapshot.hits += m_fast_path_hits.load(std::memory_order_relaxed);
    return snapshot;
}
