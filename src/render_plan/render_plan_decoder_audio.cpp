#include <chronon3d/render_plan/easing_contract.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace easing_name = chronon3d::render_plan::detail::easing_name;
namespace interpolation_name = chronon3d::render_plan::detail::interpolation_name;

namespace chronon3d::render_plan::detail {

Result<DecodedAudioMix, PlanDecodeError> decode_audio_mix(
    const nlohmann::json& raw_mix, Frame canvas_duration) {
    DecodedAudioMix decoded;
    auto& mix = decoded.mix;

    mix.master_gain_db = raw_mix.value("master_gain_db", 0.0f);
    mix.duck_amount_db = raw_mix.value("duck_amount_db", -9.0f);
    mix.duck_attack_ms = raw_mix.value("duck_attack_ms", 120.0f);
    mix.duck_release_ms = raw_mix.value("duck_release_ms", 400.0f);
    if (!std::isfinite(mix.master_gain_db) ||
        !std::isfinite(mix.duck_amount_db) ||
        !std::isfinite(mix.duck_attack_ms) || mix.duck_attack_ms < 0.0f ||
        !std::isfinite(mix.duck_release_ms) || mix.duck_release_ms < 0.0f)
        return PlanDecodeError{"audio", "audio mix gains and duck times must be finite; attack/release must be non-negative",
            "invalid_audio_mix", "render-plan-audio"};
    std::set<std::string> ids;
    for (const auto& raw_clip : raw_mix.at("clips")) {
        AudioClipPlan clip;
        clip.id = raw_clip.at("id").get<std::string>();
        clip.asset = raw_clip.at("asset").get<std::string>();
        clip.clip_in = raw_clip.contains("clip_in")
            ? std::optional<Frame>{Frame{raw_clip.at("clip_in").get<std::int64_t>()}} : std::nullopt;
        clip.clip_out = raw_clip.contains("clip_out")
            ? std::optional<Frame>{Frame{raw_clip.at("clip_out").get<std::int64_t>()}} : std::nullopt;
        clip.start = raw_clip.contains("start")
            ? std::optional<Frame>{Frame{raw_clip.at("start").get<std::int64_t>()}} : std::nullopt;
        clip.gain_db = raw_clip.value("gain_db", 0.0f);
        clip.muted = raw_clip.value("muted", false);
        clip.role = raw_clip.value("role", std::string{"music"});
        if (clip.id.empty() || !ids.insert(clip.id).second)
            return PlanDecodeError{"audio.clips[].id", "audio clip ids must be non-empty and unique",
                "invalid_audio_clip_id", "render-plan-audio"};
        if (invalid_logical_path(clip.asset))
            return PlanDecodeError{"audio.clips[].asset", "audio asset must be a relative logical path",
                "invalid_audio_asset", "render-plan-audio"};
        if (!std::isfinite(clip.gain_db))
            return PlanDecodeError{"audio.clips[].gain_db", "gain_db must be finite",
                "invalid_audio_gain", "render-plan-audio"};
        if (clip.role != "music" && clip.role != "voice" && clip.role != "sfx")
            return PlanDecodeError{"audio.clips[].role", "role must be music, voice, or sfx",
                "invalid_audio_role", "render-plan-audio"};
        if ((clip.clip_in && clip.clip_in->integral() < 0) ||
            (clip.clip_out && clip.clip_out->integral() < 0) ||
            (clip.start && clip.start->integral() < 0) ||
            (clip.clip_in && clip.clip_out && *clip.clip_out <= *clip.clip_in))
            return PlanDecodeError{"audio.clips[]", "trim and timeline frames must be non-negative; clip_out must exceed clip_in",
                "invalid_audio_trim", "render-plan-audio"};
        if (raw_clip.contains("gain")) {
            const auto& raw_gain = raw_clip.at("gain");
            AnimationTrackPlan track;
            track.property = raw_gain.value("property", std::string{"gain_db"});
            if (track.property != "gain_db")
                return PlanDecodeError{"audio.clips[].gain.property", "animated gain property must be gain_db",
                    "invalid_audio_gain_property", "render-plan-audio"};
            track.easing = raw_gain.value("easing", std::string{"linear"});
            if (raw_gain.contains("bezier_curve")) {
                const auto& curve = raw_gain.at("bezier_curve");
                if (!curve.is_array() || curve.size() != 4)
                    return PlanDecodeError{"audio.clips[].gain.bezier_curve", "must contain [x1,y1,x2,y2]",
                        "invalid_audio_gain_curve", "render-plan-audio"};
                std::array<float, 4> parsed{};
                for (std::size_t index = 0; index < 4; ++index) parsed[index] = curve.at(index).get<float>();
                    if (!std::all_of(parsed.begin(), parsed.end(), [](float v) { return std::isfinite(v); }) ||
                    parsed[0] < 0.0f || parsed[0] > 1.0f || parsed[2] < 0.0f || parsed[2] > 1.0f ||
                    parsed[1] < -10.0f || parsed[1] > 10.0f || parsed[3] < -10.0f || parsed[3] > 10.0f)
                    return PlanDecodeError{"audio.clips[].gain.bezier_curve", "values must be finite and x handles in [0,1]",
                        "invalid_audio_gain_curve", "render-plan-audio"};
                track.bezier_curve = parsed;
            }
            if ((track.easing == easing_name::kBezier) != track.bezier_curve.has_value())
                return PlanDecodeError{"audio.clips[].gain", "bezier easing and bezier_curve must be provided together",
                    "invalid_audio_gain_curve", "render-plan-audio"};
            for (const auto& raw_key : raw_gain.at("keyframes")) {
                AnimationKeyframePlan key;
                key.frame = Frame{raw_key.at("frame").get<std::int64_t>()};
                const float value = raw_key.at("value").get<float>();
                if (key.frame.integral() < 0 || !std::isfinite(value))
                    return PlanDecodeError{"audio.clips[].gain.keyframes", "frames must be non-negative and values finite",
                        "invalid_audio_gain_keyframe", "render-plan-audio"};
                key.value = {value};
                if (raw_key.contains("interpolation")) key.interpolation = raw_key.at("interpolation").get<std::string>();
                if (raw_key.contains("easing")) key.easing = raw_key.at("easing").get<std::string>();
                key.roving = raw_key.value("roving", false);
                if (raw_key.contains("temporal_in_dx")) key.temporal_in_dx = raw_key.at("temporal_in_dx").get<float>();
                if (raw_key.contains("temporal_in_dy")) key.temporal_in_dy = raw_key.at("temporal_in_dy").get<float>();
                if (raw_key.contains("temporal_out_dx")) key.temporal_out_dx = raw_key.at("temporal_out_dx").get<float>();
                if (raw_key.contains("temporal_out_dy")) key.temporal_out_dy = raw_key.at("temporal_out_dy").get<float>();
                const auto finite_gain_handle = [&](const char* name) {
                    return !raw_key.contains(name) || (raw_key.at(name).is_number() &&
                        std::isfinite(raw_key.at(name).get<double>()));
                };
                if (!finite_gain_handle("temporal_in_dx") || !finite_gain_handle("temporal_in_dy") ||
                    !finite_gain_handle("temporal_out_dx") || !finite_gain_handle("temporal_out_dy"))
                    throw std::runtime_error("audio gain temporal handles must be finite numbers");
                if (raw_key.contains("temporal_in_dx") != raw_key.contains("temporal_in_dy") ||
                    raw_key.contains("temporal_out_dx") != raw_key.contains("temporal_out_dy"))
                    throw std::runtime_error("audio gain temporal dx and dy must be supplied together");
                if (key.temporal_in_dx && *key.temporal_in_dx > 0.0f)
                    throw std::runtime_error("audio gain temporal_in_dx must be non-positive");
                if (key.temporal_out_dx && *key.temporal_out_dx < 0.0f)
                    throw std::runtime_error("audio gain temporal_out_dx must be non-negative");
                if (raw_key.contains("bezier_curve")) {
                    const auto& raw_curve = raw_key.at("bezier_curve");
                    if (!raw_curve.is_array() || raw_curve.size() != 4)
                        throw std::runtime_error("audio gain keyframe bezier_curve must contain four values");
                    std::array<float, 4> curve{};
                    for (std::size_t index = 0; index < curve.size(); ++index)
                        curve[index] = raw_curve.at(index).get<float>();
                    if (!std::all_of(curve.begin(), curve.end(), [](float v) { return std::isfinite(v); }) ||
                        curve[0] < 0.0f || curve[0] > 1.0f || curve[2] < 0.0f || curve[2] > 1.0f ||
                        curve[1] < -10.0f || curve[1] > 10.0f || curve[3] < -10.0f || curve[3] > 10.0f)
                        throw std::runtime_error("audio gain keyframe bezier_curve handles are out of range");
                    key.bezier_curve = curve;
                }
                const auto interpolation = key.interpolation.value_or(std::string{});
                const bool has_handles = key.temporal_in_dx || key.temporal_in_dy ||
                    key.temporal_out_dx || key.temporal_out_dy;
                const bool has_track_timing = raw_gain.contains("easing") ||
                    raw_gain.contains("bezier_curve");
                if (key.interpolation && key.easing)
                    throw std::runtime_error("audio gain keyframe interpolation cannot be combined with easing");
                if (!key.interpolation && key.easing && has_track_timing)
                    throw std::runtime_error("audio gain per-key easing cannot be combined with track timing");
                if (key.roving && (has_handles || key.bezier_curve))
                    throw std::runtime_error("audio gain roving cannot combine with authored handles");
                if (key.roving && (interpolation == interpolation_name::kBezier || interpolation == interpolation_name::kAutoBezier))
                    throw std::runtime_error("audio gain roving cannot combine with Bezier interpolation");
                if (has_handles && interpolation != interpolation_name::kBezier)
                    throw std::runtime_error("audio gain temporal handles require interpolation='bezier'");
                if (raw_key.contains("roving") && !raw_key.at("roving").is_boolean())
                    throw std::runtime_error("audio gain roving must be a boolean");
                if (key.interpolation && *key.interpolation != interpolation_name::kLinear &&
                    *key.interpolation != interpolation_name::kHold && *key.interpolation != interpolation_name::kBezier &&
                    *key.interpolation != interpolation_name::kAutoBezier)
                    throw std::runtime_error("audio gain interpolation must be linear, hold, bezier, or auto_bezier");
                if (interpolation == interpolation_name::kBezier && !key.bezier_curve && !has_handles)
                    throw std::runtime_error("audio gain bezier interpolation needs bezier_curve or temporal handles");
                if (interpolation != interpolation_name::kBezier && key.bezier_curve)
                    throw std::runtime_error("audio gain bezier_curve requires interpolation='bezier'");
                if (!key.interpolation && ((key.easing == easing_name::kBezier) != key.bezier_curve.has_value()))
                    throw std::runtime_error("audio gain easing='bezier' requires bezier_curve");
                if (interpolation == interpolation_name::kAutoBezier && (key.easing || key.bezier_curve))
                    throw std::runtime_error("audio gain auto_bezier cannot combine with per-key easing or bezier_curve");
                if (interpolation == interpolation_name::kAutoBezier && (key.temporal_in_dx || key.temporal_in_dy ||
                    key.temporal_out_dx || key.temporal_out_dy || key.bezier_curve))
                    throw std::runtime_error("audio gain auto_bezier cannot combine with authored handles");
                if (key.roving && interpolation == interpolation_name::kAutoBezier)
                    throw std::runtime_error("audio gain roving and auto_bezier cannot be combined");
                track.keyframes.push_back(std::move(key));
            }
            for (std::size_t index = 1; index < track.keyframes.size(); ++index)
                if (track.keyframes[index - 1].frame == track.keyframes[index].frame)
                    throw std::runtime_error("audio gain keyframe frames must be unique");
            if (track.keyframes.empty())
                return PlanDecodeError{"audio.clips[].gain.keyframes", "at least one keyframe is required",
                    "empty_audio_gain_track", "render-plan-audio"};
            clip.gain = std::move(track);
        }
        if (clip.clip_out) {
            const auto timeline_start = clip.start.value_or(Frame{0}).integral();
            const auto source_in = clip.clip_in.value_or(Frame{0}).integral();
            const auto source_out = clip.clip_out->integral();
            const auto timeline_end = timeline_start + source_out - source_in;
            if (timeline_end > canvas_duration.integral())
                decoded.warnings.emplace_back("audio_clip_tail_clipped:" + clip.id);
        }
        mix.clips.push_back(std::move(clip));
    }
    if (mix.clips.empty())
        return PlanDecodeError{"audio.clips", "audio mix must contain at least one clip",
            "empty_audio_mix", "render-plan-audio"};

    return decoded;
}

} // namespace chronon3d::render_plan::detail
