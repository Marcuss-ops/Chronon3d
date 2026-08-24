#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_boundary_resolver.hpp — canonical text boundary contract
//
// Internal header — NOT part of the public API.  Third-party Unicode headers
// are intentionally confined to icu_boundary_resolver.cpp; callers consume
// only byte-offset boundary data through this interface.
// ═══════════════════════════════════════════════════════════════════════════

#include <cstddef>
#include <string_view>
#include <vector>

namespace chronon3d::text::boundary {

/// Locale/language hint used by dictionary-backed ICU boundary analysis.
/// Empty means the resolver uses its deterministic undetermined locale.
struct TextBoundaryOptions {
    std::string_view language{};
};

/// UTF-8 byte offsets returned by a boundary implementation.
/// Every list is sorted and contains the start offset (0) and, when the
/// corresponding iterator succeeds, the source end offset.
struct WordSegment {
    std::size_t byte_start{0};
    std::size_t byte_end{0};
    bool is_word{false};
};

struct BoundaryMap {
    std::vector<std::size_t> grapheme_boundaries;
    std::vector<std::size_t> word_boundaries;
    std::vector<WordSegment> word_segments;
    std::vector<std::size_t> line_boundaries;

    [[nodiscard]] bool is_grapheme_boundary(std::size_t byte_offset) const noexcept;
    [[nodiscard]] bool is_word_boundary(std::size_t byte_offset) const noexcept;
    [[nodiscard]] bool is_line_break(std::size_t byte_offset) const noexcept;
};

/// Single authority for character, word, and line boundaries in the text
/// pipeline.  Implementations return UTF-8 byte offsets so shaping and
/// composition never need to reason about ICU's UTF-16 indices.
class TextBoundaryResolver {
public:
    virtual ~TextBoundaryResolver() = default;

    [[nodiscard]] virtual BoundaryMap resolve(
        std::string_view utf8,
        const TextBoundaryOptions& options
    ) const = 0;
};

/// ICU BreakIterator-backed canonical implementation.  ICU types remain
/// private to the implementation translation unit; this adapter is the only
/// boundary authority consumed by the text pipeline.
class IcuBoundaryResolver final : public TextBoundaryResolver {
public:
    [[nodiscard]] BoundaryMap resolve(
        std::string_view utf8,
        const TextBoundaryOptions& options
    ) const override;
};

} // namespace chronon3d::text::boundary
