#pragma once

#include <cstdint>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/text/glyph_selector.hpp>

namespace chronon3d::runtime {

// Prepared MTSDF GlyphStatic.flags keeps the existing 48-byte ABI. Bit 0 marks
// MTSDF; bits 1..15 carry the static index for the active Word or Line selector.
// 0x7fff is a sentinel for glyphs without an assignment.
inline constexpr std::uint16_t kMtsdfGlyphWordIndexShift = 1u;
inline constexpr std::uint16_t kMtsdfGlyphWordIndexMask = 0x7fffu;

// TextRunDynamic.flags bit 10 marks an active per-glyph animator fill color
// (animator_fill_color is the lerp target; `color` stays the lerp base). Bits
// 4..9 carry selector metadata and 0..3 the selector/placement flags, so the
// animator-fill marker lives above them and is preserved by
// pack_text_selector_metadata.
inline constexpr std::uint32_t kTextRunFlagAnimatorFill = 1u << 10;

// TextRunDynamic.flags bits 4..9 carry selector-unit/order metadata;
// bits 11..13 optionally carry the selector shape (enum + 1). Bits 0..3
// retain selector/placement/completion flags; bit 10 is animator fill color.
inline constexpr std::uint32_t kTextRunSelectorUnitShift = 4u;
inline constexpr std::uint32_t kTextRunSelectorUnitMask = 0x7u;
inline constexpr std::uint32_t kTextRunSelectorOrderShift = 7u;
inline constexpr std::uint32_t kTextRunSelectorOrderMask = 0x7u;
// Encoded as enum value + 1 so zero remains the legacy Square/Smooth marker.
inline constexpr std::uint32_t kTextRunSelectorShapeShift = 11u;
inline constexpr std::uint32_t kTextRunSelectorShapeMask = 0x7u;

[[nodiscard]] constexpr std::uint32_t pack_text_selector_metadata(
    std::uint32_t flags, std::uint32_t unit, std::uint32_t order) noexcept {
    constexpr std::uint32_t metadata_mask =
        (kTextRunSelectorUnitMask << kTextRunSelectorUnitShift) |
        (kTextRunSelectorOrderMask << kTextRunSelectorOrderShift);
    return (flags & ~metadata_mask) |
        ((unit & kTextRunSelectorUnitMask) << kTextRunSelectorUnitShift) |
        ((order & kTextRunSelectorOrderMask) << kTextRunSelectorOrderShift);
}

[[nodiscard]] constexpr std::uint32_t pack_text_selector_metadata(
    std::uint32_t flags, std::uint32_t unit, std::uint32_t order,
    chronon3d::TextSelectorShape shape) noexcept {
    constexpr std::uint32_t metadata_mask =
        (kTextRunSelectorUnitMask << kTextRunSelectorUnitShift) |
        (kTextRunSelectorOrderMask << kTextRunSelectorOrderShift) |
        (kTextRunSelectorShapeMask << kTextRunSelectorShapeShift);
    // Wiggly modulation has no GPU ABI fields yet. CPU/GPU route selection
    // rejects that selector before packing; if a low-level caller still packs
    // it, preserve a safe square marker rather than aliasing Smooth.
    const auto encoded_shape = shape == chronon3d::TextSelectorShape::Wiggly
        ? 1u
        : (static_cast<std::uint32_t>(shape) + 1u) & kTextRunSelectorShapeMask;
    return (flags & ~metadata_mask) |
        ((unit & kTextRunSelectorUnitMask) << kTextRunSelectorUnitShift) |
        ((order & kTextRunSelectorOrderMask) << kTextRunSelectorOrderShift) |
        (encoded_shape << kTextRunSelectorShapeShift);
}

[[nodiscard]] constexpr chronon3d::TextSelectorShape unpack_text_selector_shape(
    std::uint32_t flags) noexcept {
    const auto encoded =
        (flags >> kTextRunSelectorShapeShift) & kTextRunSelectorShapeMask;
    if (encoded == 0u) {
        return (flags & 4u) != 0u ? chronon3d::TextSelectorShape::Smooth
                                  : chronon3d::TextSelectorShape::Square;
    }
    const auto value = encoded - 1u;
    return value <= static_cast<std::uint32_t>(chronon3d::TextSelectorShape::Smooth)
        ? static_cast<chronon3d::TextSelectorShape>(value)
        : chronon3d::TextSelectorShape::Square;
}

[[nodiscard]] constexpr std::uint32_t pack_mtsdf_glyph_unit_flags(
    std::uint16_t existing_flags, std::uint32_t unit_index) noexcept {
    const auto encoded = unit_index < kMtsdfGlyphWordIndexMask
        ? static_cast<std::uint16_t>(unit_index)
        : kMtsdfGlyphWordIndexMask;
    return static_cast<std::uint16_t>(existing_flags & 1u) |
        (static_cast<std::uint32_t>(encoded) << kMtsdfGlyphWordIndexShift);
}

[[nodiscard]] constexpr std::uint32_t unpack_mtsdf_glyph_unit_index(
    std::uint16_t flags) noexcept {
    return (static_cast<std::uint32_t>(flags) >> kMtsdfGlyphWordIndexShift) &
        kMtsdfGlyphWordIndexMask;
}

[[nodiscard]] inline chronon3d::SelectorWeight gpu_text_selector_weight(
    const TextRunDynamic& run, std::uint32_t glyph_index) noexcept {
    const auto order = static_cast<chronon3d::TextSelectorOrder>(
        (run.flags >> kTextRunSelectorOrderShift) & kTextRunSelectorOrderMask);
    const chronon3d::SelectorWeightParams params{
        unpack_text_selector_shape(run.flags),
        run.selector_start, run.selector_end, 0.0f, run.selector_amount,
        run.selector_unit_count,
        chronon3d::detail::apply_selector_order(order, glyph_index,
                                                run.selector_unit_count, 0u),
        true, (run.flags & 2u) != 0u, (run.flags & 8u) != 0u};
    return chronon3d::selector_weight(params, glyph_index);
}

} // namespace chronon3d::runtime
