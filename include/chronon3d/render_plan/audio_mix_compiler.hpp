#pragma once

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d::render_plan {

struct AudioMixerInput {
    std::string id;
    std::string path;
    std::string role;
    std::int64_t source_start_sample{0};
    std::int64_t source_end_sample{-1};
    std::int64_t timeline_start_sample{0};
    float gain_db{0.0f};
    std::vector<float> gain_db_per_frame;
};

struct MixerSpec {
    int sample_rate{48000};
    std::int64_t duration_samples{0};
    std::vector<AudioMixerInput> inputs;
    std::string filter_complex;
};

struct AudioMixCompileError {
    std::string path;
    std::string message;
    std::string code;
};

class AudioMixCompiler final {
public:
    [[nodiscard]] static Result<MixerSpec, AudioMixCompileError> compile(
        const AudioMixPlan& mix,
        const CanvasSpec& canvas,
        const chronon3d::assets::AssetResolver& resolver,
        int sample_rate = 48000);
};

} // namespace chronon3d::render_plan
