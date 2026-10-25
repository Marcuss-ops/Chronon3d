#include <chronon3d/scene/model/shape/path.hpp>
#include "path_operators_internal.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>
#include <variant>

namespace chronon3d {

std::optional<std::string> validate_morph_path_topology(
    const std::vector<PathCommand>& source,
    const std::vector<PathCommand>& target) {
    return path_operator_detail::validate_morph_path_topology_impl(source, target);
}

std::vector<PathShape> apply_path_operators(const PathShape& input,
    const std::vector<PathOperatorSpec>& operators) {
    using namespace path_operator_detail;
    std::vector<PathShape> paths{input};
    for (const auto& op : operators) {
        if (op.kind == PathOperatorKind::RoundCorners) {
            if (!std::holds_alternative<RoundCornersParams>(op.params))
                throw std::invalid_argument("round-corners parameters do not match operator kind");
            for (auto& path : paths) path = round_corners(path, std::get<RoundCornersParams>(op.params).radius);
        } else if (op.kind == PathOperatorKind::ZigZag) {
            if (!std::holds_alternative<ZigZagParams>(op.params))
                throw std::invalid_argument("zig-zag parameters do not match operator kind");
            for (auto& path : paths) path = zig_zag(path, std::get<ZigZagParams>(op.params));
        } else if (op.kind == PathOperatorKind::OffsetPath) {
            if (!std::holds_alternative<OffsetPathParams>(op.params))
                throw std::invalid_argument("offset-path parameters do not match operator kind");
            for (auto& path : paths) path = offset_path(path, std::get<OffsetPathParams>(op.params));
        } else if (op.kind == PathOperatorKind::PuckerBloat) {
            if (!std::holds_alternative<PuckerBloatParams>(op.params))
                throw std::invalid_argument("pucker-bloat parameters do not match operator kind");
            for (auto& path : paths) path = pucker_bloat(path, std::get<PuckerBloatParams>(op.params));
        } else if (op.kind == PathOperatorKind::Twist) {
            if (!std::holds_alternative<TwistParams>(op.params))
                throw std::invalid_argument("twist parameters do not match operator kind");
            for (auto& path : paths) path = twist_path(path, std::get<TwistParams>(op.params));
        } else if (op.kind == PathOperatorKind::WigglePath) {
            if (!std::holds_alternative<WigglePathParams>(op.params))
                throw std::invalid_argument("wiggle-path parameters do not match operator kind");
            for (auto& path : paths) path = wiggle_path(path, std::get<WigglePathParams>(op.params), op.seed);
        } else if (op.kind == PathOperatorKind::Trim) {
            if (!std::holds_alternative<TrimPathParams>(op.params))
                throw std::invalid_argument("trim-path parameters do not match operator kind");
            for (auto& path : paths) path = trim_path(path, std::get<TrimPathParams>(op.params));
        } else if (op.kind == PathOperatorKind::Morph) {
            if (!std::holds_alternative<MorphPathParams>(op.params))
                throw std::invalid_argument("morph parameters do not match operator kind");
            for (auto& path : paths) path = morph_path(path, std::get<MorphPathParams>(op.params));
        } else if (op.kind == PathOperatorKind::Merge) {
            if (!std::holds_alternative<MergeParams>(op.params))
                throw std::invalid_argument("merge parameters do not match operator kind");
            const auto& merge = std::get<MergeParams>(op.params);
            std::size_t appended = 0;
            for (const auto& subpath : merge.paths) {
                if (subpath.empty() || subpath.front().type != PathCommandType::MoveTo)
                    throw std::invalid_argument("each merged subpath must begin with move_to");
                if (subpath.size() > 1000000 - appended)
                    throw std::invalid_argument("compound path exceeds the geometry limit");
                appended += subpath.size();
            }
            for (auto& path : paths) {
                if (path.commands.size() > 1000000 - appended)
                    throw std::invalid_argument("compound path exceeds the geometry limit");
                for (const auto& subpath : merge.paths)
                    path.commands.insert(path.commands.end(), subpath.begin(), subpath.end());
            }
        } else if (op.kind == PathOperatorKind::BooleanUnion ||
                   op.kind == PathOperatorKind::BooleanDifference ||
                   op.kind == PathOperatorKind::BooleanIntersection ||
                   op.kind == PathOperatorKind::BooleanXor) {
            if (!std::holds_alternative<BooleanUnionParams>(op.params))
                throw std::invalid_argument("boolean path parameters do not match operator kind");
            auto params = std::get<BooleanUnionParams>(op.params);
            switch (op.kind) {
                case PathOperatorKind::BooleanUnion: params.operation = PathBooleanOperation::Union; break;
                case PathOperatorKind::BooleanDifference: params.operation = PathBooleanOperation::Difference; break;
                case PathOperatorKind::BooleanIntersection: params.operation = PathBooleanOperation::Intersection; break;
                case PathOperatorKind::BooleanXor: params.operation = PathBooleanOperation::Xor; break;
                default: break;
            }
            for (auto& path : paths) path = boolean_union(path, params);
        } else if (op.kind == PathOperatorKind::Repeater) {
            if (!std::holds_alternative<RepeaterParams>(op.params))
                throw std::invalid_argument("repeater parameters do not match operator kind");
            const auto& repeater = std::get<RepeaterParams>(op.params);
            if (repeater.copies == 0 || repeater.copies > 10000 ||
                !std::isfinite(repeater.opacity_decay) || repeater.opacity_decay < 0.0f)
                throw std::invalid_argument("invalid repeater parameters");
            std::vector<PathShape> expanded;
            expanded.reserve(paths.size() * repeater.copies);
            for (const auto& source : paths) {
                for (std::uint32_t index = 0; index < repeater.copies; ++index) {
                    PathShape copy = source;
                    apply_repeater(copy, repeater, index);
                    expanded.push_back(std::move(copy));
                }
            }
            paths = std::move(expanded);
        } else {
            throw std::invalid_argument("unsupported path operator kind");
        }
    }
    return paths;
}

} // namespace chronon3d
