#include "text_boundary_resolver.hpp"

#include <algorithm>

namespace chronon3d::text::boundary {

namespace {

[[nodiscard]] bool contains(
    const std::vector<std::size_t>& boundaries,
    std::size_t byte_offset
) noexcept {
    return std::binary_search(boundaries.begin(), boundaries.end(), byte_offset);
}

} // namespace

bool BoundaryMap::is_grapheme_boundary(std::size_t byte_offset) const noexcept {
    return contains(grapheme_boundaries, byte_offset);
}

bool BoundaryMap::is_word_boundary(std::size_t byte_offset) const noexcept {
    return contains(word_boundaries, byte_offset);
}

bool BoundaryMap::is_line_break(std::size_t byte_offset) const noexcept {
    return contains(line_boundaries, byte_offset);
}

} // namespace chronon3d::text::boundary
