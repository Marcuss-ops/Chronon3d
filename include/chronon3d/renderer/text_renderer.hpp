#pragma once

#include <chronon3d/renderer/renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/renderer/framebuffer.hpp>
#include <chronon3d/scene/shape.hpp>

namespace chronon3d {

class TextRenderer {
public:
    /**
     * Renders text to the provided framebuffer.
     * Returns true on success, false if font loading or rendering fails.
     */
    bool draw_text(const TextShape& text, const Transform& tr, Framebuffer& fb);

private:
    /**
     * Helper to read a file into a buffer.
     */
    static bool read_font_file(const std::string& path, std::vector<unsigned char>& out);
};

} // namespace chronon3d
