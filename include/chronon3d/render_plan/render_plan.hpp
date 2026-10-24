#pragma once

#include <chronon3d/render_plan/render_plan_layers.hpp>

namespace chronon3d::render_plan {
struct PreparedOverlayPackage {
    struct OverlayBinding {
        std::string id;
        std::string kind;
        std::string text_key;
        std::string asset_key;
    };
    struct TextBinding {
        std::string key;
        std::string text;
    };
    struct AssetBinding {
        std::string key;
        std::string logical_path;
        std::string content_hash;
    };

    std::string plan_id;
    std::string language;
    std::string content_hash;
    std::vector<OverlayBinding> overlays;
    std::vector<TextBinding> text_runs;
    std::vector<AssetBinding> assets;
};

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

struct AudioClipPlan {
    std::string id;
    std::string asset;
    std::optional<Frame> clip_in;
    std::optional<Frame> clip_out;
    std::optional<Frame> start;
    float gain_db{0.0f};
    bool muted{false};
    std::optional<AnimationTrackPlan> gain;
    std::string role{"music"};
};

struct AudioMixPlan {
    std::vector<AudioClipPlan> clips;
    float master_gain_db{0.0f};
    float duck_amount_db{-9.0f};
    float duck_attack_ms{120.0f};
    float duck_release_ms{400.0f};
};

struct LightingRigPlan {
    std::array<float, 3> key_direction{-0.2857143f, 0.8571429f, -0.4285714f};
    std::array<float, 4> key_color{1.0f, 1.0f, 1.0f, 1.0f};
    float key_intensity{0.80f};
    std::array<float, 4> ambient_color{0.6f, 0.65f, 0.7f, 1.0f};
    float ambient_intensity{0.25f};
    bool rim_enabled{false};
    std::array<float, 4> rim_color{1.0f, 1.0f, 1.0f, 1.0f};
    float rim_intensity{0.30f};
    float rim_power{2.2f};
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

}  // namespace chronon3d::render_plan
