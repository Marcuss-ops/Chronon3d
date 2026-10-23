#pragma once

#include <chronon3d/scene/model/shape/path.hpp>

#include <clipper2/clipper.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::path_operator_detail {

inline constexpr int kClipperPrecision = 4;
inline constexpr std::size_t kPathGeometryLimit = 1000000;
inline constexpr double kPathCurveTolerance = 0.05;

struct FlattenedContour {
    std::vector<Clipper2Lib::PointD> points;
    bool closed{false};
};

[[nodiscard]] std::vector<FlattenedContour> flatten_path_for_geometry(const PathShape& path);
[[nodiscard]] Clipper2Lib::FillRule clipper_fill_rule(PathFillRule rule);
[[nodiscard]] Clipper2Lib::PathsD closed_paths(const std::vector<FlattenedContour>& contours,
                                                bool close_open_contours = false);
void append_closed_paths(std::vector<PathCommand>& commands, const Clipper2Lib::PathsD& paths);
void append_offset_open_contour(std::vector<PathCommand>& commands,
                                const FlattenedContour& contour, double amount, double miter_limit);
void transform_point(Vec2& point, const RepeaterParams& params, std::uint32_t copy_index);
void multiply_opacity(PathShape& path, float factor);

[[nodiscard]] PathShape round_corners(const PathShape& source, float radius);
[[nodiscard]] PathShape zig_zag(const PathShape& source, const ZigZagParams& params);
[[nodiscard]] PathShape offset_path(const PathShape& source, const OffsetPathParams& params);
[[nodiscard]] PathShape boolean_union(const PathShape& source, const BooleanUnionParams& params);
[[nodiscard]] PathShape pucker_bloat(const PathShape& source, const PuckerBloatParams& params);
[[nodiscard]] PathShape twist_path(const PathShape& source, const TwistParams& params);
[[nodiscard]] PathShape morph_path(const PathShape& source, const MorphPathParams& params);
[[nodiscard]] PathShape trim_path(const PathShape& source, const TrimPathParams& params);
[[nodiscard]] PathShape wiggle_path(const PathShape& source, const WigglePathParams& params, std::uint32_t seed);
[[nodiscard]] std::optional<std::string> validate_morph_path_topology_impl(
    const std::vector<PathCommand>& source, const std::vector<PathCommand>& target);
void apply_repeater(PathShape& path, const RepeaterParams& params, std::uint32_t copy_index);

} // namespace chronon3d::path_operator_detail
