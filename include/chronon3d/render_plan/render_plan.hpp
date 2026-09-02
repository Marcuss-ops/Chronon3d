#pragma once

#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/render_plan/render_budget.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace chronon3d::render_plan {

inline constexpr const char* kRenderPlanSchemaV2 = "chronon.render-plan.v2";

enum class LayerType : std::uint8_t { Image, Video, Text, Color };
enum class FitMode : std::uint8_t { Cover, Contain, Stretch, None };
enum class OutputFormat : std::uint8_t { Png, Mp4, Mkv, WebM };
enum class VideoCodec : std::uint8_t { Auto, H264, H265, VP9, AV1 };
enum class RateControlMode : std::uint8_t { Crf, ConstantQp, Bitrate };

struct CanvasSpec {
    int width{0};
    int height{0};
    FrameRate fps{30, 1};
    Frame duration{0};
};

struct StrokeStyle {
    std::string color;
    std::optional<float> width;
};

struct ShadowStyle {
    std::string color;
    std::optional<float> opacity;
    std::optional<float> blur;
    std::array<float, 2> offset{0.0f, 0.0f};
    std::size_t offset_dimensions{0};
};

struct BackgroundStyle {
    std::string color;
    std::optional<float> opacity;
    std::optional<float> radius;
    std::array<float, 2> padding{0.0f, 0.0f};
    std::size_t padding_dimensions{0};
};

// Concrete text paint supplied by the caller. There is deliberately no
// family/weight lookup or preset/profile fallback at the render-plan boundary.
struct LayerStylePlan {
    std::string font;
    std::optional<float> font_size;
    std::string fill;
    std::optional<StrokeStyle> stroke;
    std::optional<ShadowStyle> shadow;
    std::optional<BackgroundStyle> background;
};

struct AnimationKeyframePlan {
    Frame frame{0};
    std::vector<float> value;
};

// Generic concrete property track. `property` names engine properties only;
// editorial ids (preset/unit/role/profile) are intentionally not representable.
struct AnimationTrackPlan {
    std::string property;
    std::vector<AnimationKeyframePlan> keyframes;
    std::string easing{"linear"};
};

struct AnimationPlan {
    std::vector<AnimationTrackPlan> tracks;
};

// Generic AE-style text animator contract. Editorial motion ids are lowered
// into these primitives before Chronon sees the plan.
struct TextSelectorPlan {
    std::string id;
    std::string unit{"glyph"};
    std::string shape{"smooth"};
    std::string order{"forward"};
    std::string combine{"replace"};
    AnimationTrackPlan start;
    AnimationTrackPlan end;
    AnimationTrackPlan offset;
    AnimationTrackPlan amount;
    bool exclude_spaces{true};
    bool randomize_order{false};
    std::uint64_t random_seed{0};
};

struct TextAnimatorPlan {
    std::string id;
    std::vector<TextSelectorPlan> selectors;
    std::vector<AnimationTrackPlan> properties;
};

struct LayerPlan {
    std::string id;
    LayerType type{LayerType::Color};
    std::string asset;
    std::string source;
    std::string text;

    // Normalized concrete font asset path copied from style.font by decoder.
    // Kept as an execution field so asset preflight remains simple.
    std::string font;
    std::optional<LayerStylePlan> style;

    std::array<float, 2> size{0.0f, 0.0f};
    std::size_t size_dimensions{0};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::size_t position_dimensions{0};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    std::size_t scale_dimensions{0};
    std::array<float, 3> rotation{0.0f, 0.0f, 0.0f};
    std::size_t rotation_dimensions{0};

    std::optional<Frame> start_frame;
    std::optional<Frame> duration_frames;
    std::optional<FitMode> fit;
    std::optional<AnimationPlan> animation;
    std::vector<TextAnimatorPlan> text_animators;

    std::optional<chronon3d::BlendMode> blend_mode;
    std::optional<float> opacity;
    bool loop{false};
};

struct OutputSpec {
    std::string path;
    OutputFormat format{OutputFormat::Png};
    VideoCodec codec{VideoCodec::Auto};
    RateControlMode rate_control{RateControlMode::Crf};
    std::int64_t bitrate{0};
    int crf{0};
    int qp{-1};
    std::string profile_id;
};

struct RenderPlan {
    std::string schema{kRenderPlanSchemaV2};
    std::string job_id{"chronon_plan"};
    std::uint64_t content_fingerprint{0};
    CanvasSpec canvas;
    std::vector<LayerPlan> layers;
    OutputSpec output;
    RenderBudget budget{};
};

struct PlanDecodeError {
    std::string path;
    std::string message;
    std::string code;
    std::string component;
};

[[nodiscard]] std::optional<PlanDecodeError> validate_render_budget(
    const RenderPlan& plan,
    const RenderBudget& budget = {});

[[nodiscard]] std::optional<PlanDecodeError> validate_render_plan_budget(
    const RenderPlan& plan,
    const RenderBudget& budget = {});

[[nodiscard]] std::uint64_t compute_render_plan_content_fingerprint(
    const RenderPlan& plan);

Result<RenderPlan, PlanDecodeError> decode_render_plan(const nlohmann::json& root);

}  // namespace chronon3d::render_plan
