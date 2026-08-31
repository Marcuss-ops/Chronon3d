#pragma once

#include <chronon3d/core/gpu_hot_path_mode.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>
#include <string_view>

namespace chronon3d::media::detail {

// Compatibility adapter for the CLI's legacy flags.  This type is deliberately
// outside the public media contract: authoring/orchestration code must use
// ExecutionRequirements + OutputSpec and let Chronon discover backends.
struct LegacyVideoExecutionRequest {
    std::string_view encoder_backend;
    std::string_view hardware_encoder;
    std::string_view codec;
    GpuHotPathMode hot_path{GpuHotPathMode::Auto};
    bool has_gop_source{false};
    bool gop_copy_only{false};
    bool allow_hybrid_gop{false};
};

[[nodiscard]] VideoExecutionDecision resolve_legacy_video_execution(
    const LegacyVideoExecutionRequest& request) noexcept;

} // namespace chronon3d::media::detail
