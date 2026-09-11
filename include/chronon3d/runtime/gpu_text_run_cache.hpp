#pragma once

#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/text_run_layout.hpp>
#include <chronon3d/cache/lru_cache.hpp>

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
        const TextRunLayout& layout) const {
        const auto value = m_entries.get(layout.layout_hash());
        if (!value || !*value) {
            return nullptr;
        }
        return *value;
    }

    void store(const TextRunLayout& layout,
               std::shared_ptr<const GpuPreparedTextRun> prepared) {
        if (!prepared) return;
        // The key is the immutable layout fingerprint, never its address.
        m_entries.put(layout.layout_hash(), std::move(prepared));
    }

    void clear() noexcept {
        m_entries.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return m_entries.stats().current_size;
    }

private:
    using Cache = cache::LruCache<
        std::uint64_t, std::shared_ptr<const GpuPreparedTextRun>>;
    mutable Cache m_entries;
};

} // namespace chronon3d::runtime
