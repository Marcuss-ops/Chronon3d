#include "audio_react_plan_baker.hpp"

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include "render_plan_compiler_detail.hpp"
#include "audio_react_baker.hpp"
#include "audio_react_contract.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::render_plan::detail {
namespace {
std::optional<std::size_t> audio_react_clip_index(const RenderPlan& plan,
                                                   std::string_view source) {
    if (!plan.audio || !source.starts_with("mix:")) return std::nullopt;
    const auto key = source.substr(4);
    std::size_t index = 0;
    const auto parsed = std::from_chars(key.data(), key.data() + key.size(), index);
    if (parsed.ec == std::errc{} && parsed.ptr == key.data() + key.size())
        return index < plan.audio->clips.size() ? std::optional<std::size_t>{index} : std::nullopt;
    for (std::size_t i = 0; i < plan.audio->clips.size(); ++i)
        if (plan.audio->clips[i].id == key) return i;
    return std::nullopt;
}

float audio_react_measure(const std::vector<float>& samples, std::int64_t center,
                          const std::optional<std::array<float, 2>>& band) {
    constexpr std::int64_t window_size = 2048;
    constexpr std::int64_t half = window_size / 2;
    double square_sum = 0.0;
    for (std::int64_t i = 0; i < window_size; ++i) {
        const auto sample_index = center - half + i;
        if (sample_index >= 0 && static_cast<std::size_t>(sample_index) < samples.size()) {
            const double sample = samples[static_cast<std::size_t>(sample_index)];
            square_sum += sample * sample;
        }
    }
    if (!band) return static_cast<float>(std::sqrt(square_sum / window_size));

    const double low = (*band)[0];
    const double high = (*band)[1];
    constexpr double nyquist = kAudioReactNyquistHz;
    if (high > nyquist) throw std::runtime_error("audio_react band upper edge exceeds the 48 kHz analysis Nyquist frequency");
    constexpr std::size_t kBins = 32;
    double power_sum = 0.0;
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        const double ratio = (static_cast<double>(bin) + 0.5) / kBins;
        const double frequency = low * std::pow(high / low, ratio);
        const double omega = 2.0 * std::numbers::pi * frequency / static_cast<double>(kAudioReactSampleRateHz);
        const double coefficient = 2.0 * std::cos(omega);
        double previous = 0.0, previous2 = 0.0;
        for (std::int64_t i = 0; i < window_size; ++i) {
            const auto sample_index = center - half + i;
            const double sample = sample_index >= 0 && static_cast<std::size_t>(sample_index) < samples.size()
                ? samples[static_cast<std::size_t>(sample_index)] : 0.0;
            const double hann = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (window_size - 1));
            const double current = sample * hann + coefficient * previous - previous2;
            previous2 = previous;
            previous = current;
        }
        power_sum += std::max(0.0, previous * previous + previous2 * previous2 - coefficient * previous * previous2);
    }
    const double normalization = static_cast<double>(window_size) * window_size;
    // The Hann coherent gain is approximately 1/2; the factor 8 recovers
    // sinusoidal mean-square energy and converts it to RMS.
    return static_cast<float>(std::sqrt(8.0 * power_sum / normalization));
}

}

void bake_audio_react_plan(RenderPlan& plan, chronon3d::assets::AssetResolver& resolver) {
    for (std::size_t layer_index = 0; layer_index < plan.layers.size(); ++layer_index) {
        auto& layer = plan.layers[layer_index];
        if (!layer.animation) continue;
        for (std::size_t track_index = 0; track_index < layer.animation->tracks.size(); ++track_index) {
            auto& track = layer.animation->tracks[track_index];
            if (!track.audio_react) continue;
            auto& driver = *track.audio_react;
            const auto clip_index = audio_react_clip_index(plan, driver.source);
            const std::string error_path = "layers[" + std::to_string(layer_index) +
                "].animation.tracks[" + std::to_string(track_index) + "].audio_react";
            if (!clip_index)
                throw std::runtime_error(error_path + ": source must identify a clip in the audio mix");
            const auto& clip = plan.audio->clips[*clip_index];
            const auto resolved = resolver.resolve_logical(clip.asset);
            if (!resolved) throw std::runtime_error(error_path + ": source audio asset cannot be resolved");
            const auto digest = chronon3d::assets::sha256_file(*resolved);
            if (!digest) throw std::runtime_error(error_path + ": source audio asset cannot be hashed");
            driver.source_sha256 = digest->hex();
            std::vector<float> samples;
            std::string decode_error;
            if (!decode_audio_mono_48k(resolved->string(), samples, decode_error))
                throw std::runtime_error(error_path + ": " + decode_error);
            if (std::any_of(samples.begin(), samples.end(), [](float sample) {
                    return !std::isfinite(sample);
                }))
                throw std::runtime_error(error_path + ": decoded audio contains a non-finite sample");
            const double fps = static_cast<double>(plan.canvas.fps.num()) / plan.canvas.fps.den();
            const auto start_frame = clip.start.value_or(Frame{0}).integral();
            const auto in_frame = clip.clip_in.value_or(Frame{0}).integral();
            const auto out_frame = clip.clip_out
                ? std::optional<std::int64_t>{clip.clip_out->integral()} : std::nullopt;
            const auto frame_count = static_cast<std::size_t>(plan.canvas.duration.integral());
            if (frame_count == 0 || frame_count > 1000000)
                throw std::runtime_error(error_path + ": composition frame count is outside analysis bounds");
            constexpr std::uint64_t kMaxAudioReactAnalysisOps = 2000000000ULL;
            const std::uint64_t operations_per_frame = 2048ULL *
                (driver.band_hz ? 32ULL : 1ULL);
            if (frame_count > kMaxAudioReactAnalysisOps / operations_per_frame)
                throw std::runtime_error(error_path + ": audio analysis exceeds the deterministic work budget");
            std::vector<float> raw(frame_count, 0.0f);
            std::vector<float> clip_gain_db(frame_count, clip.gain_db + plan.audio->master_gain_db);
            if (clip.gain) {
                chronon3d::AnimatedValue<float> animated_gain{clip.gain_db};
                for (const auto& key : clip.gain->keyframes) {
                    if (key.value.size() != 1 || !std::isfinite(key.value.front()))
                        throw std::runtime_error(error_path + ": source clip gain track must contain finite scalar values");
                    animated_gain.add_keyframe(key.frame, key.value.front(),
                        animation_keyframe_easing(*clip.gain, key));
                }
                for (std::size_t frame = 0; frame < frame_count; ++frame)
                    clip_gain_db[frame] = animated_gain.evaluate(static_cast<double>(frame)) +
                        plan.audio->master_gain_db;
            }
            for (std::size_t frame = 0; frame < frame_count; ++frame) {
                if (clip.muted) continue;
                const auto source_frame = in_frame + static_cast<std::int64_t>(frame) - start_frame;
                if (source_frame < in_frame || (out_frame && source_frame >= *out_frame)) continue;
                const auto center = static_cast<std::int64_t>(std::llround(
                    static_cast<double>(source_frame) * static_cast<double>(kAudioReactSampleRateHz) / fps));
                raw[frame] = audio_react_measure(samples, center, driver.band_hz) *
                    std::pow(10.0f, clip_gain_db[frame] / 20.0f);
                if (!std::isfinite(raw[frame]))
                    throw std::runtime_error(error_path + ": analysis produced a non-finite feature value");
            }
            driver.baked_values.resize(frame_count);
            double rolling_sum = 0.0;
            for (std::size_t frame = 0; frame < frame_count; ++frame) {
                rolling_sum += raw[frame];
                if (frame >= driver.smoothing_frames)
                    rolling_sum -= raw[frame - driver.smoothing_frames];
                const auto divisor = std::min<std::size_t>(frame + 1, driver.smoothing_frames);
                driver.baked_values[frame] = static_cast<float>(
                    rolling_sum / static_cast<double>(divisor) * driver.gain);
                if (!std::isfinite(driver.baked_values[frame]))
                    throw std::runtime_error(error_path + ": baked audio feature exceeds finite float range");
            }
            AnimationTrackPlan::DataFeedDriverPlan feed;
            feed.values = driver.baked_values;
            feed.interpolation = "linear";
            track.data_feed = std::move(feed);
        }
    }
}


} // namespace chronon3d::render_plan::detail
