#include "svg_text_baseline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

#if defined(CHRONON3D_ENABLE_TEXT)
#include <hb.h>
#include <hb-ot.h>
#endif

namespace chronon3d::render_plan::detail {

std::optional<float> svg_script_baseline_offset(
    const std::filesystem::path& font_path, float font_size,
    std::string_view text, std::string_view baseline) {
#if defined(CHRONON3D_ENABLE_TEXT)
    if (font_path.empty() || text.empty() ||
        text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !std::isfinite(font_size) || font_size <= 0.0f || font_size > 100000.0f) {
        return std::nullopt;
    }

    hb_ot_layout_baseline_tag_t tag{};
    if (baseline == "use-script") {
        tag = HB_OT_LAYOUT_BASELINE_TAG_ROMAN;
    } else if (baseline == "hanging") {
        tag = HB_OT_LAYOUT_BASELINE_TAG_HANGING;
    } else if (baseline == "ideographic") {
        tag = HB_OT_LAYOUT_BASELINE_TAG_IDEO_EMBOX_BOTTOM_OR_LEFT;
    } else if (baseline == "mathematical") {
        tag = HB_OT_LAYOUT_BASELINE_TAG_MATH;
    } else if (baseline == "central") {
        tag = HB_OT_LAYOUT_BASELINE_TAG_IDEO_EMBOX_CENTRAL;
    } else if (baseline == "text-before-edge") {
        tag = HB_OT_LAYOUT_BASELINE_TAG_IDEO_EMBOX_TOP_OR_RIGHT;
    } else if (baseline == "text-after-edge") {
        tag = HB_OT_LAYOUT_BASELINE_TAG_IDEO_EMBOX_BOTTOM_OR_LEFT;
    } else {
        return std::nullopt;
    }

    const std::string path = font_path.string();
    using Blob = std::unique_ptr<hb_blob_t, decltype(&hb_blob_destroy)>;
    using Face = std::unique_ptr<hb_face_t, decltype(&hb_face_destroy)>;
    using Font = std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)>;
    using Buffer = std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)>;
    Blob blob(hb_blob_create_from_file(path.c_str()), hb_blob_destroy);
    if (!blob || hb_blob_get_length(blob.get()) == 0) return std::nullopt;
    Face face(hb_face_create(blob.get(), 0), hb_face_destroy);
    if (!face || hb_face_get_upem(face.get()) == 0) return std::nullopt;
    Font font(hb_font_create(face.get()), hb_font_destroy);
    if (!font) return std::nullopt;
    hb_ot_font_set_funcs(font.get());
    const auto pixel_size = static_cast<unsigned int>(std::ceil(font_size));
    const auto scale = static_cast<int>(std::min<std::uint64_t>(
        static_cast<std::uint64_t>(pixel_size) * 64u,
        static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
    hb_font_set_scale(font.get(), scale, scale);
    hb_font_set_ppem(font.get(), pixel_size, pixel_size);

    Buffer buffer(hb_buffer_create(), hb_buffer_destroy);
    if (!buffer) return std::nullopt;
    hb_buffer_add_utf8(buffer.get(), text.data(), static_cast<int>(text.size()),
                       0, static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(buffer.get());
    hb_script_t script = hb_buffer_get_script(buffer.get());
    if (script == HB_SCRIPT_INVALID) script = HB_SCRIPT_LATIN;
    hb_direction_t direction = hb_buffer_get_direction(buffer.get());
    if (!HB_DIRECTION_IS_HORIZONTAL(direction)) direction = HB_DIRECTION_LTR;
    hb_language_t language = hb_buffer_get_language(buffer.get());
    if (language == HB_LANGUAGE_INVALID) language = hb_language_get_default();
    if (baseline == "use-script")
        tag = hb_ot_layout_get_horizontal_baseline_tag_for_script(script);

    hb_position_t roman = 0;
    hb_position_t selected = 0;
    hb_ot_layout_get_baseline_with_fallback2(
        font.get(), HB_OT_LAYOUT_BASELINE_TAG_ROMAN, direction,
        script, language, &roman);
    hb_ot_layout_get_baseline_with_fallback2(
        font.get(), tag, direction, script, language, &selected);
    const float offset = static_cast<float>(selected - roman) / 64.0f;
    if (!std::isfinite(offset) || std::abs(offset) > font_size * 4.0f)
        return std::nullopt;
    return offset;
#else
    (void)font_path;
    (void)font_size;
    (void)text;
    (void)baseline;
    return std::nullopt;
#endif
}

} // namespace chronon3d::render_plan::detail
