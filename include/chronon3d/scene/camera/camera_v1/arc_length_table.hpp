#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/arc_length_table.hpp
//
// Pre-computed arc-length lookup table for trajectory parameterization.
// Lives in the public header (small, no PIMPL required) so any TU that needs
// to destroy the table can see the full type — no ODR pitfalls.
// ==============================================================================
#include <vector>
#include <algorithm>

namespace chronon3d::camera_v1 {

/// Cumulative arc-length LUT. monotonically increasing entries.
class ArcLengthTable {
public:
    ArcLengthTable() = default;
    explicit ArcLengthTable(std::vector<float> cumulative)
        : cum_(std::move(cumulative)) {}

    float total() const noexcept { return cum_.empty() ? 0.0f : cum_.back(); }

    /// Map arc-length `s` (>=0) to normalized t01 in [0,1] along the trajectory.
    float t_from_arc_length(float s) const {
        if (cum_.empty()) return 0.0f;
        float total = cum_.back();
        if (total < 1e-6f) return 0.0f;
        return std::clamp(s / total, 0.0f, 1.0f);
    }

    const std::vector<float>& raw() const noexcept { return cum_; }

private:
    std::vector<float> cum_;
};

} // namespace chronon3d::camera_v1
