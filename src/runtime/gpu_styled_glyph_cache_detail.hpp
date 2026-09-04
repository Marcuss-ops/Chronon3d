namespace {

std::pair<std::uint64_t, std::uint64_t> styled_hash(std::string_view key_bytes) {
    std::uint64_t h0 = 0xcbf29ce484222325ULL;
    std::uint64_t h1 = 0x100000001b3ULL;
    for (char c : key_bytes) {
        h0 ^= static_cast<unsigned char>(c);
        h0 *= 0x100000001b3ULL;
        h1 = (h1 << 5) + h1 + static_cast<unsigned char>(c);
    }
    return {h0, h1};
}

} // namespace

std::shared_ptr<const GpuStyledGlyphCache::StyledGlyphBitmap>
GpuStyledGlyphCache::find_styled(std::string_view key_bytes) const {
    const auto [h0, h1] = styled_hash(key_bytes);
    Key key{h0, h1};
    std::lock_guard lock(m_mutex);
    const auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        ++m_stats.cache_misses;
        return {};
    }
    ++m_stats.cache_hits;
    return std::make_shared<const StyledGlyphBitmap>(it->second.bitmap);
}

void GpuStyledGlyphCache::store_styled(
    std::string_view key_bytes, std::uint32_t width, std::uint32_t height,
    std::shared_ptr<const std::vector<float>> rgba) {
    if (!rgba || width == 0 || height == 0) return;
    const auto [h0, h1] = styled_hash(key_bytes);
    Key key{h0, h1};
    std::lock_guard lock(m_mutex);
    if (m_entries.find(key) != m_entries.end()) return;
    ++m_stats.acquire_calls;
    m_entries.emplace(key, Entry{{width, height, std::move(rgba)}});
}

void GpuStyledGlyphCache::clear() noexcept {
    std::lock_guard lock(m_mutex);
    m_entries.clear();
    m_stats = {};
}

std::size_t GpuStyledGlyphCache::size() const noexcept {
    std::lock_guard lock(m_mutex);
    return m_entries.size();
}

GpuStyledGlyphCache::Stats GpuStyledGlyphCache::stats() const noexcept {
    std::lock_guard lock(m_mutex);
    return m_stats;
}
