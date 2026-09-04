#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/typewriter_layout_cache.hpp>
#include <chronon3d/text/text_layout_cache.hpp>
#include "src/backends/text/font_engine_internal.hpp"

namespace chronon3d {

namespace {
bool is_invisible_codepoint(char32_t cp) noexcept {
    if (cp <= 0x1F || (cp >= 0x7F && cp <= 0x9F)) return true;
    if (cp == 0x20 || cp == 0xA0) return true;
    if (cp == 0x2028 || cp == 0x2029) return true;
    if (cp >= 0x2000 && cp <= 0x200D) return true;
    if (cp == 0x202F || cp == 0x205F || cp == 0x2060) return true;
    return cp == 0xFEFF;
}
} // namespace

#ifndef CHRONON3D_ENABLE_TEXT
#include "font_engine_stub_detail.hpp"

namespace text::font_engine_internal {
bool has_glyph_for_codepoint(FontEngine&, const FontSpec&, char32_t cp) {
    return is_invisible_codepoint(cp);
}
} // namespace text::font_engine_internal
#endif

} // namespace chronon3d
