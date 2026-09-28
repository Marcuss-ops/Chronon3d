#pragma once

#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace chronon3d::scene {

enum class PathSampleCommandType : std::uint8_t { MoveTo, LineTo, QuadraticTo, CubicTo, Close };

struct PathSampleCommand {
    PathSampleCommandType type{PathSampleCommandType::MoveTo};
    Vec2 point{0.0f};
    Vec2 control1{0.0f};
    Vec2 control2{0.0f};
};

/// Immutable arc-length lookup table for a 2D line/quadratic/cubic path.
/// Curves are flattened deterministically with a fixed subdivision count;
/// sampling, point and tangent queries are pure functions of the table/u.
class PathArcLengthTable {
public:
    explicit PathArcLengthTable(const std::vector<PathSampleCommand>& commands,
                                std::uint32_t curve_subdivisions = 64) {
        if (commands.empty() || curve_subdivisions == 0 || curve_subdivisions > 4096)
            throw std::invalid_argument("path arc-length table requires commands and 1..4096 subdivisions");
        Vec2 current{0.0f};
        Vec2 subpath_start{0.0f};
        bool has_current = false;
        for (const auto& command : commands) {
            switch (command.type) {
                case PathSampleCommandType::MoveTo:
                    if (!points_.empty())
                        throw std::invalid_argument("path arc-length sampling accepts one continuous subpath");
                    validate(command.point);
                    current = subpath_start = command.point;
                    has_current = true;
                    append(current);
                    break;
                case PathSampleCommandType::LineTo:
                    require_current(has_current);
                    validate(command.point);
                    append(command.point);
                    current = command.point;
                    break;
                case PathSampleCommandType::QuadraticTo: {
                    require_current(has_current);
                    validate(command.point); validate(command.control1);
                    const Vec2 start = current;
                    for (std::uint32_t i = 1; i <= curve_subdivisions; ++i) {
                        const float t = static_cast<float>(i) / static_cast<float>(curve_subdivisions);
                        const float s = 1.0f - t;
                        append(s * s * start + 2.0f * s * t * command.control1 + t * t * command.point);
                    }
                    current = command.point;
                    break;
                }
                case PathSampleCommandType::CubicTo: {
                    require_current(has_current);
                    validate(command.point); validate(command.control1); validate(command.control2);
                    const Vec2 start = current;
                    for (std::uint32_t i = 1; i <= curve_subdivisions; ++i) {
                        const float t = static_cast<float>(i) / static_cast<float>(curve_subdivisions);
                        const float s = 1.0f - t;
                        append(s * s * s * start + 3.0f * s * s * t * command.control1 +
                               3.0f * s * t * t * command.control2 + t * t * t * command.point);
                    }
                    current = command.point;
                    break;
                }
                case PathSampleCommandType::Close:
                    require_current(has_current);
                    append(subpath_start);
                    current = subpath_start;
                    has_current = false;
                    break;
            }
        }
        if (points_.size() < 2 || !(total_length_ > 0.0f))
            throw std::invalid_argument("path arc-length table requires non-zero path length");
    }

    [[nodiscard]] float length() const noexcept { return total_length_; }

    [[nodiscard]] Vec2 point_at(float progress, bool clamp_end = true) const {
        const float u = clamp_end ? std::clamp(progress, 0.0f, 1.0f) : progress;
        const float distance = u * total_length_;
        if (distance < 0.0f) return points_.front().point + tangent_at(0.0f) * distance;
        if (distance > total_length_)
            return points_.back().point + tangent_at(1.0f) * (distance - total_length_);
        const auto upper = std::upper_bound(points_.begin(), points_.end(), distance,
            [](float value, const Sample& sample) { return value < sample.distance; });
        if (upper == points_.begin()) return upper->point;
        if (upper == points_.end()) return points_.back().point;
        const auto& right = *upper;
        const auto& left = *(upper - 1);
        const float span = right.distance - left.distance;
        if (span <= 1.0e-8f) return right.point;
        return glm::mix(left.point, right.point, (distance - left.distance) / span);
    }

    [[nodiscard]] Vec2 tangent_at(float progress) const {
        const float distance = std::clamp(progress, 0.0f, 1.0f) * total_length_;
        auto upper = std::upper_bound(points_.begin(), points_.end(), distance,
            [](float value, const Sample& sample) { return value < sample.distance; });
        if (upper == points_.begin()) upper = points_.begin() + 1;
        if (upper == points_.end()) upper = points_.end() - 1;
        for (auto it = upper; it != points_.begin(); --it) {
            const Vec2 delta = it->point - (it - 1)->point;
            const float magnitude = glm::length(delta);
            if (std::isfinite(magnitude) && magnitude > 1.0e-7f) return delta / magnitude;
        }
        for (auto it = upper; it != points_.end(); ++it) {
            const Vec2 delta = (it + 1)->point - it->point;
            const float magnitude = glm::length(delta);
            if (std::isfinite(magnitude) && magnitude > 1.0e-7f) return delta / magnitude;
        }
        return {1.0f, 0.0f};
    }

private:
    struct Sample { Vec2 point; float distance; };

    static void require_current(bool has_current) {
        if (!has_current) throw std::invalid_argument("path segment requires an open subpath");
    }
    static void validate(Vec2 point) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            throw std::invalid_argument("path coordinates must be finite");
    }
    void append(Vec2 point) {
        validate(point);
        if (!points_.empty()) total_length_ += glm::length(point - points_.back().point);
        points_.push_back({point, total_length_});
    }

    std::vector<Sample> points_;
    float total_length_{0.0f};
};

}  // namespace chronon3d::scene
