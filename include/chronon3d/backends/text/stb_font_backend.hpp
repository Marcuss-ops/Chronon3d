#pragma once

#include <chronon3d/backends/text/font_backend.hpp>
#include <unordered_map>
#include <vector>

namespace chronon3d::text {

class StbFontBackend final : public FontBackend {
public:
    ~StbFontBackend() override = default;

    bool load_font(const std::string& path) override;
    const std::vector<u8>* get_font_data(const std::string& path) override;
    float get_char_advance(const std::string& path, char c, float size) override;
    std::unique_ptr<GlyphBitmap> render_glyph(const std::string& path, char c, float size, GlyphMetrics& out_metrics) override;
    const std::vector<u8>* get_font_data(const std::string& path) const override;

private:
    struct FontData {
        std::vector<u8> bytes;
        // The stbtt_fontinfo will be initialized on demand or during load.
    };
    std::unordered_map<std::string, FontData> m_fonts;
};

} // namespace chronon3d::text
