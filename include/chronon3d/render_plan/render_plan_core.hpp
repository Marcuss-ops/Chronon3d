#pragma once

#include <chronon3d/render_plan/render_plan_types.hpp>
#include <chronon3d/render_plan/render_budget.hpp>
#include <chronon3d/render_plan/render_plan_audio.hpp>
#include <chronon3d/render_plan/render_plan_lighting.hpp>
#include <chronon3d/render_plan/render_plan_overlay.hpp>
#include <chronon3d/render_plan/render_plan_effects.hpp>
#include <chronon3d/render_plan/render_plan_gradient.hpp>
#include <chronon3d/render_plan/render_plan_layer.hpp>
#include <chronon3d/render_plan/render_plan_layer_types.hpp>
#include <chronon3d/render_plan/render_plan_layers.hpp>
#include <chronon3d/render_plan/render_plan_text.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::render_plan {

struct OutputSpec {
    std::string path;
    std::string color_space{"srgb"};
    float reference_white_nits{100.0f};
    std::optional<chronon3d::color::HdrMasteringMetadata> hdr_mastering_metadata;
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
    std::vector<std::string> warnings;
    CanvasSpec canvas;
    // Full-scene shutter accumulation; separate from the per-layer analytic blur.
    std::optional<MotionBlurPlan> temporal_motion_blur;
    std::optional<CameraPlan> camera;
    std::optional<std::vector<CameraSamplePlan>> camera_samples;
    std::optional<LightingRigPlan> lighting_rig;
    struct ColorManagementPlan {
        std::string ocio_config;
        std::string working_space;
        // Optional project default for video layers. A layer-level
        // ocio_input_space overrides this value.
        std::string video_input_space;
        // Optional display-referred output transform. Both names must be
        // present together; empty values preserve output.color_space behavior.
        std::string output_display;
        std::string output_view;
    };
    std::optional<ColorManagementPlan> color_management;
    std::vector<LayerPlan> layers;
    std::optional<AudioMixPlan> audio;
    std::shared_ptr<const PreparedOverlayPackage> prepared_overlay_package{};
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

[[nodiscard]] std::optional<PlanDecodeError> validate_render_plan_layer_relations(
    const RenderPlan& plan);

Result<RenderPlan, PlanDecodeError> decode_render_plan(const nlohmann::json& root);



} // namespace chronon3d::render_plan
