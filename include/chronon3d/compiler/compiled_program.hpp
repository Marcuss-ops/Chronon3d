#pragma once

#include <chronon3d/compiler/compiled_node.hpp>
#include <chronon3d/compiler/compiled_pass.hpp>
#include <chronon3d/compiler/compiled_resource.hpp>
#include <chronon3d/compiler/compiled_layer.hpp>
#include <chronon3d/compiler/temporal_update_schedule.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::compiler {

/// Immutable result of the compiler boundary.
///
/// The concrete program data is intentionally hidden behind a const shared
/// object and carries no execution-owned state or authoring object pointers.
/// Runtime consumers read this immutable value through the typed sampler and
/// graph executor. It is the canonical compiled-program representation; no
/// prepared-plan bridge is retained here.
struct ProgramMetadata {
    /// Compiler-owned copy of the output contract.  Keep this numeric so the
    /// frozen program header does not expose render-plan authoring types.
    struct OutputSpec {
        std::string path;
        std::uint8_t format{0};
        std::uint8_t codec{0};
        std::uint8_t rate_control{0};
        std::int64_t bitrate{0};
        std::int32_t crf{0};
        std::int32_t qp{-1};
        std::string profile_id;
    };

    /// Admission limits copied at the compiler boundary.  This mirrors the
    /// value contract, rather than retaining a render-plan object.
    struct RenderBudget {
        std::uint32_t max_width{7680};
        std::uint32_t max_height{4320};
        std::uint64_t max_total_pixels{7680ULL * 4320ULL};
        std::uint64_t max_frames{1'000'000};
        std::uint32_t max_layers{1024};
        std::uint64_t max_text_bytes{4ULL * 1024ULL * 1024ULL};
        std::uint64_t max_asset_reference_bytes{1ULL * 1024ULL * 1024ULL};
        std::uint64_t max_estimated_output_bytes{4ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL};
        std::uint64_t max_peak_memory_bytes{2ULL * 1024ULL * 1024ULL * 1024ULL};
        std::uint64_t max_temporal_pixels{128ULL * 1024ULL * 1024ULL};
    };

    std::string schema;
    std::string job_id;
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::int64_t duration_frames{0};
    std::int32_t fps_numerator{0};
    std::int32_t fps_denominator{1};
    OutputSpec output{};
    std::string assets_root;
    assets::PreparedAssetManifest asset_manifest{};
    assets::ContentDigest content_digest{};
    assets::ContentDigest request_digest{};
    RenderBudget render_budget{};
    std::optional<render_plan::LightingRigPlan> lighting_rig;
};

struct ProgramFingerprint {
    std::uint64_t digest{0};
    std::uint32_t algorithm_version{1};
    assets::ContentDigest content_digest{};
    assets::ContentDigest request_digest{};
};

/// Mutable assembly buffer used only between compiler passes.
struct CompiledProgramData {
    ProgramMetadata metadata;
    std::vector<CompiledNode> nodes;
    std::vector<StaticLayerProgram> layers;
    std::vector<std::uint32_t> layer_evaluation_order;
    std::vector<CompiledPass> passes;
    std::vector<ResourceBindingSpec> resources;
    std::vector<AnimationProgram> animations;
    std::vector<TimelineProgram> timelines;
    std::vector<AnimationTrackProgram> animation_tracks;
    std::vector<CurveKeyframeProgram> curve_keyframes;
    std::vector<TextProgram> texts;
    std::vector<std::string> text_values;
    std::vector<ParameterProgram> parameters;
    TemporalUpdateSchedule temporal_schedule;
    ProgramFingerprint fingerprint_data;
};

/// Frozen, data-oriented compiler output. Frame-varying values belong to the
/// sampler/runtime, not to this object.
class CompiledProgram final {
public:
    ~CompiledProgram();
    CompiledProgram(CompiledProgram&&) noexcept;
    CompiledProgram& operator=(CompiledProgram&&) noexcept;
    CompiledProgram(const CompiledProgram&) = delete;
    CompiledProgram& operator=(const CompiledProgram&) = delete;

    [[nodiscard]] const ProgramMetadata& metadata() const noexcept;
    [[nodiscard]] const std::vector<CompiledNode>& nodes() const noexcept;
    [[nodiscard]] const std::vector<StaticLayerProgram>& layers() const noexcept;
    [[nodiscard]] const std::vector<std::uint32_t>& layer_evaluation_order() const noexcept;
    [[nodiscard]] const std::vector<CompiledPass>& passes() const noexcept;
    [[nodiscard]] const std::vector<ResourceBindingSpec>& resources() const noexcept;
    [[nodiscard]] const std::vector<AnimationProgram>& animations() const noexcept;
    [[nodiscard]] const std::vector<TimelineProgram>& timelines() const noexcept;
    [[nodiscard]] const std::vector<AnimationTrackProgram>& animation_tracks() const noexcept;
    [[nodiscard]] const std::vector<CurveKeyframeProgram>& curve_keyframes() const noexcept;
    [[nodiscard]] const std::vector<TextProgram>& texts() const noexcept;
    [[nodiscard]] const std::vector<std::string>& text_values() const noexcept;
    [[nodiscard]] const std::vector<ParameterProgram>& parameters() const noexcept;
    [[nodiscard]] const TemporalUpdateSchedule& temporal_schedule() const noexcept;
    [[nodiscard]] const ProgramFingerprint& fingerprint_data() const noexcept;

    [[nodiscard]] std::uint64_t fingerprint() const noexcept { return data_->fingerprint_data.digest; }

private:
    explicit CompiledProgram(
        std::shared_ptr<const CompiledProgramData> data);

    std::shared_ptr<const CompiledProgramData> data_;

    friend class Compiler;
};

}  // namespace chronon3d::compiler
