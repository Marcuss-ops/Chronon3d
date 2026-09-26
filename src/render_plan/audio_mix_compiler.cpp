#include <chronon3d/render_plan/audio_mix_compiler.hpp>
#include <chronon3d/animation/core/animated_value.hpp>

#include "render_plan_compiler_detail.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <sstream>

namespace chronon3d::render_plan {
namespace {

std::int64_t frame_to_sample(Frame frame, FrameRate fps, int sample_rate) {
    const __int128 numerator = static_cast<__int128>(frame.integral()) *
        sample_rate * fps.den();
    const __int128 denominator = fps.num();
    const __int128 rounded = (numerator + denominator / 2) / denominator;
    if (rounded < 0 || rounded > std::numeric_limits<std::int64_t>::max())
        throw std::overflow_error("audio frame-to-sample conversion overflow");
    return static_cast<std::int64_t>(rounded);
}

std::string sample_gain_expression(const std::vector<float>& values, FrameRate fps) {
    if (values.empty()) return "1";
    std::string expression = std::to_string(values.back());
    for (std::size_t frame = values.size() - 1; frame > 0; --frame) {
        const double time = static_cast<double>(frame) * fps.den() / fps.num();
        std::ostringstream clause;
        clause.precision(12);
        clause << "if(lt(t\\," << time << ")\\," << values[frame - 1] << "\\,"
               << expression << ")";
        expression = clause.str();
    }
    return "pow(10\\,(" + expression + ")/20)";
}

void append_role_mix(std::ostringstream& graph,
                     const std::vector<std::string>& labels,
                     std::string_view output) {
    if (labels.empty()) return;
    for (const auto& label : labels) graph << '[' << label << ']';
    graph << "amix=inputs=" << labels.size()
          << ":duration=longest:normalize=0[" << output << "];";
}

} // namespace

Result<MixerSpec, AudioMixCompileError> AudioMixCompiler::compile(
    const AudioMixPlan& mix, const CanvasSpec& canvas,
    const chronon3d::assets::AssetResolver& resolver, int sample_rate) {
    if (sample_rate <= 0 || mix.clips.empty())
        return AudioMixCompileError{"audio", "sample rate must be positive and mix must have clips",
                                    "invalid_audio_mix_spec"};
    try {
        MixerSpec spec;
        spec.sample_rate = sample_rate;
        spec.duration_samples = frame_to_sample(canvas.duration, canvas.fps, sample_rate);
        spec.inputs.reserve(mix.clips.size());
        std::vector<std::string> voice_labels, music_labels, sfx_labels;
        std::ostringstream graph;
        for (std::size_t index = 0; index < mix.clips.size(); ++index) {
            const auto& clip = mix.clips[index];
            const auto resolved = resolver.resolve_logical(std::filesystem::path{clip.asset});
            if (!resolved)
                return AudioMixCompileError{"audio.clips[" + std::to_string(index) + "].asset",
                    "audio asset could not be resolved under the mounted asset root",
                    "audio_asset_unresolved"};
            AudioMixerInput input;
            input.id = clip.id;
            input.path = resolved->string();
            input.role = clip.role;
            input.source_start_sample = frame_to_sample(
                clip.clip_in.value_or(Frame{0}), canvas.fps, sample_rate);
            if (clip.clip_out)
                input.source_end_sample = frame_to_sample(*clip.clip_out, canvas.fps, sample_rate);
            input.timeline_start_sample = frame_to_sample(
                clip.start.value_or(Frame{0}), canvas.fps, sample_rate);
            input.gain_db_per_frame.assign(
                static_cast<std::size_t>(canvas.duration.integral()), clip.gain_db);
            if (clip.gain) {
                chronon3d::AnimatedValue<float> gain{clip.gain_db};
                for (const auto& key : clip.gain->keyframes) {
                    if (key.value.size() != 1 || !std::isfinite(key.value.front()))
                        return AudioMixCompileError{"audio.clips[" + std::to_string(index) + "].gain",
                            "animated gain values must be finite scalars", "invalid_audio_gain_track"};
                    gain.add_keyframe(key.frame, key.value.front(),
                                      detail::animation_keyframe_easing(*clip.gain, key));
                }
                for (std::int64_t frame = 0; frame < canvas.duration.integral(); ++frame)
                    input.gain_db_per_frame[static_cast<std::size_t>(frame)] =
                        gain.evaluate(static_cast<double>(frame));
            }
            input.gain_db = clip.gain_db;
            if (clip.muted)
                std::fill(input.gain_db_per_frame.begin(), input.gain_db_per_frame.end(), -120.0f);
            const std::string label = "clip" + std::to_string(index);
            if (!clip.muted) {
                graph << '[' << index << ":a]aresample=" << sample_rate
                      << ",atrim=start_sample=" << input.source_start_sample;
                if (input.source_end_sample >= 0)
                    graph << ":end_sample=" << input.source_end_sample;
                graph << ",asetpts=PTS-STARTPTS,adelay="
                      << input.timeline_start_sample << "S:all=1,apad=whole_len="
                      << spec.duration_samples << ",atrim=end_sample="
                      << spec.duration_samples << ",volume='"
                      << sample_gain_expression(input.gain_db_per_frame, canvas.fps)
                      << "':eval=frame[" << label << "];";
                if (clip.role == "voice") voice_labels.push_back(label);
                else if (clip.role == "music") music_labels.push_back(label);
                else sfx_labels.push_back(label);
            }
            spec.inputs.push_back(std::move(input));
        }
        append_role_mix(graph, voice_labels, "voice");
        append_role_mix(graph, music_labels, "music");
        append_role_mix(graph, sfx_labels, "sfx");

        std::vector<std::string> final_labels;
        if (!music_labels.empty() && !voice_labels.empty()) {
            const float ratio = std::pow(10.0f, std::abs(mix.duck_amount_db) / 20.0f);
            // Keep the voice bus in the final mix while a separate copy drives
            // the compressor. Explicitly splitting the label avoids FFmpeg's
            // implicit multi-consumer handling, and padding both buses to the
            // canvas prevents sidechaincompress from truncating the music at
            // the last active voice sample.
            graph << "[voice]asplit=2[voice_sidechain][voice_mix];"
                  << "[music][voice_sidechain]sidechaincompress=threshold=0.01:ratio="
                  << ratio << ":attack=" << mix.duck_attack_ms
                  << ":release=" << mix.duck_release_ms << "[ducked_music];";
            final_labels.push_back("ducked_music");
            final_labels.push_back("voice_mix");
        } else if (!music_labels.empty()) final_labels.push_back("music");
        else if (!voice_labels.empty()) final_labels.push_back("voice");
        if (!sfx_labels.empty()) final_labels.push_back("sfx");
        if (final_labels.empty())
            return AudioMixCompileError{"audio.clips", "all audio clips are muted",
                                        "audio_mix_all_muted"};
        for (const auto& label : final_labels) graph << '[' << label << ']';
        graph << "amix=inputs=" << final_labels.size()
              << ":duration=longest:normalize=0,volume='pow(10\\,("
              << mix.master_gain_db << ")/20)',atrim=end_sample="
              << spec.duration_samples << ",asetpts=PTS-STARTPTS[out]";
        spec.filter_complex = graph.str();
        if (spec.filter_complex.size() > 8U * 1024U * 1024U)
            return AudioMixCompileError{"audio", "compiled gain envelope exceeds the 8 MiB filter budget",
                                        "audio_filter_budget_exceeded"};
        return spec;
    } catch (const std::exception& error) {
        return AudioMixCompileError{"audio", error.what(), "audio_mix_compile_failed"};
    }
}

} // namespace chronon3d::render_plan
