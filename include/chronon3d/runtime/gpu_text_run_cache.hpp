#pragma once

#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/text_run_layout.hpp>
#include <chronon3d/cache/lru_cache.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace chronon3d::runtime {

struct GlyphStatic;

struct GpuPreparedTextRun {
    struct Batch {
        RenderSurfaceHandle page{kInvalidRenderSurfaceHandle};
        std::vector<GlyphStatic> glyphs;
    };
    bool ready{false};
    std::vector<Batch> batches;
    std::optional<CompiledSelector> reveal_selector;
};

class GpuTextRunPreparedCache {
public:
    static constexpr std::size_t kDefaultCapacity = 4096;
    static constexpr std::size_t kShardCount = 16;

    GpuTextRunPreparedCache()
        : m_entries(kDefaultCapacity, kShardCount, cache::CapacityMode::Count) {}

    [[nodiscard]] std::shared_ptr<const GpuPreparedTextRun> find(
        const TextRunLayout& layout, std::uint8_t quality = 0) const {
        const auto value = m_entries.get(cache_key(layout, quality));
        if (!value || !*value) {
            return nullptr;
        }
        return *value;
    }

    void store(const TextRunLayout& layout, std::uint8_t quality,
               std::shared_ptr<const GpuPreparedTextRun> prepared) {
        if (!prepared) return;
        // The key is the immutable layout fingerprint plus the glyph-generation
        // policy. High and Fast must never alias one prepared atlas batch.
        m_entries.put(cache_key(layout, quality), std::move(prepared));
    }

    void store(const TextRunLayout& layout,
               std::shared_ptr<const GpuPreparedTextRun> prepared) {
        store(layout, 0, std::move(prepared));
    }

    void clear() noexcept {
        m_entries.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return m_entries.stats().current_size;
    }

private:
    [[nodiscard]] static std::uint64_t cache_key(
        const TextRunLayout& layout, std::uint8_t quality) noexcept {
        constexpr std::uint64_t kQualitySalt = 0x9e3779b97f4a7c15ULL;
        return layout.layout_hash() ^
            (static_cast<std::uint64_t>(quality) * kQualitySalt);
    }

    using Cache = cache::LruCache<
        std::uint64_t, std::shared_ptr<const GpuPreparedTextRun>>;
    mutable Cache m_entries;
};

} // namespace chronon3d::runtime
