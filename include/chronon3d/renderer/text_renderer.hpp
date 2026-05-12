#pragma once

#include <chronon3d/scene/shape.hpp>
#include <chronon3d/renderer/framebuffer.hpp>

namespace chronon3d {

class TextRenderer {
public:
    /**
     * Renders text to the provided framebuffer.
     * Returns true on success, false if font loading or rendering fails.
     */
    bool draw_text(const TextShape& text, Framebuffer& framebuffer);

private:
    /**
     * Helper to read a file into a buffer.
     */
    static bool read_font_file(const std::string& path, std::vector<unsigned char>& out);
};

} // namespace chronon3d
