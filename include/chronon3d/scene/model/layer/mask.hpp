#pragma once

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/path_commands.hpp>
#include <chronon3d/graphics/field2d.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace chronon3d {

enum class MaskType {
    None,
    Rect,
    RoundedRect,
    Circle,
    Path,
    Ellipse,
    Field2D,
    Text
};

enum class MaskCombineMode : std::uint8_t { Add, Subtract, Intersect, Difference };

struct MaskOperation {
    MaskCombineMode mode{MaskCombineMode::Add};
    MaskType type{MaskType::None};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    Vec2 size{0.0f, 0.0f};
    f32 radius{0.0f};
    std::vector<PathCommand> path;
    std::optional<graphics::Field2D> field;
    std::string text_source;
    f32 feather{0.0f};
    f32 expansion{0.0f};
    f32 opacity{1.0f};
    bool inverted{false};
};

struct Mask {
    MaskType type{MaskType::None};
    // Combination rule for the primary shape against the implicit layer mask.
    // Subtract starts from full layer coverage; the other modes use the
    // primary shape as their first coverage.
    MaskCombineMode initial_mode{MaskCombineMode::Add};

    // Forward-protection: lock `type` as a public field (not a method).
    // Many call sites historically invoked `mask.type()` as a method;
    // `Shape::type()` IS a method (variant getter), but `Mask::type` is
    // a plain field. This static_assert makes a future regression a
    // compile-time error rather than a silent rot cleanup.
    static_assert(std::is_member_object_pointer_v<decltype(&Mask::type)>,
        "Mask::type must remain a public data member (field).");
    Vec3 pos{0.0f, 0.0f, 0.0f};
    Vec2 size{0.0f, 0.0f};
    f32  radius{0.0f};
    std::vector<PathCommand> path;
    std::optional<graphics::Field2D> field;
    std::string text_source;
    f32 feather{0.0f};
    f32 expansion{0.0f};
    f32 opacity{1.0f};
    bool inverted{false};
    // Additional ordered shapes are composited with the legacy primary mask.
    // Keeping the primary fields preserves source and binary-facing APIs.
    std::vector<MaskOperation> operations;

    [[nodiscard]] bool enabled() const { return type != MaskType::None || !operations.empty(); }
};

// Params used in LayerBuilder API
struct RectMaskParams {
    Vec2 size{100.0f, 100.0f};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    bool inverted{false};
    f32 feather{0.0f};
    f32 expansion{0.0f};
};

struct RoundedRectMaskParams {
    Vec2 size{100.0f, 100.0f};
    f32  radius{8.0f};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    bool inverted{false};
    f32 feather{0.0f};
    f32 expansion{0.0f};
};

struct CircleMaskParams {
    f32  radius{50.0f};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    bool inverted{false};
    f32 feather{0.0f};
    f32 expansion{0.0f};
};

struct EllipseMaskParams {
    Vec2 size{100.0f, 100.0f};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    f32 feather{0.0f};
    f32 expansion{0.0f};
    bool inverted{false};
};

struct PathMaskParams {
    std::vector<PathCommand> commands;
    Vec3 pos{0.0f, 0.0f, 0.0f};
    f32 feather{0.0f};
    f32 expansion{0.0f};
    bool inverted{false};
};

} // namespace chronon3d
