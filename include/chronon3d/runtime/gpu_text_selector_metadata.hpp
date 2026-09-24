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

// TextRunDynamic.flags bits 4..6 carry selector-unit metadata for host-side
// per-glyph weighting. Bits 0..3 retain their existing selector/placement flags.
inline constexpr std::uint32_t kTextRunSelectorUnitShift = 4u;
inline constexpr std::uint32_t kTextRunSelectorUnitMask = 0x7u;
inline constexpr std::uint32_t kTextRunSelectorOrderShift = 7u;
inline constexpr std::uint32_t kTextRunSelectorOrderMask = 0x7u;

[[nodiscard]] constexpr std::uint32_t pack_text_selector_metadata(
    std::uint32_t flags, std::uint32_t unit, std::uint32_t order) noexcept {
    constexpr std::uint32_t metadata_mask =
        (kTextRunSelectorUnitMask << kTextRunSelectorUnitShift) |
        (kTextRunSelectorOrderMask << kTextRunSelectorOrderShift);
    return (flags & ~metadata_mask) |
        ((unit & kTextRunSelectorUnitMask) << kTextRunSelectorUnitShift) |
        ((order & kTextRunSelectorOrderMask) << kTextRunSelectorOrderShift);
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
        (run.flags & 4u) != 0u ? chronon3d::TextSelectorShape::Smooth
                               : chronon3d::TextSelectorShape::Square,
        run.selector_start, run.selector_end, 0.0f, run.selector_amount,
        run.selector_unit_count,
        chronon3d::detail::apply_selector_order(order, glyph_index,
                                                run.selector_unit_count, 0u),
        true, (run.flags & 2u) != 0u, (run.flags & 8u) != 0u};
    return chronon3d::selector_weight(params, glyph_index);
}

} // namespace chronon3d::runtime
