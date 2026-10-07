#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_validator.hpp>
#include <chronon3d/render_plan/animation_property_registry.hpp>

#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cctype>
#include <exception>
#include <map>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>
#include <set>

namespace {

using chronon3d::Frame;
using chronon3d::render_plan::LayerPlan;
using chronon3d::render_plan::PlanDecodeError;

bool has_explicit_3d_vector(const nlohmann::json& layer, const char* key) {
    return layer.contains(key) && layer.at(key).is_array() &&
           layer.at(key).size() >= 3;
}

struct ResolvedVariables {
    nlohmann::json plan;
    std::vector<std::string> unused;
};

ResolvedVariables resolve_variables(const nlohmann::json& source) {
    ResolvedVariables result{source, {}};
    if (!source.contains("variables")) return result;
    const auto& raw_variables = source.at("variables");
    if (!raw_variables.is_object() || raw_variables.size() > 1024)
        throw std::runtime_error("variables must be an object with at most 1024 entries");
    std::unordered_map<std::string, nlohmann::json> variables;
    for (auto it = raw_variables.begin(); it != raw_variables.end(); ++it) {
        if (it.key().empty() ||
            !(std::isalpha(static_cast<unsigned char>(it.key().front())) || it.key().front() == '_') ||
            !std::all_of(it.key().begin() + 1, it.key().end(), [](unsigned char c) {
                return std::isalnum(c) || c == '_';
            }) ||
            !(it.value().is_string() || it.value().is_number()))
            throw std::runtime_error("variables must map names to string or number values");
        variables.emplace(it.key(), it.value());
    }
    std::set<std::string> used;
    const auto resolve_string = [&](std::string value, bool typed,
                                    const std::string& path) -> nlohmann::json {
        std::optional<nlohmann::json> exact;
        std::string output;
        std::size_t cursor = 0;
        while (true) {
            const auto begin = value.find("${", cursor);
            if (begin == std::string::npos) {
                output.append(value, cursor, std::string::npos);
                break;
            }
            output.append(value, cursor, begin - cursor);
            const auto end = value.find('}', begin + 2);
            if (end == std::string::npos)
                throw std::runtime_error("unterminated variable reference in " + path);
            const auto name = value.substr(begin + 2, end - begin - 2);
            const auto found = variables.find(name);
            if (found == variables.end())
                throw std::runtime_error("unknown render variable '${" + name + "}' in " + path);
            used.insert(name);
            if (typed && begin == 0 && end + 1 == value.size()) {
                exact = found->second;
                break;
            }
            output += found->second.is_string() ? found->second.get<std::string>()
                                                : found->second.dump();
            cursor = end + 1;
        }
        if (exact) return *exact;
        return output;
    };
    const auto walk = [&](const auto& self, nlohmann::json& value,
                          const std::string& path, const std::string& parent_key) -> void {
        if (value.is_object()) {
            for (auto it = value.begin(); it != value.end(); ++it) {
                if (it.key().find("${") != std::string::npos)
                    throw std::runtime_error("variable references are not allowed in object keys");
                self(self, it.value(), path + "." + it.key(), it.key());
            }
        } else if (value.is_array()) {
            for (std::size_t i = 0; i < value.size(); ++i)
                self(self, value[i], path + "[" + std::to_string(i) + "]", parent_key);
        } else if (value.is_string()) {
            const auto text = value.get<std::string>();
            if (text.find("${") != std::string::npos) {
                if (parent_key == "property" || parent_key == "type")
                    throw std::runtime_error("render variables are not allowed in property/type names");
                value = resolve_string(text, true, path);
            }
        }
    };
    result.plan.erase("variables");
    walk(walk, result.plan, "$", {});
    for (const auto& [name, unused] : variables) {
        (void)unused;
        if (!used.contains(name)) result.unused.push_back(name);
    }
    return result;
}

}  // namespace

namespace chronon3d::render_plan {
using detail::apply_layout_stagger;
using detail::collect_final_key_timing_warnings;
using detail::find_3d_track;
using detail::validate_per_key_timing;

Result<RenderPlan, PlanDecodeError> decode_render_plan(const nlohmann::json& root) {
    ResolvedVariables resolved;
    try {
        resolved = resolve_variables(root);
    } catch (const std::exception& error) {
        return PlanDecodeError{"variables", error.what(), "invalid_render_variables", "render-plan-variables"};
    }
    const auto& resolved_root = resolved.plan;
    // Check the canonical layer-track inventory before JSON Schema so callers
    // get the registered property's actionable diagnostic instead of only an
    // enum mismatch from the structural validator.
    if (resolved_root.is_object() && resolved_root.contains("layers") &&
        resolved_root.at("layers").is_array()) {
        for (std::size_t layer_index = 0; layer_index < resolved_root.at("layers").size(); ++layer_index) {
            const auto& layer = resolved_root.at("layers")[layer_index];
            if (!layer.is_object() || !layer.contains("animation") ||
                !layer.at("animation").is_object() || !layer.at("animation").contains("tracks") ||
                !layer.at("animation").at("tracks").is_array()) continue;
            const auto& tracks = layer.at("animation").at("tracks");
            for (std::size_t track_index = 0; track_index < tracks.size(); ++track_index) {
                const auto& track = tracks[track_index];
                if (!track.is_object() || !track.contains("property") ||
                    !track.at("property").is_string()) continue;
                const auto property = track.at("property").get<std::string>();
                if (find_animation_property(property)) continue;
                std::string message = "unregistered property '" + property + "'";
                if (const auto nearest = nearest_animation_property_name(property); !nearest.empty())
                    message += "; did you mean '" + std::string{nearest} + "'?";
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].animation.tracks[" +
                    std::to_string(track_index) + "].property", std::move(message)};
            }
        }
    }
    const auto validation = validate_render_plan(resolved_root);
    if (!validation.ok())
        return PlanDecodeError{"", validation.format()};

    try {
        validate_per_key_timing(resolved_root);
        RenderPlan plan;
        plan.warnings.reserve(resolved.unused.size());
        for (const auto& name : resolved.unused)
            plan.warnings.push_back("unused render variable '" + name + "'");
        if (resolved_root.contains("warnings"))
            for (const auto& warning : resolved_root.at("warnings"))
                plan.warnings.push_back(warning.get<std::string>());
        collect_final_key_timing_warnings(resolved_root, plan.warnings);
        plan.schema = resolved_root.at("schema").get<std::string>();
        plan.job_id = resolved_root.value("job_id", std::string{"chronon_plan"});
        if (resolved_root.contains("color_management")) {
            const auto& raw = resolved_root.at("color_management");
            RenderPlan::ColorManagementPlan cm;
            cm.ocio_config = raw.at("ocio_config").get<std::string>();
            cm.working_space = raw.at("working_space").get<std::string>();
            cm.video_input_space = raw.value("video_input_space", std::string{});
            cm.output_display = raw.value("output_display", std::string{});
            cm.output_view = raw.value("output_view", std::string{});
            if (cm.output_display.empty() != cm.output_view.empty())
                return PlanDecodeError{"color_management",
                    "output_display and output_view must be specified together",
                    "incomplete_ocio_output_view", "render-plan-color"};
            if (cm.ocio_config.empty() || cm.working_space.empty())
                return PlanDecodeError{"color_management",
                    "ocio_config and working_space must be non-empty",
                    "invalid_color_management", "render-plan-color"};
            if (detail::invalid_logical_path(cm.ocio_config))
                return PlanDecodeError{"color_management.ocio_config",
                    "OCIO config must be a relative path inside the mounted asset root",
                    "invalid_ocio_config_path", "render-plan-color"};
            plan.color_management = std::move(cm);
        }
        const auto& canvas = resolved_root.at("canvas");
        plan.canvas = CanvasSpec{
            canvas.at("width").get<int>(),
            canvas.at("height").get<int>(),
            FrameRate{canvas.at("fps_num").get<int>(), canvas.at("fps_den").get<int>()},
            Frame{canvas.at("duration_frames").get<std::int64_t>()}};
        if (resolved_root.contains("audio")) {
            const auto& raw_mix = resolved_root.at("audio");
            AudioMixPlan mix;
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
                if (detail::invalid_logical_path(clip.asset))
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
                    if ((track.easing == "bezier") != track.bezier_curve.has_value())
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
                        if (key.roving && (interpolation == "bezier" || interpolation == "auto_bezier"))
                            throw std::runtime_error("audio gain roving cannot combine with Bezier interpolation");
                        if (has_handles && interpolation != "bezier")
                            throw std::runtime_error("audio gain temporal handles require interpolation='bezier'");
                        if (raw_key.contains("roving") && !raw_key.at("roving").is_boolean())
                            throw std::runtime_error("audio gain roving must be a boolean");
                        if (key.interpolation && *key.interpolation != "linear" &&
                            *key.interpolation != "hold" && *key.interpolation != "bezier" &&
                            *key.interpolation != "auto_bezier")
                            throw std::runtime_error("audio gain interpolation must be linear, hold, bezier, or auto_bezier");
                        if (interpolation == "bezier" && !key.bezier_curve && !has_handles)
                            throw std::runtime_error("audio gain bezier interpolation needs bezier_curve or temporal handles");
                        if (interpolation != "bezier" && key.bezier_curve)
                            throw std::runtime_error("audio gain bezier_curve requires interpolation='bezier'");
                        if (!key.interpolation && ((key.easing == "bezier") != key.bezier_curve.has_value()))
                            throw std::runtime_error("audio gain easing='bezier' requires bezier_curve");
                        if (interpolation == "auto_bezier" && (key.easing || key.bezier_curve))
                            throw std::runtime_error("audio gain auto_bezier cannot combine with per-key easing or bezier_curve");
                        if (interpolation == "auto_bezier" && (key.temporal_in_dx || key.temporal_in_dy ||
                            key.temporal_out_dx || key.temporal_out_dy || key.bezier_curve))
                            throw std::runtime_error("audio gain auto_bezier cannot combine with authored handles");
                        if (key.roving && interpolation == "auto_bezier")
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
                    if (timeline_end > plan.canvas.duration.integral())
                        plan.warnings.emplace_back("audio_clip_tail_clipped:" + clip.id);
                }
                mix.clips.push_back(std::move(clip));
            }
            if (mix.clips.empty())
                return PlanDecodeError{"audio.clips", "audio mix must contain at least one clip",
                    "empty_audio_mix", "render-plan-audio"};
            plan.audio = std::move(mix);
        }
        if (resolved_root.contains("temporal_motion_blur")) {
            const auto& raw = resolved_root.at("temporal_motion_blur");
            MotionBlurPlan blur;
            blur.shutter_angle = raw.value("shutter_angle", 180.0f);
            blur.samples = raw.value("samples", std::uint32_t{12});
            if (!std::isfinite(blur.shutter_angle) || blur.shutter_angle <= 0.0f ||
                blur.shutter_angle > 360.0f || blur.samples < 2 || blur.samples > 64)
                return PlanDecodeError{
                    "temporal_motion_blur",
                    "requires shutter_angle in (0, 360] and samples in [2, 64]",
                    "invalid_temporal_motion_blur", "render-plan-decoder"};
            if (std::any_of(resolved_root.at("layers").begin(),
                            resolved_root.at("layers").end(), [](const auto& layer) {
                                return layer.is_object() && layer.contains("motion_blur");
                            }))
                return PlanDecodeError{
                    "temporal_motion_blur",
                    "cannot be combined with per-layer analytic motion_blur",
                    "conflicting_motion_blur_modes", "render-plan-decoder"};
            plan.temporal_motion_blur = blur;
        }
        if (resolved_root.contains("camera"))
            plan.camera = detail::decode_camera(resolved_root.at("camera"));
        if (resolved_root.contains("camera_shake")) {
            if (plan.schema != kRenderPlanSchemaV3 || !plan.camera)
                return PlanDecodeError{"camera_shake", "camera_shake requires a V3 camera", "camera_shake_requires_v3_camera", "render-plan-camera"};
            const auto& raw = resolved_root.at("camera_shake");
            CameraShakePlan shake;
            shake.amplitude_px = raw.value("amplitude_px", shake.amplitude_px);
            shake.frequency_hz = raw.value("frequency_hz", shake.frequency_hz);
            shake.decay = raw.value("decay", shake.decay);
            shake.seed = raw.value("seed", shake.seed);
            shake.rotation_amplitude_deg = raw.value("rotation_amplitude_deg", shake.rotation_amplitude_deg);
            shake.start_frame = Frame{raw.value("start_frame", std::int64_t{0})};
            shake.duration_frames = Frame{raw.value("duration_frames", std::int64_t{0})};
            if (!std::isfinite(shake.amplitude_px) || shake.amplitude_px < 0.0f || shake.amplitude_px > 4096.0f ||
                !std::isfinite(shake.frequency_hz) || shake.frequency_hz <= 0.0f || shake.frequency_hz > 120.0f ||
                !std::isfinite(shake.decay) || shake.decay < 0.0f || shake.decay > 100.0f ||
                !std::isfinite(shake.rotation_amplitude_deg) || shake.rotation_amplitude_deg < 0.0f || shake.rotation_amplitude_deg > 360.0f ||
                shake.start_frame.integral() < 0 || shake.start_frame.integral() > 1000000 ||
                shake.duration_frames.integral() < 0 || shake.duration_frames.integral() > 1000000)
                return PlanDecodeError{"camera_shake", "camera_shake values are outside supported bounds", "invalid_camera_shake", "render-plan-camera"};
            plan.camera_shake = shake;
        }
        if (resolved_root.contains("lighting_rig")) {
            if (plan.schema != kRenderPlanSchemaV3)
                return PlanDecodeError{"lighting_rig", "lighting_rig is available only in render-plan V3", "v3_lighting_in_v2", "render-plan-lighting"};
            const auto& raw = resolved_root.at("lighting_rig");
            LightingRigPlan rig;
            const auto read_array = [&](const char* key, auto& target) {
                if (!raw.contains(key)) return;
                for (std::size_t i = 0; i < target.size(); ++i)
                    target[i] = raw.at(key).at(i).get<float>();
            };
            const auto read_color = [&](const char* key, auto& target) {
                if (!raw.contains(key)) return;
                const auto& color = raw.at(key);
                for (std::size_t i = 0; i < target.size(); ++i)
                    target[i] = color.at(i).get<float>();
            };
            read_array("key_direction", rig.key_direction);
            read_color("key_color", rig.key_color);
            read_color("ambient_color", rig.ambient_color);
            read_color("rim_color", rig.rim_color);
            rig.key_intensity = raw.value("key_intensity", rig.key_intensity);
            rig.ambient_intensity = raw.value("ambient_intensity", rig.ambient_intensity);
            rig.rim_enabled = raw.value("rim_enabled", rig.rim_enabled);
            rig.rim_intensity = raw.value("rim_intensity", rig.rim_intensity);
            rig.rim_power = raw.value("rim_power", rig.rim_power);
            const auto finite_range = [](float value, float low, float high) {
                return std::isfinite(value) && value >= low && value <= high;
            };
            if (!std::all_of(rig.key_direction.begin(), rig.key_direction.end(),
                    [](float value) { return std::isfinite(value) && std::abs(value) <= 1.0f; }) ||
                std::sqrt(rig.key_direction[0] * rig.key_direction[0] + rig.key_direction[1] * rig.key_direction[1] + rig.key_direction[2] * rig.key_direction[2]) < 1e-6f ||
                !std::all_of(rig.key_color.begin(), rig.key_color.end(), [](float v) { return std::isfinite(v) && v >= 0 && v <= 1; }) ||
                !std::all_of(rig.ambient_color.begin(), rig.ambient_color.end(), [](float v) { return std::isfinite(v) && v >= 0 && v <= 1; }) ||
                !std::all_of(rig.rim_color.begin(), rig.rim_color.end(), [](float v) { return std::isfinite(v) && v >= 0 && v <= 1; }) ||
                !finite_range(rig.key_intensity, 0.0f, 16.0f) ||
                !finite_range(rig.ambient_intensity, 0.0f, 16.0f) ||
                !finite_range(rig.rim_intensity, 0.0f, 16.0f) ||
                !finite_range(rig.rim_power, 1e-6f, 32.0f))
                return PlanDecodeError{"lighting_rig", "lighting rig values are outside their supported finite ranges", "invalid_lighting_rig", "render-plan-lighting"};
            plan.lighting_rig = rig;
        }
        if (resolved_root.contains("camera_animation")) {
            if (!plan.camera)
                return PlanDecodeError{"camera_animation",
                    "camera_animation requires a camera object",
                    "camera_animation_requires_camera", "render-plan-decoder"};
            plan.camera_animation = detail::decode_camera_tracks(resolved_root.at("camera_animation"));
            const bool has_dof_animation = std::any_of(
                plan.camera_animation->begin(), plan.camera_animation->end(), [](const auto& track) {
                    return track.property == chronon3d::render_plan::CameraPropertyPlan::FocusDistance ||
                           track.property == chronon3d::render_plan::CameraPropertyPlan::Aperture ||
                           track.property == chronon3d::render_plan::CameraPropertyPlan::MaxBlur;
                });
            if (has_dof_animation && !plan.camera->dof_enabled)
                return PlanDecodeError{"camera_animation",
                    "animated DOF channels require camera.dof.enabled=true",
                    "camera_dof_animation_requires_dof", "render-plan-decoder"};
        }

        std::size_t layer_index = 0;
        for (const auto& layer_value : resolved_root.at("layers")) {
            auto layer = detail::decode_layer(layer_value);
            const auto transition_error = [&](const std::optional<TransitionPlan>& transition,
                                              const char* slot) -> std::optional<PlanDecodeError> {
                if (!transition) return std::nullopt;
                const auto lifetime = layer.duration_frames.value_or(plan.canvas.duration).integral();
                const auto span = transition->duration_frames.integral() +
                                  transition->delay_frames.integral();
                if (transition->duration_frames.integral() < 1 || span > lifetime)
                    return PlanDecodeError{
                        "layers[" + std::to_string(layer_index) + "]." + slot,
                        "transition duration plus delay must fit within the layer lifetime",
                        "invalid_transition_duration", "render-plan-transition"};
                return std::nullopt;
            };
            if (const auto error = transition_error(layer.transition_in, "transition_in")) return *error;
            if (const auto error = transition_error(layer.transition_out, "transition_out")) return *error;
            if (layer.safe_insets &&
                ((*layer.safe_insets)[0] + (*layer.safe_insets)[2] >=
                     static_cast<float>(plan.canvas.height) ||
                 (*layer.safe_insets)[1] + (*layer.safe_insets)[3] >=
                     static_cast<float>(plan.canvas.width))) {
                return PlanDecodeError{
                    "layers[" + std::to_string(layer_index) + "].safe_insets",
                    "safe insets must leave a non-empty canvas area",
                    "invalid_safe_insets", "render-plan-decoder"};
            }
            const bool explicit_3d_vector =
                has_explicit_3d_vector(layer_value, "position") ||
                has_explicit_3d_vector(layer_value, "scale") ||
                has_explicit_3d_vector(layer_value, "rotation");
            const auto offending_track = find_3d_track(layer_value);
            if (layer.depth_role && has_explicit_3d_vector(layer_value, "position") &&
                std::abs(layer.position[2]) > 0.0f) {
                return PlanDecodeError{
                    "layers[" + std::to_string(layer_index) + "].depth_role",
                    "use depth_role + depth_offset, or explicit position_z, not both",
                    "depth_role_position_conflict", "render-plan-decoder"};
            }
            if (!layer.enable_3d && (explicit_3d_vector || offending_track)) {
                std::string reason = "3D transform data requires enable_3d=true";
                if (offending_track) {
                    reason += "; offending track: " + *offending_track;
                }
                return PlanDecodeError{
                    "layers[" + std::to_string(layer_index) + "].enable_3d",
                    std::move(reason),
                    "3d_requires_explicit_enable",
                    "render-plan-decoder"};
            }
            if (detail::invalid_logical_path(layer.asset)) {
                return PlanDecodeError{"layers[].asset",
                    "asset references must be relative logical paths"};
            }
            if (detail::invalid_logical_path(layer.source)) {
                return PlanDecodeError{"layers[].source",
                    "source references must be relative logical paths"};
            }
            if (detail::invalid_logical_path(layer.font)) {
                return PlanDecodeError{"layers[].style.font",
                    "font references must be relative logical paths"};
            }
            if (layer.type == LayerType::Text) {
                if (!layer.style) {
                    return PlanDecodeError{"layers[].style",
                        "text layers require a concrete style object"};
                }
                if (layer.font.empty()) {
                    return PlanDecodeError{"layers[].style.font",
                        "text layers require an explicit prepared font asset"};
                }
                if (!layer.style->font_size || *layer.style->font_size <= 0.0f) {
                    return PlanDecodeError{"layers[].style.font_size",
                        "text layers require a positive concrete font_size"};
                }
                if (layer.style->fill.empty()) {
                    return PlanDecodeError{"layers[].style.fill",
                        "text layers require a concrete fill color"};
                }
                if (layer.text_counter) {
                    const auto& counter = layer.text_counter->counter;
                    if (counter.start_frame.integral() + counter.duration_frames.integral() >
                        plan.canvas.duration.integral())
                        return PlanDecodeError{
                            "layers[" + std::to_string(layer_index) + "].text_counter.counter",
                            "counter start_frame plus duration_frames must fit the canvas duration",
                            "text_counter_outside_canvas", "render-plan-text-counter"};
                }
            }
            plan.layers.push_back(std::move(layer));
            ++layer_index;
        }

        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            auto& layer = plan.layers[index];
            if (!layer.follow_path) continue;
            auto& driver = *layer.follow_path;
            if (!driver.path_id.empty()) {
                const auto source = std::find_if(plan.layers.begin(), plan.layers.end(),
                    [&](const LayerPlan& candidate) { return candidate.id == driver.path_id; });
                if (source == plan.layers.end() || !source->shape || source->shape->path.size() < 2)
                    return PlanDecodeError{"layers[" + std::to_string(index) + "].follow_path.path_id",
                        "path_id must name a layer with inline path commands", "follow_path_source_missing",
                        "render-plan-follow-path"};
                driver.path_commands = source->shape->path;
            }
            if (driver.path_commands.size() < 2)
                return PlanDecodeError{"layers[" + std::to_string(index) + "].follow_path",
                    "follow_path requires path commands", "follow_path_path_missing", "render-plan-follow-path"};
            if (layer.animation) {
                for (const auto& track : layer.animation->tracks) {
                    if (track.property == "position" || track.property == "position_x" ||
                        track.property == "position_y" || track.property == "position_z")
                        return PlanDecodeError{"layers[" + std::to_string(index) + "].animation",
                            "follow_path owns position; remove explicit position tracks", "follow_path_position_conflict",
                            "render-plan-follow-path"};
                }
            }
        }

        if (const auto stagger_error = apply_layout_stagger(
                plan.layers, plan.canvas.width, plan.canvas.height))
            return *stagger_error;

        for (auto& particle_layer : plan.layers) {
            if (!particle_layer.particle_emitter ||
                particle_layer.particle_emitter->shape != EmitterShape::Path) continue;
            const auto source=std::find_if(plan.layers.begin(),plan.layers.end(),[&](const LayerPlan& candidate) {
                return candidate.id==particle_layer.particle_emitter->path_id;
            });
            if(source==plan.layers.end() || !source->shape || source->shape->type!=ShapeContentTypePlan::Path)
                return PlanDecodeError{"layers[].particle_emitter.path_id",
                    "path emitter source must name a path shape layer",
                    "particle_path_source_missing","render-plan-relations"};
            auto& points=particle_layer.particle_emitter->path_points;
            points.clear();
            chronon3d::Vec2 current{0.0f,0.0f}, subpath_start{0.0f,0.0f};
            bool has_current=false;
            const auto append=[&](chronon3d::Vec2 point) {
                const auto differs=[&] {
                    const float dx=points.back().x-point.x,dy=points.back().y-point.y;
                    return std::sqrt(dx*dx+dy*dy)>1.0e-5f;
                };
                if(points.empty() || differs()) points.push_back(point);
            };
            for(const auto& command:source->shape->path) {
                const chronon3d::Vec2 end{command.point[0],command.point[1]};
                if(command.type==ShapePathCommandTypePlan::MoveTo) {
                    current=end; subpath_start=end; has_current=true; append(end);
                } else if(command.type==ShapePathCommandTypePlan::LineTo && has_current) {
                    current=end; append(end);
                } else if(command.type==ShapePathCommandTypePlan::QuadraticTo && has_current) {
                    const chronon3d::Vec2 control{command.control1[0],command.control1[1]}, start=current;
                    for(int step=1;step<=16;++step) { const float t=step/16.0f,u=1.0f-t; append(u*u*start+2*u*t*control+t*t*end); }
                    current=end;
                } else if(command.type==ShapePathCommandTypePlan::CubicTo && has_current) {
                    const chronon3d::Vec2 c1{command.control1[0],command.control1[1]},c2{command.control2[0],command.control2[1]},start=current;
                    for(int step=1;step<=24;++step) { const float t=step/24.0f,u=1.0f-t; append(u*u*u*start+3*u*u*t*c1+3*u*t*t*c2+t*t*t*end); }
                    current=end;
                } else if(command.type==ShapePathCommandTypePlan::Close && has_current) {
                    append(subpath_start); current=subpath_start;
                }
            }
            if(points.size()<2)
                return PlanDecodeError{"layers[].particle_emitter.path_id",
                    "resolved path must contain at least two distinct points",
                    "particle_path_degenerate","render-plan-relations"};
        }

        for (std::size_t i = 0; i < plan.layers.size(); ++i) {
            if (plan.layers[i].ocio_input_space.empty()) continue;
            if (!plan.color_management)
                return PlanDecodeError{"layers[" + std::to_string(i) + "].ocio_input_space",
                    "ocio_input_space requires the plan color_management contract",
                    "missing_color_management", "render-plan-color"};
            if (!plan.layers[i].input_color_space.empty() &&
                plan.layers[i].input_color_space != "srgb")
                return PlanDecodeError{"layers[" + std::to_string(i) + "].input_color_space",
                    "use either input_color_space or ocio_input_space, not both",
                    "conflicting_input_color_spaces", "render-plan-color"};
        }

        if (const auto relation_error = validate_render_plan_layer_relations(plan))
            return *relation_error;

        const auto& output = resolved_root.at("output");
        plan.output.path = output.at("path").get<std::string>();
        plan.output.color_space = output.value("color_space", std::string{"srgb"});
        plan.output.reference_white_nits = output.value(
            "reference_white_nits", 100.0f);
        if (plan.output.color_space != "srgb" &&
            plan.output.color_space != "rec709" &&
            plan.output.color_space != "linearsrgb" &&
            plan.output.color_space != "pq" &&
            plan.output.color_space != "hlg")
            return PlanDecodeError{"output.color_space",
                "must be srgb, rec709, linearsrgb, pq, or hlg",
                "output_color_space_invalid", "render-plan-color"};
        if (!std::isfinite(plan.output.reference_white_nits) ||
            plan.output.reference_white_nits <= 0.0f ||
            plan.output.reference_white_nits > 10000.0f)
            return PlanDecodeError{"output.reference_white_nits",
                "must be finite, greater than 0, and at most 10000 nits",
                "reference_white_invalid", "render-plan-color"};
        if (output.contains("format"))
            plan.output.format = detail::output_format(output.at("format").get<std::string>());
        if (output.contains("codec"))
            plan.output.codec = detail::video_codec(output.at("codec").get<std::string>());
        if (output.contains("hdr_mastering_metadata")) {
            const auto& raw = output.at("hdr_mastering_metadata");
            chronon3d::color::HdrMasteringMetadata metadata;
            const auto read_xy = [&](const nlohmann::json& point)
                -> std::optional<std::array<float, 2>> {
                if (!point.is_array() || point.size() != 2)
                    return std::nullopt;
                const float x = point.at(0).get<float>();
                const float y = point.at(1).get<float>();
                if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0f ||
                    y < 0.0f || x > 1.0f || y > 1.0f || x + y > 1.0f)
                    return std::nullopt;
                return std::array<float, 2>{x, y};
            };
            const auto& primaries = raw.at("display_primaries");
            bool valid_metadata = primaries.is_array() && primaries.size() == 3;
            if (valid_metadata) {
                for (std::size_t index = 0; index < 3; ++index) {
                    const auto point = read_xy(primaries.at(index));
                    if (!point) { valid_metadata = false; break; }
                    metadata.display_primaries[index] = *point;
                }
            }
            const auto white_point = read_xy(raw.at("white_point"));
            if (white_point) metadata.white_point = *white_point;
            else valid_metadata = false;
            metadata.min_luminance_nits = raw.at("min_luminance_nits").get<float>();
            metadata.max_luminance_nits = raw.at("max_luminance_nits").get<float>();
            valid_metadata = valid_metadata &&
                std::isfinite(metadata.min_luminance_nits) &&
                std::isfinite(metadata.max_luminance_nits) &&
                metadata.min_luminance_nits >= 0.0f &&
                metadata.max_luminance_nits > metadata.min_luminance_nits &&
                metadata.max_luminance_nits <= 10000.0f;
            if (raw.contains("content_light")) {
                const auto& light = raw.at("content_light");
                chronon3d::color::ContentLightMetadata content{
                    light.at("max_cll_nits").get<std::uint32_t>(),
                    light.at("max_fall_nits").get<std::uint32_t>()};
                if (content.max_fall_nits > content.max_cll_nits ||
                    content.max_cll_nits > 10000 || content.max_fall_nits > 10000)
                    valid_metadata = false;
                metadata.content_light = content;
            }
            if (!valid_metadata)
                return PlanDecodeError{"output.hdr_mastering_metadata",
                    "mastering xy coordinates must be finite valid chromaticities; luminance must satisfy 0 <= min < max <= 10000 nits and MaxFALL <= MaxCLL",
                    "hdr_mastering_metadata_invalid", "render-plan-color"};
            if ((plan.output.color_space != "pq" && plan.output.color_space != "hlg") ||
                (plan.output.format != OutputFormat::Mp4 &&
                 plan.output.format != OutputFormat::Mkv &&
                 plan.output.format != OutputFormat::WebM))
                return PlanDecodeError{"output.hdr_mastering_metadata",
                    "HDR mastering metadata requires PQ/HLG video output",
                    "hdr_mastering_requires_hdr_video", "render-plan-color"};
            plan.output.hdr_mastering_metadata = std::move(metadata);
        }
        plan.output.bitrate = output.value("bitrate", std::int64_t{0});
        plan.output.crf = output.value("crf", 0);
        plan.output.qp = output.value("qp", -1);
        const auto rate_control = output.value("rate_control", "crf");
        if (rate_control == "qp") plan.output.rate_control = RateControlMode::ConstantQp;
        else if (rate_control == "bitrate") plan.output.rate_control = RateControlMode::Bitrate;
        else if (rate_control == "crf") plan.output.rate_control = RateControlMode::Crf;
        else return PlanDecodeError{"output.rate_control", "must be crf, qp, or bitrate"};
        plan.output.profile_id = output.value("profile_id", std::string{});
        if (plan.output.profile_id == "velox-h264-1080p30-v1" &&
            (plan.canvas.width != 1920 || plan.canvas.height != 1080 ||
             plan.canvas.fps != FrameRate{30, 1})) {
            return PlanDecodeError{"canvas", "velox-h264-1080p30-v1 requires 1920x1080 at 30 fps"};
        }
        if (resolved_root.contains("budget")) {
            const auto& budget = resolved_root.at("budget");
            plan.budget.max_temporal_pixels = budget.value(
                "max_temporal_pixels", plan.budget.max_temporal_pixels);
        }
        if (const auto budget_error = validate_render_budget(plan))
            return *budget_error;
        plan.content_fingerprint = compute_render_plan_content_fingerprint(plan);
        return plan;
    } catch (const std::exception& error) {
        return PlanDecodeError{"", error.what()};
    }
}

}  // namespace chronon3d::render_plan
