#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/render_graph/render_node.hpp>
#include <chronon3d/renderer/text/text_renderer.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/math/vec2.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace chronon3d {

struct GlyphIsland {
    std::vector<std::vector<Vec2>> polygon;
    std::vector<uint32_t>          indices;
    std::vector<Vec2>              flat_verts;
};

struct GlyphGeometry {
    std::vector<GlyphIsland> islands;
    float                    advance{0.0f};
};

class FakeExtrudedTextRenderer {
public:
    void draw(
        Framebuffer& fb,
        const RenderNode& node,
        const RenderState& state,
        const Camera& camera,
        i32 width,
        i32 height,
        TextRenderer& text_renderer
    );

    void clear_cache() { m_glyph_cache.clear(); }

private:
    const GlyphGeometry& get_glyph(const std::string& font_path, float font_size,
                                   int codepoint, const uint8_t* font_data);

    std::unordered_map<std::string, GlyphGeometry> m_glyph_cache;
};

} // namespace chronon3d
