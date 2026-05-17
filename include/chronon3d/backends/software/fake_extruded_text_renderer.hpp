#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/backends/text/text_renderer.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/math/vec2.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace chronon3d {

struct GlyphIsland {
    std::vector<std::vector<Vec2>> polygon;   // [0] = outer, [1..] = holes
    std::vector<uint32_t>          indices;
    std::vector<Vec2>              flat_verts;
};

struct GlyphGeometry {
    std::vector<GlyphIsland> islands;
    float                    advance{0.0f};
};

class FakeExtrudedTextRenderer {
public:
    // Standalone draw (used by modular graph path): self-contained per call.
    void draw(Framebuffer& fb, const RenderNode& node, const RenderState& state,
              const Camera& camera, i32 width, i32 height, TextRenderer& text_renderer,
              std::optional<FakeExtrudedTextRenderState> rt_override = std::nullopt);

    // Batched API (used by legacy render_layer_nodes): accumulate all FakeExtrudedText
    // primitives across objects in one frame, then flush with global depth sort.
    void begin_frame();
    void collect(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                 const Camera& camera, i32 width, i32 height, TextRenderer& text_renderer,
                 std::optional<FakeExtrudedTextRenderState> rt_override = std::nullopt);
    void flush(Framebuffer& fb);

    void clear_cache() { m_glyph_cache.clear(); }

private:
    struct SideQ { Vec2 v[4]; Color c0, c1; float depth; };
    struct Tri3D { Vec2 v[3]; Color colors[3]; float depth; };

    const GlyphGeometry& get_glyph(const std::string& font_path, float font_size,
                                   int codepoint, const uint8_t* font_data);

    void collect_geometry(const RenderNode& node, const RenderState& state,
                          i32 width, i32 height,
                          TextRenderer& text_renderer,
                          const renderer::Projector2_5D& projector);

    std::unordered_map<std::string, GlyphGeometry> m_glyph_cache;
    std::vector<SideQ> m_quads;
    std::vector<Tri3D> m_tris;
};

} // namespace chronon3d
