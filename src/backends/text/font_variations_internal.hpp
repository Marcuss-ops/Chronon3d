// SPDX-License-Identifier: MIT
#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace chronon3d::text_internal {

struct FontVariationCoordinate {
    FT_ULong tag{0};
    float value{0.0f};
};

inline bool parse_font_variation_axes(
    std::string_view input,
    std::vector<FontVariationCoordinate>& out) {
    out.clear();
    if (!input.empty() && input.back() == ',') return false;
    while (!input.empty()) {
        const auto comma = input.find(',');
        auto item = input.substr(0, comma);
        input = comma == std::string_view::npos ? std::string_view{} : input.substr(comma + 1);
        while (!item.empty() && std::isspace(static_cast<unsigned char>(item.front()))) item.remove_prefix(1);
        while (!item.empty() && std::isspace(static_cast<unsigned char>(item.back()))) item.remove_suffix(1);
        if (item.size() < 6 || item[4] != '=') return false;
        const auto tag_text = item.substr(0, 4);
        for (const char c : tag_text) {
            if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e) return false;
        }
        auto value_text = item.substr(5);
        while (!value_text.empty() && std::isspace(static_cast<unsigned char>(value_text.front()))) value_text.remove_prefix(1);
        while (!value_text.empty() && std::isspace(static_cast<unsigned char>(value_text.back()))) value_text.remove_suffix(1);
        float value = 0.0f;
        const auto parsed = std::from_chars(value_text.data(), value_text.data() + value_text.size(),
                                            value, std::chars_format::general);
        if (value_text.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != value_text.data() + value_text.size() || !std::isfinite(value)) return false;

        const FT_ULong tag = FT_MAKE_TAG(tag_text[0], tag_text[1], tag_text[2], tag_text[3]);
        auto existing = std::find_if(out.begin(), out.end(), [tag](const auto& axis) {
            return axis.tag == tag;
        });
        if (existing == out.end()) out.push_back({tag, value});
        else existing->value = value;
    }
    return true;
}

/// Applies requested design coordinates, starting from the font defaults so
/// omitted axes never leak coordinates from an earlier call. Returns the
/// actual, range-clamped values through `effective` for the HarfBuzz font.
inline bool apply_font_variation_coordinates(
    FT_Library library,
    FT_Face face,
    const std::vector<FontVariationCoordinate>& requested,
    std::vector<FontVariationCoordinate>* effective = nullptr) {
    FT_MM_Var* mm_var = nullptr;
    if (FT_Get_MM_Var(face, &mm_var) != 0) return requested.empty();
    struct MmVarGuard {
        FT_Library library;
        FT_MM_Var* value;
        ~MmVarGuard() { if (value) FT_Done_MM_Var(library, value); }
    } guard{library, mm_var};

    std::vector<FT_Fixed> coordinates;
    coordinates.reserve(mm_var->num_axis);
    for (FT_UInt i = 0; i < mm_var->num_axis; ++i) coordinates.push_back(mm_var->axis[i].def);
    std::vector<FontVariationCoordinate> clamped;
    clamped.reserve(requested.size());
    for (const auto& variation : requested) {
        FT_UInt axis_index = mm_var->num_axis;
        for (FT_UInt i = 0; i < mm_var->num_axis; ++i) {
            if (mm_var->axis[i].tag == variation.tag) { axis_index = i; break; }
        }
        if (axis_index == mm_var->num_axis) return false;
        const auto& axis = mm_var->axis[axis_index];
        const double value = std::clamp(static_cast<double>(variation.value),
            static_cast<double>(axis.minimum) / 65536.0,
            static_cast<double>(axis.maximum) / 65536.0);
        coordinates[axis_index] = static_cast<FT_Fixed>(std::llround(value * 65536.0));
        clamped.push_back({variation.tag, static_cast<float>(value)});
    }
    if (FT_Set_Var_Design_Coordinates(face, mm_var->num_axis, coordinates.data()) != 0) return false;
    if (effective) *effective = std::move(clamped);
    return true;
}

inline bool apply_font_variation_axes(FT_Library library, FT_Face face,
                                     std::string_view axes) {
    std::vector<FontVariationCoordinate> requested;
    return parse_font_variation_axes(axes, requested) &&
           apply_font_variation_coordinates(library, face, requested);
}

} // namespace chronon3d::text_internal
