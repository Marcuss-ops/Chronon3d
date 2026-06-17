#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/arc_length_table.hpp
//
// Pre-computed arc-length lookup table for trajectory parameterization.
//
// Each entry stores (cumulative_length, parametric_t) so that the inverse
// lookup parameter_at_fraction(fraction) does a binary-search + interpolation
// instead of a naive s/total division.
//
// The table is built by sampling every segment's ACTUAL curve (not just the
// straight-line distance between control points), using the segment samplers.
// ==============================================================================
#include <vector>
#include <algorithm>
#include <cstddef>

namespace chronon3d::camera_v1 {

/// Entry in the arc-length LUT: maps cumulative arc-length → parametric t.
struct ArcLengthEntry {
    float cumulative_length{0.0f};  ///< Arc length from trajectory start to this sample.
    float parameter{0.0f};          ///< Normalised trajectory parameter t ∈ [0, 1].
};

/// Cumulative arc-length LUT with proper inverse (binary-search + interpolation).
class ArcLengthTable {
public:
    ArcLengthTable() = default;

    /// Build from pre-computed entries (must be monotonically increasing).
    explicit ArcLengthTable(std::vector<ArcLengthEntry> entries)
        : entries_(std::move(entries)) {}

    /// Total arc length of the trajectory.
    float total() const noexcept {
        return entries_.empty() ? 0.0f : entries_.back().cumulative_length;
    }

    /// Map a normalised arc-length fraction f ∈ [0, 1] to parametric t ∈ [0, 1].
    /// Uses lower_bound + linear interpolation between adjacent entries.
    float parameter_at_fraction(float fraction) const {
        if (entries_.empty()) return 0.0f;
        const float total_len = entries_.back().cumulative_length;
        if (total_len < 1e-6f) return 0.0f;

        const float wanted = std::clamp(fraction, 0.0f, 1.0f) * total_len;

        // Binary search for the first entry whose cumulative_length >= wanted.
        auto it = std::lower_bound(
            entries_.begin(), entries_.end(), wanted,
            [](const ArcLengthEntry& e, float val) { return e.cumulative_length < val; });

        if (it == entries_.begin()) return entries_.front().parameter;
        if (it == entries_.end())   return entries_.back().parameter;

        const auto hi = it;
        const auto lo = it - 1;
        const float seg_len = hi->cumulative_length - lo->cumulative_length;
        const float frac = (seg_len > 1e-6f)
            ? (wanted - lo->cumulative_length) / seg_len
            : 0.0f;

        return lo->parameter + frac * (hi->parameter - lo->parameter);
    }

    /// Map an absolute arc-length `s` (>= 0) to parametric t ∈ [0, 1].
    /// Convenience wrapper around parameter_at_fraction.
    float t_from_arc_length(float s) const {
        if (entries_.empty()) return 0.0f;
        const float total_len = entries_.back().cumulative_length;
        if (total_len < 1e-6f) return 0.0f;
        return parameter_at_fraction(s / total_len);
    }

    const std::vector<ArcLengthEntry>& entries() const noexcept { return entries_; }

private:
    std::vector<ArcLengthEntry> entries_;
};

} // namespace chronon3d::camera_v1
