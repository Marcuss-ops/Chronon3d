#pragma once

#include <doctest/doctest.h>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <content/text/text_helpers.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace test_text_quality {

inline std::filesystem::path test_repo_root() {
    std::filesystem::path src(__FILE__);
    // tests/text/test_text_quality_helpers.hpp -> repo root
    return std::filesystem::absolute(src).parent_path().parent_path().parent_path();
}

inline std::string bundled_font_path(const std::string& rel) {
    return (test_repo_root() / rel).string();
}

// `FontSpec`, `FontEngine`, `GlyphRun` all live in `chronon3d::` and the
// helpers below refer to them unqualified; an explicit using-directive
// scoped INSIDE this namespace keeps references short while preventing
// name pollution of any TU that transitively includes this header.
using namespace chronon3d;

inline FontSpec inter_bold_quality() {
    return FontSpec{
        .font_path = bundled_font_path("assets/fonts/Inter-Bold.ttf"),
        .font_family = "Inter",
        .font_weight = 700,
    };
}

inline bool require_font(FontEngine& engine, const FontSpec& spec = inter_bold_quality()) {
    if (!engine.can_load(spec)) {
        MESSAGE("Skipping: font not available (", spec.font_path, ")");
        return false;
    }
    return true;
}

inline float measure(FontEngine& engine, std::string_view text, float size = 32.0f) {
    return engine.measure_text(text, inter_bold_quality(), size);
}

inline std::optional<GlyphRun> shape(FontEngine& engine, std::string_view text,
                                     float size = 32.0f) {
    return engine.shape_text(text, inter_bold_quality(), size);
}

inline FontSpec noto_naskh_arabic_quality() {
    return FontSpec{
        .font_path = bundled_font_path("assets/fonts/NotoNaskhArabic-Bold.ttf"),
        .font_family = "Noto Naskh Arabic",
        .font_weight = 700,
    };
}

} // namespace test_text_quality
