#pragma once

#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/text/glyph_selector.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace chronon3d {
struct TextRunLayout;
}

namespace chronon3d::runtime {

struct GlyphStatic;

struct GpuPreparedTextRun {
    struct Batch {
        RenderSurfaceHandle page{kInvalidRenderSurfaceHandle};
        std::vector<GlyphStatic> glyphs;
    };
    bool ready{false};
    std::vector<Batch> batches;
    std::shared_ptr<GlyphSelectorSpec> reveal_selector_spec;
    std::optional<CompiledSelector> reveal_selector;
};

class GpuTextRunPreparedCache {
public:
    [[nodiscard]] std::shared_ptr<const GpuPreparedTextRun> find(
        const TextRunLayout* layout) const {
        if (!layout) return nullptr;
        std::lock_guard lock(m_mutex);
        auto it = m_entries.find(layout);
        if (it != m_entries.end()) {
            return it->second;
        }
        return nullptr;
    }

    void store(const TextRunLayout* layout,
               std::shared_ptr<const GpuPreparedTextRun> prepared) {
        if (!layout || !prepared) return;
        std::lock_guard lock(m_mutex);
        m_entries[layout] = std::move(prepared);
    }

    void clear() noexcept {
        std::lock_guard lock(m_mutex);
        m_entries.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        std::lock_guard lock(m_mutex);
        return m_entries.size();
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_map<const TextRunLayout*, std::shared_ptr<const GpuPreparedTextRun>> m_entries;
};

} // namespace chronon3d::runtime
