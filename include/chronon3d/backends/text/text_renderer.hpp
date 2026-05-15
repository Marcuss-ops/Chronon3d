#pragma once

#include <chronon3d/backends/software/renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/backends/assets/font_cache.hpp>

namespace chronon3d {

class TextRenderer {
public:
    void set_backend(std::shared_ptr<text::FontBackend> backend) { m_cache.set_backend(std::move(backend)); }
    // state is optional — when non-null, mask clipping is applied per pixel.
    bool draw_text(const TextShape& text, const Transform& tr, Framebuffer& fb,
                   const RenderState* state = nullptr);

    // Returns raw font bytes for mesh-based rendering; null if not found.
    const CachedFont* get_font(const std::string& path) {
        return m_cache.get_or_load(path);
    }

    void clear_cache() { m_cache.clear(); }

private:
    FontCache m_cache;
};

} // namespace chronon3d
