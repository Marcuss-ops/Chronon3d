#include <chronon3d/scene/model/shape/path_operator.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/effects/cube_lut.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>
#include <cmath>
#include <string>
#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/assets/svg_path_loader.hpp>

#include "render_plan_compiler_detail.hpp"
#include "audio_react_baker.hpp"

#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/graphics/shape_style/stroke_style.hpp>
#include <chronon3d/scene/builders/text_run_materialization.hpp>
#include <chronon3d/text/text_run_builder.hpp>
#include <chronon3d/text/animated_text_document.hpp>
#include "../scene/camera/camera_v1/camera_program_sources.hpp"
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/timeline/composition_definition.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <locale>
#include <sstream>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace chronon3d::render_plan {

namespace {
std::uint64_t shake_mix64(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

double shake_value_noise(double sample, std::uint32_t seed, std::uint32_t channel) noexcept {
    const auto cell = static_cast<std::int64_t>(std::floor(sample));
    const double fraction = sample - static_cast<double>(cell);
    const auto value_at = [seed, channel](std::int64_t index) {
        const auto bits = shake_mix64(static_cast<std::uint64_t>(index) ^
            (static_cast<std::uint64_t>(seed) << 32U) ^ channel);
        return static_cast<double>(bits >> 11U) * (1.0 / 9007199254740992.0) * 2.0 - 1.0;
    };
    const double smooth = fraction * fraction * fraction *
        (fraction * (fraction * 6.0 - 15.0) + 10.0);
    return value_at(cell) + (value_at(cell + 1) - value_at(cell)) * smooth;
}

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
    const double nyquist = 24000.0;
    if (high > nyquist) throw std::runtime_error("audio_react band upper edge exceeds the 48 kHz analysis Nyquist frequency");
    constexpr std::size_t kBins = 32;
    double power_sum = 0.0;
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        const double ratio = (static_cast<double>(bin) + 0.5) / kBins;
        const double frequency = low * std::pow(high / low, ratio);
        const double omega = 2.0 * std::numbers::pi * frequency / 48000.0;
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

void bake_audio_react(RenderPlan& plan, chronon3d::assets::AssetResolver& resolver) {
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
            if (!detail::decode_audio_mono_48k(resolved->string(), samples, decode_error))
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
                        detail::animation_keyframe_easing(*clip.gain, key));
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
                    static_cast<double>(source_frame) * 48000.0 / fps));
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
} // namespace

Result<PreparedRenderPlan, PlanDecodeError> compile_render_plan(
    const RenderPlan& input_plan,
    chronon3d::assets::AssetResolver& resolver,
    const RenderPlanFingerprintOptions& fingerprint_options,
    const compiler::CompiledProgram* canonical_program) {
    try {
        auto expanded_plan = input_plan;
        auto& plan = expanded_plan;
        bake_audio_react(plan, resolver);
        for (std::size_t layer_index = 0; layer_index < plan.layers.size(); ++layer_index) {
            auto& layer = plan.layers[layer_index];
            if (!layer.animation) continue;
            for (std::size_t track_index = 0; track_index < layer.animation->tracks.size(); ++track_index) {
                auto& track = layer.animation->tracks[track_index];
                if (!track.data_feed || track.data_feed->asset.empty()) continue;
                auto& feed = *track.data_feed;
                const auto path = "layers[" + std::to_string(layer_index) + "].animation.tracks[" +
                    std::to_string(track_index) + "].data_feed.asset";
                const auto resolved = resolver.resolve_logical(feed.asset);
                if (!resolved)
                    return PlanDecodeError{path, "data_feed asset could not be resolved within the mounted asset root",
                        "data_feed_asset_unresolved", "render-plan-data-feed"};
                std::error_code size_error;
                const auto byte_size = std::filesystem::file_size(*resolved, size_error);
                constexpr std::uintmax_t kMaxDataFeedAssetBytes = 32ULL * 1024ULL * 1024ULL;
                if (size_error || byte_size > kMaxDataFeedAssetBytes)
                    return PlanDecodeError{path, "data_feed asset must be readable and no larger than 32 MiB",
                        "data_feed_asset_size", "render-plan-data-feed"};
                const auto digest = chronon3d::assets::sha256_file(*resolved);
                if (!digest || digest->hex() != feed.sha256)
                    return PlanDecodeError{path, "data_feed asset SHA-256 does not match the declared digest",
                        "data_feed_digest_mismatch", "render-plan-data-feed"};
                try {
                    std::ifstream input(*resolved, std::ios::binary);
                    const auto data = nlohmann::json::parse(input);
                    if (!data.is_array() || data.size() != feed.item_count)
                        throw std::runtime_error("data_feed asset must be a numeric JSON array matching item_count");
                    feed.values.clear();
                    feed.values.reserve(data.size());
                    for (const auto& sample : data) {
                        if (!sample.is_number() || !std::isfinite(sample.get<double>()) ||
                            !std::isfinite(sample.get<float>()))
                            throw std::runtime_error("data_feed asset values must be finite float numbers");
                        feed.values.push_back(sample.get<float>());
                    }
                } catch (const std::exception& error) {
                    return PlanDecodeError{path, error.what(), "data_feed_asset_invalid", "render-plan-data-feed"};
                }
            }
        }
        for (std::size_t layer_index = 0; layer_index < plan.layers.size(); ++layer_index) {
            auto& layer = plan.layers[layer_index];
            if (!layer.shape || !layer.shape->lottie_asset) continue;
            const auto logical = *layer.shape->lottie_asset;
            const auto resolved = resolver.resolve_logical(logical);
            if (!resolved)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.lottie",
                    "Lottie asset could not be resolved within the mounted asset root",
                    "lottie_asset_unresolved", "render-plan-lottie"};
            auto lowered = detail::expand_static_lottie(*layer.shape, *resolved);
            if (lowered)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.lottie",
                    *lowered, "lottie_unsupported_or_invalid", "render-plan-lottie"};
        }
#if defined(CHRONON3D_ENABLE_SVG)
        for (std::size_t layer_index = 0; layer_index < plan.layers.size(); ++layer_index) {
            auto& layer = plan.layers[layer_index];
            if (!layer.shape || !layer.shape->svg_asset) continue;
            const auto logical = *layer.shape->svg_asset;
            const auto resolved = resolver.resolve_logical(logical);
            if (!resolved)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG asset could not be resolved within the mounted asset root",
                    "svg_asset_unresolved", "render-plan-svg"};
            const auto imported = chronon3d::assets::load_svg_document_file(resolved->string());
            if (!imported.ok)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    imported.error, "svg_import_failed", "render-plan-svg"};
            if (imported.elements.empty())
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG contains no path geometry", "svg_empty_geometry", "render-plan-svg"};
            if (imported.elements.size() > 4096)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG styled path count exceeds 4096", "svg_resource_limit", "render-plan-svg"};
            auto& shape = *layer.shape;
            if (shape.operators.size() != 0)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.operators",
                    "SVG asset paths cannot be combined with path operators",
                    "svg_operator_conflict", "render-plan-svg"};
            shape.imported_subpaths.clear();
            shape.imported_subpaths.reserve(imported.elements.size());
            std::size_t total_commands = 0;
            for (const auto& element : imported.elements) {
                if (!std::isfinite(element.opacity) || element.opacity < 0.0f ||
                    element.opacity > 1.0f ||
                    (element.fill && element.fill->enabled && element.fill->type != FillType::Solid) ||
                    element.stroke.gradient)
                    return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                        "SVG opacity must be in [0,1]; only solid path paints are supported",
                        "svg_unsupported_style", "render-plan-svg"};
                ImportedSubpathPlan subpath;
                if (element.fill && element.fill->enabled) {
                    auto color = element.fill->solid;
                    color.a *= element.opacity;
                    subpath.fill_color = std::array<float, 4>{color.r, color.g, color.b, color.a};
                }
                if (element.stroke.enabled) {
                    StrokeStyle stroke;
                    stroke.width = element.stroke.width;
                    auto color = element.stroke.color;
                    color.a *= element.opacity;
                    stroke.solid_color = std::array<float, 4>{color.r, color.g, color.b, color.a};
                    subpath.stroke = std::move(stroke);
                }
                for (const auto& command : element.path.commands) {
                    ShapePathCommandPlan lowered;
                    const auto quantize = [](float value) { return std::round(value * 10000.0f) / 10000.0f; };
                    const auto point = [&](Vec2 value, std::array<float, 2>& out) {
                        if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
                            std::abs(value.x) > 100000.0f || std::abs(value.y) > 100000.0f)
                            throw std::runtime_error("SVG coordinate exceeds finite resource bounds");
                        out = {quantize(value.x), quantize(value.y)};
                    };
                    switch (command.type) {
                        case PathCommandType::MoveTo: lowered.type = ShapePathCommandTypePlan::MoveTo; point(command.p0, lowered.point); break;
                        case PathCommandType::LineTo: lowered.type = ShapePathCommandTypePlan::LineTo; point(command.p0, lowered.point); break;
                        case PathCommandType::QuadraticTo: lowered.type = ShapePathCommandTypePlan::QuadraticTo; point(command.p0, lowered.point); point(command.p1, lowered.control1); break;
                        case PathCommandType::CubicTo: lowered.type = ShapePathCommandTypePlan::CubicTo; point(command.p0, lowered.point); point(command.p1, lowered.control1); point(command.p2, lowered.control2); break;
                        case PathCommandType::Close: lowered.type = ShapePathCommandTypePlan::Close; break;
                    }
                    if (total_commands >= 65536)
                        return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                            "SVG total path command limit exceeded", "svg_resource_limit", "render-plan-svg"};
                    subpath.path.push_back(lowered);
                    ++total_commands;
                }
                shape.imported_subpaths.push_back(std::move(subpath));
            }
        }
#else
        for (std::size_t index = 0; index < plan.layers.size(); ++index)
            if (plan.layers[index].shape && plan.layers[index].shape->svg_asset)
                return PlanDecodeError{"layers[" + std::to_string(index) + "].shape.asset",
                    "Chronon was built without SVG import support", "svg_import_unavailable",
                    "render-plan-svg"};
#endif
        const auto compile_total_t0 = chronon3d::profiling::now();
        double asset_prepare_ms = 0.0;
        double fingerprint_ms = 0.0;
        double media_resolution_ms = 0.0;
        double materialize_layout_ms = 0.0;
        double composition_compile_ms = 0.0;
        double compatibility_view_ms = 0.0;
        if (const auto budget_error = validate_render_budget(plan))
            return *budget_error;

        const auto asset_t0 = chronon3d::profiling::now();
        auto prepared_store = chronon3d::assets::prepare_asset_store(plan, resolver);
        if (!prepared_store) {
            return PlanDecodeError{
                "assets." + prepared_store.error().logical_path,
                prepared_store.error().message,
                prepared_store.error().code ==
                        chronon3d::assets::AssetPreflightErrorCode::MissingAsset
                    ? "MissingAsset" : "",
                "asset_resolver"};
        }
        auto resources = std::move(prepared_store).value();
        asset_prepare_ms = chronon3d::profiling::duration_ms(
            asset_t0, chronon3d::profiling::now());
        auto assets = resources.manifest();

        const auto fingerprint_t0 = chronon3d::profiling::now();
        // Audit FP-1: the canonical job-identity authority, shared with the
        // compiler path so both produce the same digest for the same job.
        const auto fingerprints = compute_render_job_fingerprint(
            plan, assets, fingerprint_options);
        auto content_plan = plan;
        content_plan.job_id.clear();
        content_plan.output = {};
        const auto content_fingerprint =
            compute_render_plan_content_fingerprint(content_plan);
        fingerprint_ms = chronon3d::profiling::duration_ms(
            fingerprint_t0, chronon3d::profiling::now());

        const auto canvas = CanvasInfo::from_dimensions(
            static_cast<float>(plan.canvas.width),
            static_cast<float>(plan.canvas.height));

        const auto media_t0 = chronon3d::profiling::now();
        std::vector<std::string> resolved_video_paths(plan.layers.size());
        std::vector<std::string> resolved_particle_sprite_paths(plan.layers.size());
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            const auto& layer = plan.layers[index];
            if (layer.type != LayerType::Video) continue;
            const auto resolved = resolver.resolve_logical(
                std::filesystem::path(layer.source));
            if (!resolved)
                throw std::runtime_error(
                    "video source was not prepared: " + layer.source);
            resolved_video_paths[index] = resolved->string();
        }
        for (std::size_t index=0; index<plan.layers.size(); ++index) {
            const auto& layer=plan.layers[index];
            if (!layer.particle_emitter || layer.particle_emitter->sprite_asset.empty()) continue;
            const auto resolved=resolver.resolve_logical(std::filesystem::path(layer.particle_emitter->sprite_asset));
            if(!resolved) throw std::runtime_error("particle sprite asset was not prepared: "+layer.particle_emitter->sprite_asset);
            resolved_particle_sprite_paths[index]=resolved->string();
        }
        media_resolution_ms = chronon3d::profiling::duration_ms(
            media_t0, chronon3d::profiling::now());

        CompositionSpec spec;
        spec.name = plan.job_id;
        spec.width = plan.canvas.width;
        spec.height = plan.canvas.height;
        spec.frame_rate = plan.canvas.fps;
        spec.duration = plan.canvas.duration;

        auto font_resolver = std::make_shared<chronon3d::assets::AssetResolver>();
        if (const auto root = resolver.mount_root(); !root.empty())
            font_resolver->mount(root);
        auto font_engine = std::make_shared<chronon3d::FontEngine>(*font_resolver);

        const auto materialize_t0 = chronon3d::profiling::now();
        // Lower each text definition once at the render-plan boundary. The
        // scene callback is evaluated once per frame; keeping PreparedText
        // here avoids rebuilding TextDocument/spans/shaping options on every
        // frame while preserving the existing layout-cache and animator
        // semantics in LayerBuilder::build().
        std::vector<std::optional<chronon3d::PreparedText>> prepared_texts(
            plan.layers.size());
        std::vector<std::shared_ptr<const chronon3d::AnimatedTextDocument>> prepared_text_counters(
            plan.layers.size());
        std::vector<std::optional<chronon3d::Vec2>> prepared_text_bounds(
            plan.layers.size());
        const auto assets_root = resolver.mount_root();
        chronon3d::TextCompileServices text_compile_services{
            font_engine.get(), nullptr,
            assets_root.empty() ? std::filesystem::path{}
                                : assets_root / "fonts"};
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            const auto& layer = plan.layers[index];
            if (layer.type != LayerType::Text) continue;
            auto definition = detail::materialize_text(layer, canvas);
            const auto resolved = resolver.resolve_logical(
                std::filesystem::path(definition.style.font.font_path));
            if (!resolved) {
                throw std::runtime_error(
                    "font asset was not prepared: " +
                    definition.style.font.font_path);
            }
            definition.style.font.font_path = resolved->string();
            if (layer.text_counter) {
                const auto& counter = layer.text_counter->counter;
                AnimationTrackPlan value_track;
                value_track.property = "text_counter";
                value_track.easing = counter.easing;
                value_track.keyframes = {
                    {counter.start_frame, {counter.from}},
                    {counter.start_frame + counter.duration_frames, {counter.to}}};
                chronon3d::AnimatedTextDocument animated;
                const auto replace_token = [&](float value) {
                    double magnitude = value;
                    std::string compact_suffix;
                    if (counter.format == "percent") magnitude *= 100.0;
                    if (counter.format == "compact") {
                        const double absolute = std::abs(magnitude);
                        if (absolute >= 1.0e9) { magnitude /= 1.0e9; compact_suffix = "B"; }
                        else if (absolute >= 1.0e6) { magnitude /= 1.0e6; compact_suffix = "M"; }
                        else if (absolute >= 1.0e3) { magnitude /= 1.0e3; compact_suffix = "K"; }
                    }
                    std::ostringstream formatted;
                    formatted.imbue(std::locale::classic());
                    formatted << std::fixed << std::setprecision(counter.decimals) << magnitude;
                    std::string number = formatted.str();
                    if (counter.decimals == 0)
                        number = std::to_string(static_cast<std::int64_t>(std::llround(magnitude)));
                    if (counter.format == "percent") number += "%";
                    else if (counter.format == "currency") {
                        const std::string symbol = counter.currency == "USD" ? "$" :
                            counter.currency == "EUR" ? "€" : counter.currency == "GBP" ? "£" : counter.currency + " ";
                        number = symbol + number;
                    } else if (counter.format == "compact") number += compact_suffix;
                    return counter.prefix + number + counter.suffix;
                };
                const auto fill_value = [&](float value) {
                    auto target = definition;
                    const auto found = target.content.value.find(layer.text_counter->token);
                    target.content.value.replace(found, layer.text_counter->token.size(), replace_token(value));
                    const auto token_start = found;
                    const auto token_end = found + layer.text_counter->token.size();
                    const auto replacement_size = target.content.value.size() -
                        (definition.content.value.size() - layer.text_counter->token.size());
                    const auto delta = static_cast<std::int64_t>(replacement_size) -
                        static_cast<std::int64_t>(layer.text_counter->token.size());
                    for (auto& span : target.spans) {
                        if (span.byte_end <= token_start) continue;
                        if (span.byte_start < token_end)
                            throw std::runtime_error("text_counter token cannot overlap styled text spans");
                        const auto shifted_start = static_cast<std::int64_t>(span.byte_start) + delta;
                        const auto shifted_end = static_cast<std::int64_t>(span.byte_end) + delta;
                        if (shifted_start < 0 || shifted_end < shifted_start)
                            throw std::runtime_error("text_counter span shift overflow");
                        span.byte_start = static_cast<std::size_t>(shifted_start);
                        span.byte_end = static_cast<std::size_t>(shifted_end);
                    }
                    return target;
                };
                for (std::int64_t offset = 0;
                     offset <= counter.duration_frames.integral(); ++offset) {
                    const Frame frame = counter.start_frame + Frame{offset};
                    const float value = detail::evaluate_scalar_track(
                        value_track, static_cast<double>(frame.integral()),
                        counter.from, plan.canvas.fps);
                    const auto definition_at_frame = fill_value(value);
                    chronon3d::SourceTextKeyframe key;
                    key.frame = frame;
                    key.document = chronon3d::to_text_document(definition_at_frame);
                    animated.add_keyframe(std::move(key));
                }
                const auto to_definition = fill_value(counter.to);
                prepared_text_counters[index] =
                    std::make_shared<const chronon3d::AnimatedTextDocument>(std::move(animated));
                definition = to_definition;
            }
            prepared_texts[index] = chronon3d::prepare_text(definition);
            if ((layer.style && layer.style->background) || layer.safe_insets) {
                auto measured = chronon3d::compile_text_layout(
                    *prepared_texts[index], text_compile_services);
                if (!measured)
                    throw std::runtime_error(
                        "text card layout failed for layer '" + layer.id + "': " +
                        measured.error().message);
                prepared_text_bounds[index] = measured.value()->bounds;
            }
        }
        std::vector<std::vector<EffectParams>> prepared_effects(plan.layers.size());
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            for (const auto& effect : plan.layers[index].effects) {
                if (effect.kind == LayerPlan::EffectKindPlan::Lut3D) {
                    const auto resolved = resolver.resolve_logical(std::filesystem::path(effect.asset));
                    if (!resolved) throw std::runtime_error("3D LUT asset was not prepared: " + effect.asset);
                    std::ifstream file(*resolved, std::ios::binary);
                    if (!file) throw std::runtime_error("failed to open 3D LUT asset: " + effect.asset);
                    std::string contents((std::istreambuf_iterator<char>(file)), {});
                    if (contents.size() > 8U * 1024U * 1024U)
                        throw std::runtime_error("3D LUT asset exceeds the 8 MiB parser limit: " + effect.asset);
                    prepared_effects[index].emplace_back(effects::parse_cube_lut(contents, effect.amount));
                } else if (effect.kind == LayerPlan::EffectKindPlan::Echo) {
                    prepared_effects[index].emplace_back(EchoParams{
                        .echoes = effect.echoes, .decay = effect.decay,
                        .time_offset_frames = effect.time_offset_frames});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Keying) {
                    KeyingParams keying;
                    keying.mode = effect.keying_mode == LayerPlan::KeyingModePlan::Luma
                        ? KeyingMode::Luma : KeyingMode::Chroma;
                    keying.key_color = Color{effect.key_color[0], effect.key_color[1], effect.key_color[2], 1.0f};
                    keying.threshold = effect.threshold;
                    keying.softness = effect.softness;
                    keying.spill = effect.spill;
                    keying.clip_black = effect.clip_black;
                    keying.clip_white = effect.clip_white;
                    keying.choke = effect.choke;
                    keying.garbage_x = effect.garbage_rect[0];
                    keying.garbage_y = effect.garbage_rect[1];
                    keying.garbage_w = effect.garbage_rect[2];
                    keying.garbage_h = effect.garbage_rect[3];
                    keying.garbage_feather = effect.garbage_feather;
                    prepared_effects[index].emplace_back(keying);
                } else if (effect.kind == LayerPlan::EffectKindPlan::Threshold) {
                    prepared_effects[index].emplace_back(ThresholdParams{
                        .threshold = effect.threshold,
                        .softness = effect.threshold_softness});
                } else if (effect.kind == LayerPlan::EffectKindPlan::ChromaticAberration) {
                    prepared_effects[index].emplace_back(ChromaticAberrationParams{
                        .red_offset = effect.chromatic_red_offset,
                        .blue_offset = effect.chromatic_blue_offset});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Halftone) {
                    prepared_effects[index].emplace_back(HalftoneParams{
                        .size = effect.halftone_size,
                        .angle_degrees = effect.halftone_angle,
                        .amount = effect.halftone_amount});
                } else if (effect.kind == LayerPlan::EffectKindPlan::DirectionalBlur) {
                    prepared_effects[index].emplace_back(DirectionalBlurParams{
                        .angle = effect.directional_angle,
                        .length = effect.directional_length,
                        .samples = effect.directional_samples});
                } else if (effect.kind == LayerPlan::EffectKindPlan::RadialBlur) {
                    prepared_effects[index].emplace_back(RadialBlurParams{
                        .center = Vec2{effect.radial_center[0], effect.radial_center[1]},
                        .amount = effect.radial_amount,
                        .render_samples = effect.radial_render_samples,
                        .preview_samples = effect.radial_preview_samples});
                } else if (effect.kind == LayerPlan::EffectKindPlan::GaussianBlur) {
                    prepared_effects[index].emplace_back(BlurParams{.radius = effect.radius});
                } else if (effect.kind == LayerPlan::EffectKindPlan::DropShadow) {
                    prepared_effects[index].emplace_back(DropShadowParams{
                        .offset = Vec2{effect.offset[0], effect.offset[1]},
                        .color = Color{effect.effect_color[0], effect.effect_color[1],
                                       effect.effect_color[2], effect.effect_color[3]},
                        .radius = effect.radius});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Bloom) {
                    prepared_effects[index].emplace_back(BloomParams{
                        .threshold = effect.bloom_threshold,
                        .radius = effect.radius,
                        .intensity = effect.bloom_intensity});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Glow) {
                    prepared_effects[index].emplace_back(GlowParams{
                        .radius = effect.radius,
                        .intensity = effect.glow_intensity,
                        .color = Color{effect.effect_color[0], effect.effect_color[1],
                                       effect.effect_color[2], effect.effect_color[3]}});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Vignette) {
                    prepared_effects[index].emplace_back(VignetteParams{
                        .radius = effect.radius, .softness = effect.softness,
                        .amount = effect.amount,
                        .color = Color{effect.effect_color[0], effect.effect_color[1],
                                       effect.effect_color[2], effect.effect_color[3]}});
                } else if (effect.kind == LayerPlan::EffectKindPlan::LightRays) {
                    prepared_effects[index].emplace_back(LightRaysParams{
                        .origin = Vec2{effect.light_rays_origin[0], effect.light_rays_origin[1]},
                        .length = effect.light_rays_length,
                        .density = effect.light_rays_density,
                        .color = Color{effect.light_rays_color[0], effect.light_rays_color[1],
                                       effect.light_rays_color[2], effect.light_rays_color[3]},
                        .decay = effect.light_rays_decay});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Noise) {
                    prepared_effects[index].emplace_back(NoiseParams{
                        .amount = effect.amount,
                        .seed = effect.seed,
                        .animated = effect.animated,
                        .color_mode = effect.rgb_noise ? NoiseColorMode::RGB
                                                       : NoiseColorMode::Monochrome,
                        .size = effect.noise_size,
                        .shadow_response = effect.grain_shadow_response,
                        .midtone_response = effect.grain_midtone_response,
                        .highlight_response = effect.grain_highlight_response});
                } else if (effect.kind == LayerPlan::EffectKindPlan::FractalNoise) {
                    prepared_effects[index].emplace_back(FractalNoiseParams{
                        .seed = effect.seed,
                        .octaves = effect.octaves,
                        .frequency = effect.frequency,
                        .amplitude = effect.amplitude,
                        .evolution = effect.evolution,
                        .contrast = effect.contrast,
                        .brightness = effect.brightness,
                        .color_mode = effect.rgb_noise ? NoiseColorMode::RGB
                                                       : NoiseColorMode::Monochrome});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Sharpen) {
                    prepared_effects[index].emplace_back(SharpenParams{
                        .amount = effect.amount, .radius = effect.radius, .threshold = effect.threshold});
                } else if (effect.kind == LayerPlan::EffectKindPlan::TurbulentDisplace) {
                    prepared_effects[index].emplace_back(TurbulentDisplaceParams{
                        .amount=effect.amount,.size=effect.warp_size,.evolution=effect.warp_evolution,
                        .evolution_speed=effect.warp_speed,.complexity=effect.warp_complexity});
                } else if (effect.kind == LayerPlan::EffectKindPlan::WaveWarp) {
                    prepared_effects[index].emplace_back(WaveWarpParams{
                        .type=static_cast<WaveWarpParams::WaveType>(effect.wave_type),
                        .direction=Vec2{effect.warp_direction[0],effect.warp_direction[1]},
                        .amplitude=effect.amount,.wavelength=effect.warp_wavelength,
                        .speed=effect.warp_speed,.phase=effect.warp_phase});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Ripple) {
                    prepared_effects[index].emplace_back(RippleParams{
                        .center=Vec2{effect.warp_center[0],effect.warp_center[1]},
                        .radius=effect.warp_radius,.wave_speed=effect.warp_speed,
                        .width=effect.warp_width,.wave_height=effect.warp_height,.damping=effect.warp_damping});
                } else if (effect.kind == LayerPlan::EffectKindPlan::DisplacementMap) {
                    prepared_effects[index].emplace_back(DisplacementMapParams{
                        .map_layer=effect.map_layer,.intensity_x=effect.map_intensity_x,
                        .intensity_y=effect.map_intensity_y,
                        .channel=static_cast<DisplacementMapParams::Channel>(effect.map_channel)});
                } else if (effect.kind == LayerPlan::EffectKindPlan::CornerPin) {
                    prepared_effects[index].emplace_back(CornerPinParams{
                        .top_left = Vec2{effect.quad[0], effect.quad[1]},
                        .top_right = Vec2{effect.quad[2], effect.quad[3]},
                        .bottom_right = Vec2{effect.quad[4], effect.quad[5]},
                        .bottom_left = Vec2{effect.quad[6], effect.quad[7]}});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Twirl) {
                    prepared_effects[index].emplace_back(TwirlParams{
                        .center = Vec2{effect.warp_center[0], effect.warp_center[1]},
                        .angle_degrees = effect.warp_angle,
                        .radius = effect.warp_radius,
                        .falloff = effect.warp_falloff});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Bulge) {
                    prepared_effects[index].emplace_back(BulgeParams{
                        .center = Vec2{effect.warp_center[0], effect.warp_center[1]},
                        .amount = effect.warp_amount,
                        .radius = effect.warp_radius});
                } else if (effect.kind == LayerPlan::EffectKindPlan::MeshWarp) {
                    MeshWarpParams mesh;
                    mesh.cols = static_cast<int>(effect.mesh_cols);
                    mesh.rows = static_cast<int>(effect.mesh_rows);
                    mesh.smoothness = effect.mesh_smoothness;
                    mesh.offsets.reserve(effect.mesh_offsets.size() / 2);
                    for (std::size_t i = 0; i + 1 < effect.mesh_offsets.size(); i += 2)
                        mesh.offsets.push_back(Vec2{effect.mesh_offsets[i], effect.mesh_offsets[i + 1]});
                    prepared_effects[index].emplace_back(std::move(mesh));

                } else if (effect.kind == LayerPlan::EffectKindPlan::Vignette) {
                    prepared_effects[index].emplace_back(VignetteEffectParams{
                        .amount = effect.amount, .radius = effect.warp_radius, .softness = effect.warp_falloff,
                        .color = Color{effect.effect_color[0], effect.effect_color[1], effect.effect_color[2], effect.effect_color[3]}});
                } else if (effect.kind == LayerPlan::EffectKindPlan::Catalog) {
                    auto params = effects::make_default_animatable_effect_params(effect.canonical_effect_id);
                    if (!params) throw std::runtime_error("catalog effect cannot be materialized by the compiler");
                    for (const auto& value : effect.canonical_params) {
                        if (!effects::set_animatable_effect_param(*params, effect.canonical_effect_id,
                                                                  value.param, value.value))
                            throw std::runtime_error("catalog effect parameter failed to apply during compilation");
                    }
                    prepared_effects[index].push_back(std::move(*params));
                } else {
                    prepared_effects[index].emplace_back(MosaicParams{
                        .block_size = effect.block_size});
                }
            }
        }
        materialize_layout_ms = chronon3d::profiling::duration_ms(
            materialize_t0, chronon3d::profiling::now());

        CompositionDefinition definition;
        definition.composition = spec;
        const bool has_3d_layer = std::any_of(
            plan.layers.begin(), plan.layers.end(),
            [](const LayerPlan& layer) { return layer.enable_3d; });
        const auto plan_camera = detail::make_camera_descriptor(plan);
        std::vector<detail::FollowPathEvaluators> follow_path_evaluators(plan.layers.size());
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            const auto& layer = plan.layers[index];
            if (!layer.follow_path) continue;
            Vec3 base_position{layer.position[0], layer.position[1], layer.position[2]};
            if ((layer.type == LayerType::Text || layer.type == LayerType::Shape ||
                 layer.type == LayerType::Particles || layer.type == LayerType::Light) &&
                layer.position_dimensions >= 2) {
                base_position.x -= static_cast<float>(plan.canvas.width) * 0.5f;
                base_position.y -= static_cast<float>(plan.canvas.height) * 0.5f;
            }
            const Vec3 base_rotation{layer.rotation[0], layer.rotation[1], layer.rotation[2]};
            follow_path_evaluators[index] = detail::make_follow_path_evaluators(
                *layer.follow_path, base_position, base_rotation,
                plan.canvas.width, plan.canvas.height, plan.canvas.fps);
        }
        definition.scene = [plan, resolved_video_paths, resolved_particle_sprite_paths, prepared_texts, prepared_text_counters, prepared_text_bounds, prepared_effects, font_engine,
                            has_3d_layer, plan_camera, follow_path_evaluators](
                               const FrameContext& ctx) {
            SceneBuilder scene(ctx);
            scene.font_engine(font_engine.get());
            if (plan.lighting_rig) {
                const auto& source = *plan.lighting_rig;
                chronon3d::rendering::LightingRig rig;
                rig.key_direction = chronon3d::rendering::safe_normalize(Vec3{
                    source.key_direction[0], source.key_direction[1], source.key_direction[2]});
                rig.key_color = Color{source.key_color[0], source.key_color[1], source.key_color[2], source.key_color[3]};
                rig.key_intensity = source.key_intensity;
                rig.ambient_color = Color{source.ambient_color[0], source.ambient_color[1], source.ambient_color[2], source.ambient_color[3]};
                rig.ambient_intensity = source.ambient_intensity;
                rig.rim.enabled = source.rim_enabled;
                rig.rim.color = Color{source.rim_color[0], source.rim_color[1], source.rim_color[2], source.rim_color[3]};
                rig.rim.intensity = source.rim_intensity;
                rig.rim.power = source.rim_power;
                scene.apply_lighting_rig(rig);
            }

            // Render-plan text positions are converted to centre-relative
            // layer coordinates by apply_layer_primitives(). Keep the
            // approved GlowBasics camera at that canonical world origin; a
            // second canvas-centre translation here would move the text out
            // of frame.
            if (has_3d_layer && !plan.camera) {
                Camera2_5D camera;
                camera.enabled = true;
                camera.optics_mode = CameraOpticsMode::Zoom;
                camera.position = Vec3{0.0f, 0.0f, -1000.0f};
                camera.zoom = 1000.0f;
                camera.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};
                camera.point_of_interest_enabled = true;
                // The implicit camera has a fixed pose. Layer motion is
                // tracked on the layers themselves and must not keep the
                // camera in the per-frame animated-camera path.
                camera.is_animated = false;
                scene.camera().set(camera);
            }
            // The canonical CLI executor builds this scene callback directly
            // and does not pass through Composition::evaluate(), which is the
            // path that normally applies a composition's default camera. Apply
            // render-plan cameras here so camera tracks affect the rendered
            // frame in both execution paths.
            if (plan_camera) {
                chronon3d::camera_v1::PoseTracksSource pose;
                pose.position = chronon3d::AnimatedValue<chronon3d::Vec3>{
                    plan_camera->base.position};
                pose.rotation = chronon3d::AnimatedValue<chronon3d::Vec3>{
                    plan_camera->base.rotation};
                pose.fov_deg = chronon3d::AnimatedValue<float>{
                    plan.camera->fov_deg};
                pose.zoom = chronon3d::AnimatedValue<float>{
                    1000.0f * plan.camera->zoom};
                if (const auto* authored = std::get_if<
                        chronon3d::camera_v1::PoseTracksSource>(
                            &plan_camera->source)) {
                    pose = *authored;
                }
                const auto camera_context = chronon3d::camera_v1::CameraEvalContext::at(
                    ctx.frame(), ctx.frame_rate(), ctx.width, ctx.height);
                auto evaluated_camera = chronon3d::camera_v1::eval_pose_tracks(
                    plan_camera->base, pose, camera_context);
                if (plan.camera_shake) {
                    const auto& shake = *plan.camera_shake;
                    const double frame = static_cast<double>(ctx.frame().integral());
                    const double elapsed_frames = frame - static_cast<double>(shake.start_frame.integral());
                    const bool in_range = elapsed_frames >= 0.0 &&
                        (shake.duration_frames.integral() == 0 ||
                         elapsed_frames <= static_cast<double>(shake.duration_frames.integral()));
                    if (in_range) {
                        const double elapsed_seconds = elapsed_frames /
                            (static_cast<double>(ctx.frame_rate().numerator) / ctx.frame_rate().denominator);
                        const double envelope = std::exp(-static_cast<double>(shake.decay) * elapsed_seconds);
                        const double phase = elapsed_seconds * static_cast<double>(shake.frequency_hz);
                        evaluated_camera.position.x += static_cast<float>(shake.amplitude_px *
                            shake_value_noise(phase, shake.seed, 0U) * envelope);
                        evaluated_camera.position.y += static_cast<float>(shake.amplitude_px *
                            shake_value_noise(phase, shake.seed, 1U) * envelope);
                        evaluated_camera.rotation.z += static_cast<float>(shake.rotation_amplitude_deg *
                            shake_value_noise(phase, shake.seed, 2U) * envelope);
                        evaluated_camera.is_animated = true;
                    }
                }
                scene.camera().set(std::move(evaluated_camera));
            }

            for (std::size_t index = 0; index < plan.layers.size(); ++index) {
                const auto& layer = plan.layers[index];
                const auto author_layer = [&](LayerBuilder& builder) {
                    builder.font_engine(font_engine.get());
                    if (layer.flow_layout) {
                        const auto& p=*layer.flow_layout; LayoutFlow flow;
                        flow.group_id=p.group_id; flow.main_axis=p.axis=="column"?Axis::Column:Axis::Row;
                        flow.gap=p.gap; flow.wrap=p.wrap; flow.cell_size={p.cell_size[0],p.cell_size[1]};
                        flow.align=p.align=="center"?AlignItems::Center:p.align=="end"?AlignItems::End:p.align=="stretch"?AlignItems::Stretch:AlignItems::Start;
                        flow.justify=p.justify=="center"?JustifyContent::Center:p.justify=="end"?JustifyContent::End:p.justify=="space_between"?JustifyContent::SpaceBetween:p.justify=="space_around"?JustifyContent::SpaceAround:p.justify=="space_evenly"?JustifyContent::SpaceEvenly:JustifyContent::Start;
                        builder.flow_layout(std::move(flow));
                    }
                    if (layer.grid_layout) {
                        const auto& p=*layer.grid_layout; LayoutGrid grid;
                        grid.group_id=p.group_id; grid.columns=p.columns; grid.rows=p.rows;
                        grid.fit=p.fit=="fixed_rows"?GridFit::FixedRows:p.fit=="fixed_columns"?GridFit::FixedColumns:GridFit::Auto;
                        grid.gap={p.gap[0],p.gap[1]}; grid.cell_size={p.cell_size[0],p.cell_size[1]};
                        builder.grid_layout(std::move(grid));
                    }
                    // Image textures carry their own color.  2.5D projection
                    // changes their pose, not their albedo, so keep them out
                    // of the scene lighting pass (which otherwise grays light
                    // product imagery and its transparent edge pixels).
                    if ((layer.type == LayerType::Image || layer.type == LayerType::Shape ||
                         layer.type == LayerType::Mesh) && layer.enable_3d)
                        builder.accepts_lights(false).accepts_shadows(false);
                    switch (layer.type) {
                        case LayerType::Image: {
                            ImageParams params;
                            params.asset_path = layer.asset;
                            params.size = layer.size_dimensions == 2
                                ? Vec2{layer.size[0], layer.size[1]}
                                : Vec2{static_cast<float>(plan.canvas.width),
                                       static_cast<float>(plan.canvas.height)};
                            params.fit = detail::fit_mode(
                                layer.fit.value_or(FitMode::Cover));
                            params.radius = layer.radius.value_or(0.0f);
                            builder.image("image", std::move(params));
                            break;
                        }
                        case LayerType::Video: {
                            // The canvas frame rate is the source sampling
                            // rate: the video must advance in lockstep with the
                            // timeline. A guessed 30 fps played 24 fps content
                            // at 0.8x while the audio ran at 100%, desyncing
                            // from second 0. validate_render_budget() already
                            // guarantees a positive rate, and every consumer
                            // (VideoNode, DirectYuvProgram) falls back to the
                            // frame-input rate if it is ever non-positive.
                            const float canvas_fps = static_cast<float>(plan.canvas.fps.numerator) /
                                (plan.canvas.fps.denominator > 0 ? static_cast<float>(plan.canvas.fps.denominator) : 1.0f);
                            video::VideoSource source{
                                .path = resolved_video_paths[index],
                                .source_fps = canvas_fps};
                            // Render-plan video geometry is expressed as a
                            // top-left canvas box.  The canonical scene
                            // transform, consumed by both the graph and
                            // DirectYUV, is centre-relative.  Preserve the
                            // effective displayed size by making the source
                            // size the pre-scale box; this also prevents the
                            // DirectYUV path from applying `size` and
                            // `scale` twice.
                            if (layer.size_dimensions == 2) {
                                const float sx = std::abs(layer.scale[0]) > 1e-6f
                                    ? std::abs(layer.scale[0]) : 1.0f;
                                const float sy = std::abs(layer.scale[1]) > 1e-6f
                                    ? std::abs(layer.scale[1]) : 1.0f;
                                source.size = {
                                    layer.size[0] / sx,
                                    layer.size[1] / sy};
                            }
                            if (layer.loop)
                                source.loop_mode = video::VideoLoopMode::Loop;
                            builder.video(std::move(source));
                            break;
                        }
                        case LayerType::Color: {
                            RectParams params;
                            params.size = layer.size_dimensions == 2
                                ? Vec2{layer.size[0], layer.size[1]}
                                : Vec2{static_cast<float>(plan.canvas.width),
                                       static_cast<float>(plan.canvas.height)};
                            params.color = {layer.color[0], layer.color[1],
                                            layer.color[2], layer.color[3]};
                            if (const auto animated = detail::fill_color_at(
                                    layer, ctx.effective_frame(),
                                    Color{params.color.r, params.color.g,
                                          params.color.b, params.color.a}, plan.canvas.fps))
                                params.color = *animated;
                            if (layer.position_dimensions < 2) {
                                builder.pin_to(Anchor::Center);
                                params.pos = {-params.size.x * 0.5f, -params.size.y * 0.5f, 0.0f};
                            }
                            builder.rect("color", std::move(params));
                            break;
                        }
                        case LayerType::Particles: {
                            if (!layer.particle_emitter)
                                throw std::runtime_error(
                                    "particles layer '" + layer.id + "' has no emitter parameters");
                            const double fps = static_cast<double>(plan.canvas.fps.numerator) /
                                static_cast<double>(plan.canvas.fps.denominator);
                            if (!std::isfinite(fps) || fps <= 0.0)
                                throw std::runtime_error("particles require a positive canvas frame rate");
                            const double seconds = static_cast<double>(ctx.effective_frame()) / fps;
                            const auto sampled = sample_particles(*layer.particle_emitter, seconds);
                            if (!sampled.ok())
                                throw std::runtime_error("particle sampling failed: " + sampled.error);
                            builder.accepts_lights(false).accepts_shadows(false);
                            for (const auto& particle : sampled.particles) {
                                if (!resolved_particle_sprite_paths[index].empty()) {
                                    ImageParams sprite;
                                    sprite.asset_path=resolved_particle_sprite_paths[index];
                                    sprite.size=Vec2{particle.size*particle.sprite_scale,particle.size*particle.sprite_scale};
                                    sprite.pos=Vec3{particle.position.x,particle.position.y,0.0f};
                                    sprite.opacity=particle.color.a;
                                    sprite.tint=Color{particle.color.r,particle.color.g,particle.color.b,1.0f};
                                    sprite.rotation_radians=particle.rotation;
                                    builder.image("particle_"+std::to_string(particle.index),std::move(sprite));
                                    continue;
                                }
                                CircleParams params;
                                params.radius = particle.size * 0.5f;
                                params.color = particle.color;
                                params.fill = graphics::FillStyle::solid(particle.color);
                                params.pos = Vec3{particle.position.x, particle.position.y, 0.0f};
                                builder.circle("particle_" + std::to_string(particle.index),
                                               std::move(params));
                            }
                            break;
                        }
                        case LayerType::Light: {
                            if (!layer.light)
                                throw std::runtime_error("light layer '" + layer.id + "' has no light payload");
                            const auto& light = *layer.light;
                            const Color color{
                                light.color[0] * light.intensity,
                                light.color[1] * light.intensity,
                                light.color[2] * light.intensity,
                                light.color[3]};
                            CircleParams params;
                            params.radius = light.radius;
                            params.color = color;
                            params.fill = graphics::FillStyle::radial(
                                {0.5f, 0.5f}, 0.5f,
                                {{0.0f, color}, {1.0f, Color{
                                    color.r, color.g, color.b, 0.0f}}});
                            builder.accepts_lights(false).accepts_shadows(false);
                            builder.circle("radial_light", std::move(params));
                            break;
                        }
                        case LayerType::Adjustment:
                            // Adjustment effects consume the composite built
                            // from preceding layers and intentionally draw no
                            // source geometry of their own.
                            break;
                        case LayerType::Mesh: {
                            if (!layer.inline_mesh)
                                throw std::runtime_error("mesh layer '" + layer.id + "' has no inline geometry");
                            InlineMeshParams params;
                            params.name = layer.id;
                            params.vertices.reserve(layer.inline_mesh->vertices.size());
                            for (const auto& vertex : layer.inline_mesh->vertices)
                                params.vertices.push_back(Vertex{
                                    Vec3{vertex.position[0], vertex.position[1], vertex.position[2]},
                                    Vec3{vertex.normal[0], vertex.normal[1], vertex.normal[2]},
                                    Vec2{vertex.uv[0], vertex.uv[1]}});
                            params.indices = layer.inline_mesh->indices;
                            builder.accepts_lights(false).accepts_shadows(false);
                            builder.inline_mesh(std::move(params));
                            auto& node = builder.last_node_handle().node();
                            node.color = Color{layer.color[0], layer.color[1], layer.color[2], layer.color[3]};
                            if (layer.inline_mesh->material) {
                                const auto& source = *layer.inline_mesh->material;
                                RenderNode::InlineMeshMaterial material;
                                material.diffuse = source.diffuse;
                                material.specular = source.specular;
                                material.shininess = source.shininess;
                                material.rim_intensity = source.rim_intensity;
                                if (source.base_color)
                                    material.base_color = Color{(*source.base_color)[0], (*source.base_color)[1],
                                                                (*source.base_color)[2], (*source.base_color)[3]};
                                node.inline_mesh_material = material;
                                builder.accepts_lights(true);
                            }
                            break;
                        }
                        case LayerType::Shape: {
                            if (!layer.shape)
                                throw std::runtime_error("shape layer '" + layer.id + "' has no shape payload");
                            const auto& shape = *layer.shape;
                            const Vec2 size = layer.size_dimensions == 2
                                ? Vec2{layer.size[0], layer.size[1]}
                                : Vec2{static_cast<float>(plan.canvas.width),
                                       static_cast<float>(plan.canvas.height)};
                            const Color authored_color{layer.color[0], layer.color[1],
                                                       layer.color[2], layer.color[3]};
                            const Color base_color = detail::fill_color_at(
                                layer, ctx.effective_frame(), authored_color,
                                plan.canvas.fps).value_or(authored_color);
                            const auto animated_gradient = detail::fill_gradient_at(
                                layer, ctx.effective_frame());
                            const auto& active_gradient = animated_gradient
                                ? animated_gradient : shape.gradient;
                            std::optional<graphics::FillStyle> fill;
                            if (active_gradient) {
                                fill = graphics::FillStyle::solid(base_color);
                                fill->gradient = *active_gradient;
                            } else if (shape.fill_color) {
                                fill = graphics::FillStyle::solid(base_color);
                            }
                            graphics::StrokeStyle stroke;
                            if (shape.stroke) {
                                stroke.enabled = true;
                                stroke.width = shape.stroke->width.value_or(1.0f);
                                if (shape.stroke->solid_color) {
                                    const auto& rgba = *shape.stroke->solid_color;
                                    stroke.color = Color{rgba[0], rgba[1], rgba[2], rgba[3]};
                                } else if (!shape.stroke->color.empty() && !shape.stroke->gradient) {
                                    const auto parsed = parse_hex_color(shape.stroke->color);
                                    if (!parsed)
                                        throw std::runtime_error("shape layer '" + layer.id + "' has invalid stroke color");
                                    stroke.color = *parsed;
                                }
                                stroke.gradient = shape.stroke->gradient;
                                stroke.trim_start = shape.stroke->trim_start.value_or(0.0f);
                                stroke.trim_end = shape.stroke->trim_end.value_or(1.0f);
                                stroke.dash_array = shape.stroke->dash_array;
                                stroke.dash_offset = shape.stroke->dash_offset;
                                if (const auto animated_width = detail::stroke_width_at(
                                        layer, ctx.effective_frame(), stroke.width,
                                        plan.canvas.fps))
                                    stroke.width = *animated_width;
                                if (!shape.stroke->gradient) {
                                    if (const auto animated_color = detail::stroke_color_at(
                                            layer, ctx.effective_frame(), stroke.color,
                                            plan.canvas.fps))
                                        stroke.color = *animated_color;
                                }
                                if (const auto animated_offset=detail::stroke_dash_offset_at(
                                        layer, ctx.effective_frame(), plan.canvas.fps))
                                    stroke.dash_offset=*animated_offset;
                            }
                            if (layer.position_dimensions < 2 &&
                                shape.type != ShapeContentTypePlan::Line &&
                                shape.type != ShapeContentTypePlan::Path &&
                                shape.type != ShapeContentTypePlan::Star &&
                                shape.type != ShapeContentTypePlan::Polygon &&
                                shape.type != ShapeContentTypePlan::Arrow &&
                                shape.type != ShapeContentTypePlan::Arc &&
                                shape.type != ShapeContentTypePlan::Grid &&
                                shape.type != ShapeContentTypePlan::DotGrid &&
                                shape.type != ShapeContentTypePlan::DeviceFrame) {
                                builder.pin_to(Anchor::Center);
                            }
                            switch (shape.type) {
                                case ShapeContentTypePlan::Rect: {
                                    RectParams params;
                                    params.size = size;
                                    params.color = base_color;
                                    params.fill = fill;
                                    params.stroke = stroke;
                                    builder.rect("shape", std::move(params));
                                    break;
                                }
                                case ShapeContentTypePlan::RoundedRect: {
                                    RoundedRectParams params;
                                    params.size = size;
                                    params.radius = shape.radius;
                                    params.corner_radius = shape.corner_radius;
                                    params.color = base_color;
                                    params.fill = fill;
                                    params.stroke = stroke;
                                    builder.rounded_rect("shape", std::move(params));
                                    break;
                                }
                                case ShapeContentTypePlan::Ellipse: {
                                    CircleParams params;
                                    params.radius = std::min(size.x, size.y) * 0.5f;
                                    params.color = base_color;
                                    params.fill = fill;
                                    params.stroke = stroke;
                                    builder.circle("shape", std::move(params));
                                    break;
                                }
                                case ShapeContentTypePlan::Line:
                                case ShapeContentTypePlan::Path:
                                case ShapeContentTypePlan::Star:
                                case ShapeContentTypePlan::Polygon:
                                case ShapeContentTypePlan::Arrow:
                                case ShapeContentTypePlan::Arc:
                                case ShapeContentTypePlan::Grid:
                                case ShapeContentTypePlan::DotGrid: {
                                    if (!shape.imported_subpaths.empty()) {
                                        for (std::size_t path_index = 0;
                                             path_index < shape.imported_subpaths.size(); ++path_index) {
                                            const auto& source = shape.imported_subpaths[path_index];
                                            PathParams imported_path;
                                            imported_path.color = base_color;
                                            for (const auto& command : source.path) {
                                                const Vec2 point{command.point[0], command.point[1]};
                                                const Vec2 c1{command.control1[0], command.control1[1]};
                                                const Vec2 c2{command.control2[0], command.control2[1]};
                                                switch (command.type) {
                                                    case ShapePathCommandTypePlan::MoveTo: imported_path.commands.push_back(PathCommand::move_to(point)); break;
                                                    case ShapePathCommandTypePlan::LineTo: imported_path.commands.push_back(PathCommand::line_to(point)); break;
                                                    case ShapePathCommandTypePlan::QuadraticTo: imported_path.commands.push_back(PathCommand::quadratic_to(c1, point)); break;
                                                    case ShapePathCommandTypePlan::CubicTo: imported_path.commands.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                                                    case ShapePathCommandTypePlan::Close:
                                                        imported_path.commands.push_back(PathCommand::close());
                                                        imported_path.closed = true;
                                                        break;
                                                }
                                            }
                                            if (source.fill_color) {
                                                const auto& rgba = *source.fill_color;
                                                imported_path.fill = Fill::solid_color(
                                                    Color{rgba[0], rgba[1], rgba[2], rgba[3]});
                                            } else {
                                                imported_path.fill.enabled = false;
                                            }
                                            if (source.stroke && source.stroke->solid_color) {
                                                const auto& rgba = *source.stroke->solid_color;
                                                imported_path.stroke = graphics::StrokeStyle::solid(
                                                    Color{rgba[0], rgba[1], rgba[2], rgba[3]},
                                                    source.stroke->width.value_or(1.0f)).to_path_stroke();
                                            } else {
                                                imported_path.stroke.enabled = false;
                                            }
                                            builder.path("svg_path_" + std::to_string(path_index),
                                                         std::move(imported_path));
                                        }
                                        break;
                                    }
                                    PathParams params;
                                    for (const auto& command : shape.path) {
                                        const Vec2 point{command.point[0], command.point[1]};
                                        const Vec2 control1{command.control1[0], command.control1[1]};
                                        const Vec2 control2{command.control2[0], command.control2[1]};
                                        switch (command.type) {
                                            case ShapePathCommandTypePlan::MoveTo:
                                                params.commands.push_back(PathCommand::move_to(point)); break;
                                            case ShapePathCommandTypePlan::LineTo:
                                                params.commands.push_back(PathCommand::line_to(point)); break;
                                            case ShapePathCommandTypePlan::QuadraticTo:
                                                params.commands.push_back(PathCommand::quadratic_to(control1, point)); break;
                                            case ShapePathCommandTypePlan::CubicTo:
                                                params.commands.push_back(PathCommand::cubic_to(control1, control2, point)); break;
                                            case ShapePathCommandTypePlan::Close:
                                                params.commands.push_back(PathCommand::close());
                                                params.closed = true;
                                                break;
                                        }
                                    }
                                    if (active_gradient) {
                                        params.fill.enabled = true;
                                        params.fill.type = fill_type_for_gradient(active_gradient->type);
                                        params.fill.gradient = *active_gradient;
                                    } else if (shape.fill_color) {
                                        // Path fill RGB/alpha lives in its Fill payload; node.color
                                        // supplies only the layer-opacity multiplier to draw_path().
                                        params.fill = Fill::solid_color(base_color);
                                    }
                                    params.stroke = stroke.to_path_stroke();
                                    params.pos = {0.0f, 0.0f, 0.0f};
                                    if (shape.operators.empty()) {
                                builder.path("shape", std::move(params));
                            } else {
                                PathShape source_path;
                                source_path.commands = params.commands;
                                source_path.stroke = params.stroke;
                                source_path.fill = params.fill;
                                source_path.closed = params.closed;
                                std::vector<PathOperatorSpec> specs;
                                for (const auto& op : shape.operators) {
                                    if (op.kind == PathOperatorKindPlan::RoundCorners) {
                                        specs.push_back({PathOperatorKind::RoundCorners,
                                            RoundCornersParams{op.radius}, 0});
                                    } else if (op.kind == PathOperatorKindPlan::ZigZag) {
                                        specs.push_back({PathOperatorKind::ZigZag,
                                            ZigZagParams{op.size, op.points_per_unit, op.smooth}, 0});
                                    } else if (op.kind == PathOperatorKindPlan::OffsetPath) {
                                        specs.push_back({PathOperatorKind::OffsetPath,
                                            OffsetPathParams{op.amount, op.miter_limit}, 0});
                                    } else if (op.kind == PathOperatorKindPlan::PuckerBloat) {
                                        specs.push_back({PathOperatorKind::PuckerBloat,
                                            PuckerBloatParams{op.amount, {op.anchor[0], op.anchor[1]}}, 0});
                                    } else if (op.kind == PathOperatorKindPlan::Twist) {
                                        specs.push_back({PathOperatorKind::Twist,
                                            TwistParams{op.degrees, {op.center[0], op.center[1]}}, 0});
                                    } else if (op.kind == PathOperatorKindPlan::WigglePath) {
                                        specs.push_back({PathOperatorKind::WigglePath,
                                            WigglePathParams{op.amount, op.frequency}, op.seed});
                                    } else if (op.kind == PathOperatorKindPlan::Morph) {
                                        MorphPathParams morph;
                                        morph.progress = op.morph_progress;
                                        morph.target.reserve(op.morph_target.size());
                                        for (const auto& command : op.morph_target) {
                                            const Vec2 point{command.point[0], command.point[1]};
                                            const Vec2 c1{command.control1[0], command.control1[1]};
                                            const Vec2 c2{command.control2[0], command.control2[1]};
                                            switch (command.type) {
                                                case ShapePathCommandTypePlan::MoveTo: morph.target.push_back(PathCommand::move_to(point)); break;
                                                case ShapePathCommandTypePlan::LineTo: morph.target.push_back(PathCommand::line_to(point)); break;
                                                case ShapePathCommandTypePlan::QuadraticTo: morph.target.push_back(PathCommand::quadratic_to(c1, point)); break;
                                                case ShapePathCommandTypePlan::CubicTo: morph.target.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                                                case ShapePathCommandTypePlan::Close: morph.target.push_back(PathCommand::close()); break;
                                            }
                                        }
                                        if (op.morph_animation) {
                                            chronon3d::AnimatedValue<float> progress{op.morph_progress};
                                            for (const auto& key : op.morph_animation->keyframes) {
                                                if (key.value.size() != 1)
                                                    throw std::runtime_error("morph animation keyframes require scalar progress");
                                                progress.add_keyframe(
                                                    key.frame, key.value.front(),
                                                    detail::animation_keyframe_easing(*op.morph_animation, key));
                                            }
                                            morph.progress = std::clamp(
                                                progress.evaluate(ctx.effective_frame()), 0.0f, 1.0f);
                                        }
                                        specs.push_back({PathOperatorKind::Morph, std::move(morph), 0});
                                    } else if (op.kind == PathOperatorKindPlan::Merge) {
                                        MergeParams merge;
                                        for (const auto& planned_path : op.paths) {
                                            std::vector<PathCommand> subpath;
                                            for (const auto& command : planned_path) {
                                                const Vec2 point{command.point[0], command.point[1]};
                                                const Vec2 c1{command.control1[0], command.control1[1]};
                                                const Vec2 c2{command.control2[0], command.control2[1]};
                                                switch (command.type) {
                                                    case ShapePathCommandTypePlan::MoveTo: subpath.push_back(PathCommand::move_to(point)); break;
                                                    case ShapePathCommandTypePlan::LineTo: subpath.push_back(PathCommand::line_to(point)); break;
                                                    case ShapePathCommandTypePlan::QuadraticTo: subpath.push_back(PathCommand::quadratic_to(c1, point)); break;
                                                    case ShapePathCommandTypePlan::CubicTo: subpath.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                                                    case ShapePathCommandTypePlan::Close: subpath.push_back(PathCommand::close()); break;
                                                }
                                            }
                                            merge.paths.push_back(std::move(subpath));
                                        }
                                        specs.push_back({PathOperatorKind::Merge, std::move(merge), 0});
                                    } else if (op.kind == PathOperatorKindPlan::Trim) {
                                        TrimPathParams trim{op.trim_start, op.trim_end, op.trim_offset};
                                        if (op.trim_animation) {
                                            chronon3d::AnimatedValue<float> start{op.trim_start};
                                            chronon3d::AnimatedValue<float> end{op.trim_end};
                                            chronon3d::AnimatedValue<float> offset{op.trim_offset};
                                            for (const auto& key : op.trim_animation->keyframes) {
                                                const auto easing = detail::animation_keyframe_easing(
                                                    *op.trim_animation, key);
                                                if (!key.value.empty()) start.add_keyframe(key.frame, key.value[0], easing);
                                                if (key.value.size() > 1) end.add_keyframe(key.frame, key.value[1], easing);
                                                if (key.value.size() > 2) offset.add_keyframe(key.frame, key.value[2], easing);
                                            }
                                            trim.start = std::clamp(start.evaluate(ctx.effective_frame()), 0.0f, 1.0f);
                                            trim.end = std::clamp(end.evaluate(ctx.effective_frame()), trim.start, 1.0f);
                                            trim.offset = offset.evaluate(ctx.effective_frame());
                                        }
                                        specs.push_back({PathOperatorKind::Trim,
                                            trim, 0});
                                    } else {
                                        const auto& repeater = op.repeater;
                                        RepeaterParams rp;
                                        rp.copies = repeater.copies;
                                        rp.position = {repeater.position[0], repeater.position[1]};
                                        rp.scale = {repeater.scale[0], repeater.scale[1]};
                                        rp.rotation_degrees = repeater.rotation_degrees;
                                        rp.anchor = {repeater.anchor[0], repeater.anchor[1]};
                                        rp.opacity_decay = repeater.opacity_decay;
                                        specs.push_back({PathOperatorKind::Repeater, rp, 0});
                                    }
                                }
                                auto expanded = apply_path_operators(source_path, specs);
                                for (std::size_t i = 0; i < expanded.size(); ++i) {
                                    PathParams copy = params;
                                    copy.commands = std::move(expanded[i].commands);
                                    copy.stroke = std::move(expanded[i].stroke);
                                    copy.fill = std::move(expanded[i].fill);
                                    copy.color.a *= expanded[i].opacity;
                                    builder.path("shape_repeater_" + std::to_string(i), std::move(copy));
                                }
                            }
                                    break;
                                }
                                case ShapeContentTypePlan::DeviceFrame: {
                                    const float width = size.x;
                                    const float height = size.y;
                                    if (layer.position_dimensions < 2)
                                        builder.pin_to(Anchor::Center);
                                    const bool phone = shape.device_frame_kind == DeviceFrameKindPlan::Phone;
                                    const float bezel = std::min(width, height) * (phone ? 0.035f : 0.02f);
                                    const Color shell_color{0.035f, 0.045f, 0.06f, 1.0f};
                                    const Color chrome_color{0.055f, 0.065f, 0.085f, 1.0f};
                                    RoundedRectParams shell;
                                    shell.size = size;
                                    shell.radius = std::min(width, height) * (phone ? 0.12f : 0.055f);
                                    shell.color = shell_color;
                                    shell.fill = graphics::FillStyle::solid(shell_color);
                                    shell.stroke = stroke;
                                    shell.pos = {-width * 0.5f, -height * 0.5f, 0.0f};
                                    builder.rounded_rect("device_shell", std::move(shell));

                                    RoundedRectParams screen;
                                    screen.size = {std::max(1.0f, width - bezel * 2.0f),
                                                   std::max(1.0f, height - bezel * 2.0f)};
                                    const Vec2 screen_size = screen.size;
                                    screen.radius = std::min(screen.size.x, screen.size.y) *
                                        (phone ? 0.085f : 0.025f);
                                    screen.color = base_color;
                                    screen.fill = fill;
                                    screen.pos = {-width * 0.5f + bezel,
                                                  -height * 0.5f + bezel, 0.0f};
                                    builder.rounded_rect("device_screen", std::move(screen));

                                    if (shape.device_frame_chrome && phone) {
                                        const float notch_width = std::min(screen_size.x * 0.70f, width * 0.30f);
                                        const float notch_height = std::min(screen_size.y * 0.20f,
                                            std::max(2.0f, height * 0.035f));
                                        RoundedRectParams notch;
                                        notch.size = {notch_width, notch_height};
                                        notch.radius = notch_height * 0.5f;
                                        notch.color = chrome_color;
                                        notch.fill = graphics::FillStyle::solid(chrome_color);
                                        notch.pos = {-notch_width * 0.5f,
                                            -height * 0.5f + bezel, 0.0f};
                                        builder.rounded_rect("device_notch", std::move(notch));
                                    } else if (shape.device_frame_chrome) {
                                        const float toolbar_height = std::min(screen_size.y,
                                            std::max(8.0f, height * 0.13f));
                                        RoundedRectParams toolbar;
                                        toolbar.size = {width - bezel * 2.0f, toolbar_height};
                                        toolbar.radius = std::min(4.0f, bezel);
                                        toolbar.color = chrome_color;
                                        toolbar.fill = graphics::FillStyle::solid(chrome_color);
                                        toolbar.pos = {-width * 0.5f + bezel,
                                            -height * 0.5f + bezel, 0.0f};
                                        builder.rounded_rect("browser_toolbar", std::move(toolbar));
                                        const std::array<Color, 3> lights{
                                            Color{0.96f, 0.28f, 0.25f, 1.0f},
                                            Color{0.98f, 0.72f, 0.20f, 1.0f},
                                            Color{0.23f, 0.78f, 0.36f, 1.0f}};
                                        for (std::size_t light = 0; light < lights.size(); ++light) {
                                            CircleParams dot;
                                            dot.radius = std::min(toolbar_height * 0.22f,
                                                std::max(1.0f, bezel * 0.20f));
                                            dot.color = lights[light];
                                            dot.fill = graphics::FillStyle::solid(lights[light]);
                                            dot.pos = {-width * 0.5f + bezel +
                                                dot.radius + static_cast<float>(light) * dot.radius * 3.2f,
                                                -height * 0.5f + bezel + toolbar_height * 0.5f, 0.0f};
                                            builder.circle("browser_traffic_light_" +
                                                std::to_string(light), std::move(dot));
                                        }
                                    }
                                    break;
                                }
                            }
                            if (layer.style && layer.style->backdrop) {
                                const auto& backdrop = *layer.style->backdrop;
                                builder.backdrop_blur(BackdropBlurParams{
                                    .blur = backdrop.blur,
                                    .brightness = backdrop.brightness,
                                    .saturation = backdrop.saturation,
                                    .rim_light = backdrop.rim_light,
                                    .refraction = backdrop.refraction,
                                    .surface_noise = backdrop.surface_noise});
                            }
                            break;
                        }
                        case LayerType::Text: {
                            builder.kind(LayerKind::Text);
                            // Text paint is authored in screen space; it is
                            // not a lit 3D material. Keeping this semantic
                            // contract at the layer boundary prevents the
                            // native GPU path from inserting CPU-only light/
                            // depth-grade passes between projected text and
                            // its compositor output.
                            builder.accepts_lights(false).accepts_shadows(false);
                            if (!prepared_texts[index])
                                throw std::runtime_error(
                                    "text layer was not prepared");
                            if (layer.style && layer.style->background) {
                                if (!prepared_text_bounds[index])
                                    throw std::runtime_error(
                                        "text card bounds were not measured for layer '" + layer.id + "'");
                                const auto& background = *layer.style->background;
                                const auto color = parse_hex_color(
                                    background.color,
                                    background.opacity.value_or(1.0f));
                                if (!color)
                                    throw std::runtime_error(
                                        "text layer '" + layer.id + "' has invalid background color");
                                const Vec2 padding = background.padding_dimensions == 2
                                    ? Vec2{background.padding[0], background.padding[1]}
                                    : Vec2{0.0f, 0.0f};
                                const Vec2 card_size{
                                    std::max(1.0f, prepared_text_bounds[index]->x + 2.0f * padding.x),
                                    std::max(1.0f, prepared_text_bounds[index]->y + 2.0f * padding.y)};
                                RoundedRectParams card;
                                card.size = card_size;
                                card.radius = background.radius.value_or(0.0f);
                                card.pos = {-card_size.x * 0.5f, -card_size.y * 0.5f, 0.0f};
                                card.color = *color;
                                card.fill = graphics::FillStyle::solid(*color);
                                builder.rounded_rect("text_card", std::move(card));
                            }
                            auto& text_builder = builder.text_run(
                                "text",
                                *prepared_texts[index])
                                .font_engine(font_engine.get());
                            if (prepared_text_counters[index])
                                text_builder.from_animated_document(prepared_text_counters[index]);
                            detail::apply_text_animators(text_builder, layer, plan.canvas.fps);
                            detail::apply_text_glow(builder, layer);
                            break;
                        }
                    }
                    detail::apply_layer_primitives(
                        builder, layer, plan.canvas.width, plan.canvas.height, plan.canvas.fps,
                        layer.follow_path ? &follow_path_evaluators[index] : nullptr);
                    if (layer.type == LayerType::Particles && layer.particle_emitter)
                        builder.blend(layer.particle_emitter->blend_mode);
                    if (layer.safe_insets) {
                                const auto& insets = *layer.safe_insets;
                                Vec2 content_size{};
                                if (layer.type == LayerType::Text &&
                                    prepared_text_bounds[index]) {
                                    content_size = *prepared_text_bounds[index];
                                    if (layer.style && layer.style->background) {
                                        const auto& padding_style = *layer.style->background;
                                        if (padding_style.padding_dimensions == 2) {
                                            content_size.x += 2.0f * padding_style.padding[0];
                                            content_size.y += 2.0f * padding_style.padding[1];
                                        }
                                    }
                                } else if (layer.type == LayerType::Shape &&
                                           layer.size_dimensions == 2) {
                                    content_size = {layer.size[0], layer.size[1]};
                                } else if ((layer.type == LayerType::Image ||
                                            layer.type == LayerType::Video) &&
                                           layer.size_dimensions == 2) {
                                    content_size = {layer.size[0], layer.size[1]};
                                } else if (layer.type == LayerType::Particles &&
                                           layer.particle_emitter) {
                                    content_size = layer.particle_emitter->emitter_size;
                                } else if (layer.type == LayerType::Light && layer.light) {
                                    content_size = Vec2{layer.light->radius * 2.0f};
                                }
                                content_size.x *= std::abs(layer.scale[0]);
                                content_size.y *= std::abs(layer.scale[1]);
                                const float angle = glm::radians(layer.rotation[2]);
                                const Vec2 rotated_size{
                                    std::abs(std::cos(angle)) * content_size.x +
                                        std::abs(std::sin(angle)) * content_size.y,
                                    std::abs(std::sin(angle)) * content_size.x +
                                        std::abs(std::cos(angle)) * content_size.y};
                                const float half_canvas_width =
                                    static_cast<float>(plan.canvas.width) * 0.5f;
                                const float half_canvas_height =
                                    static_cast<float>(plan.canvas.height) * 0.5f;
                                builder.keep_in_safe_area(chronon3d::SafeArea{
                                    .top = insets[0] - half_canvas_height,
                                    .bottom = half_canvas_height + insets[2] + rotated_size.y,
                                    .left = insets[3] - half_canvas_width,
                                    .right = half_canvas_width + insets[1] + rotated_size.x});
                    }
                    if (layer.type == LayerType::Shape && layer.style) {
                        std::size_t shadow_index = 0;
                        const auto add_shadow = [&](const ShadowStyle& shadow) {
                            const std::string prefix = "shadow_" + std::to_string(shadow_index++);
                            const auto color = parse_hex_color(shadow.color, 1.0f);
                            if (!color)
                                throw std::runtime_error("shape layer '" + layer.id + "' has invalid shadow color");
                            if (shadow.offset_dimensions != 2)
                                throw std::runtime_error("shape layer '" + layer.id + "' shadow requires a two-dimensional offset");
                            const float opacity = detail::shadow_scalar_at(
                                layer, prefix + "_opacity", ctx.effective_frame(),
                                shadow.opacity.value_or(1.0f), plan.canvas.fps)
                                .value_or(shadow.opacity.value_or(1.0f));
                            const float blur = detail::shadow_scalar_at(
                                layer, prefix + "_blur", ctx.effective_frame(),
                                shadow.blur.value_or(0.0f), plan.canvas.fps)
                                .value_or(shadow.blur.value_or(0.0f));
                            const float offset_x = detail::shadow_scalar_at(
                                layer, prefix + "_offset_x", ctx.effective_frame(),
                                shadow.offset[0], plan.canvas.fps).value_or(shadow.offset[0]);
                            const float offset_y = detail::shadow_scalar_at(
                                layer, prefix + "_offset_y", ctx.effective_frame(),
                                shadow.offset[1], plan.canvas.fps).value_or(shadow.offset[1]);
                            const auto animated_color = detail::shadow_color_at(
                                layer, prefix + "_color", ctx.effective_frame(),
                                *color, plan.canvas.fps);
                            Color final_color = animated_color.value_or(*color);
                            final_color.a *= opacity;
                            if (!std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f ||
                                !std::isfinite(blur) || blur < 0.0f ||
                                !std::isfinite(offset_x) || !std::isfinite(offset_y))
                                throw std::runtime_error("animated shadow parameters are outside supported bounds");
                            builder.effect(DropShadowParams{
                                .offset = Vec2{offset_x, offset_y},
                                .color = final_color,
                                .radius = blur});
                        };
                        if (layer.style->shadow) add_shadow(*layer.style->shadow);
                        for (const auto& shadow : layer.style->shadows) add_shadow(shadow);
                    }
                    for (std::size_t effect_index=0; effect_index<prepared_effects[index].size(); ++effect_index) {
                        const auto& effect=prepared_effects[index][effect_index];
                        const auto* effect_plan=effect_index<layer.effects.size()?&layer.effects[effect_index]:nullptr;
                        const auto time_control=effect_plan?effect_plan->posterize_fps:std::nullopt;
                        std::optional<TemporalCurve1D> time_curve;
                        if(effect_plan&&effect_plan->time_curve)
                            time_curve=detail::make_effect_time_curve(*effect_plan->time_curve);
                        const double composition_fps=static_cast<double>(plan.canvas.fps.numerator)/plan.canvas.fps.denominator;
                        const double duration_seconds=static_cast<double>(plan.canvas.duration.value)/composition_fps;
                        std::string_view canonical_effect_id;
                        const auto effect_type = effects::detect_effect_type(effect);
                        for (const auto& entry : effects::builtin_effect_catalog())
                            if (entry.type == effect_type) { canonical_effect_id = entry.id; break; }
                        std::optional<EffectParams> animated_effect;
                        if (!canonical_effect_id.empty()) {
                            for (const auto& param_track : layer.effect_param_tracks) {
                                if (param_track.effect_id != canonical_effect_id) continue;
                                if (!animated_effect) animated_effect.emplace(effect);
                                const auto base = effects::get_animatable_effect_param(
                                    *animated_effect, param_track.effect_id, param_track.param);
                                if (!base)
                                    throw std::runtime_error("effect parameter track is incompatible with the compiled effect parameter type");
                                const float value = detail::evaluate_scalar_track(
                                    param_track.track, ctx.effective_frame(), *base, plan.canvas.fps);
                                const auto* bounds = effects::find_animatable_effect_param(
                                    param_track.effect_id, param_track.param);
                                if (!bounds || !std::isfinite(value))
                                    throw std::runtime_error("effect parameter track produced a non-finite sample");
                                if (!effects::set_animatable_effect_param(
                                        *animated_effect, param_track.effect_id, param_track.param,
                                        std::clamp(value, bounds->minimum, bounds->maximum)))
                                    throw std::runtime_error("failed to apply compiled effect parameter track");
                            }
                        }
                        const auto& dispatch_effect = animated_effect ? *animated_effect : effect;
                        std::visit([&](const auto& params) {
                            using T = std::decay_t<decltype(params)>;
                            if constexpr (!std::is_same_v<T, std::monostate>)
                                builder.effect(params,time_control,time_curve,duration_seconds,composition_fps);
                        }, dispatch_effect);
                    }
                    if (const auto motion_blur = detail::motion_blur_params_at(
                            layer, ctx.effective_frame(), plan.canvas.fps,
                            layer.follow_path ? &follow_path_evaluators[index] : nullptr)) {
                        builder.effect(*motion_blur);
                    }
                    if (layer.path_mask || !layer.masks.empty()) {
                        const auto convert_mask_path = [](const auto& input) {
                            std::vector<PathCommand> output;
                            output.reserve(input.size());
                            for (const auto& command : input) {
                                const Vec2 point{command.point[0], command.point[1]};
                                const Vec2 c1{command.control1[0], command.control1[1]};
                                const Vec2 c2{command.control2[0], command.control2[1]};
                                switch (command.type) {
                                    case ShapePathCommandTypePlan::MoveTo: output.push_back(PathCommand::move_to(point)); break;
                                    case ShapePathCommandTypePlan::LineTo: output.push_back(PathCommand::line_to(point)); break;
                                    case ShapePathCommandTypePlan::QuadraticTo: output.push_back(PathCommand::quadratic_to(c1, point)); break;
                                    case ShapePathCommandTypePlan::CubicTo: output.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                                    case ShapePathCommandTypePlan::Close: output.push_back(PathCommand::close()); break;
                                }
                            }
                            return output;
                        };
                        const auto build_operation = [&](const LayerPlan::PathMaskPlan& source) {
                            MaskOperation operation;
                            operation.type = source.type == "path" ? MaskType::Path
                                : source.type == "rect" ? MaskType::Rect
                                : source.type == "rounded_rect" ? MaskType::RoundedRect
                                : MaskType::Ellipse;
                            operation.mode = source.mode == "add" ? MaskCombineMode::Add
                                : source.mode == "subtract" ? MaskCombineMode::Subtract
                                : source.mode == "intersect" ? MaskCombineMode::Intersect
                                : MaskCombineMode::Difference;
                            operation.pos = Vec3{source.position[0], source.position[1], 0.0f};
                            operation.size = Vec2{source.size[0], source.size[1]};
                            operation.radius = source.radius;
                            operation.path = convert_mask_path(source.path);
                            operation.feather = source.feather_track
                                ? std::clamp(detail::evaluate_scalar_track(*source.feather_track,
                                    ctx.effective_frame(), source.feather, plan.canvas.fps), 0.0f, 1000000.0f)
                                : source.feather;
                            operation.expansion = source.expansion_track
                                ? std::clamp(detail::evaluate_scalar_track(*source.expansion_track,
                                    ctx.effective_frame(), source.expansion, plan.canvas.fps), -1000000.0f, 1000000.0f)
                                : source.expansion;
                            operation.opacity = source.opacity_track
                                ? std::clamp(detail::evaluate_scalar_track(*source.opacity_track,
                                    ctx.effective_frame(), source.opacity, plan.canvas.fps), 0.0f, 1.0f)
                                : source.opacity;
                            operation.inverted = source.inverted;
                            Vec2 path_center{0.0f, 0.0f};
                            if (source.type == "path" && source.size[0] == 0.0f && source.size[1] == 0.0f) {
                                float min_x = std::numeric_limits<float>::max();
                                float min_y = std::numeric_limits<float>::max();
                                float max_x = std::numeric_limits<float>::lowest();
                                float max_y = std::numeric_limits<float>::lowest();
                                for (const auto& command : operation.path) {
                                    if (command.type == PathCommandType::Close) continue;
                                    const auto include = [&](Vec2 point) {
                                        min_x = std::min(min_x, point.x); min_y = std::min(min_y, point.y);
                                        max_x = std::max(max_x, point.x); max_y = std::max(max_y, point.y);
                                    };
                                    include(command.p0);
                                    if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo)
                                        include(command.p1);
                                    if (command.type == PathCommandType::CubicTo) include(command.p2);
                                }
                                path_center = Vec2{(min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f};
                                operation.size = Vec2{max_x - min_x, max_y - min_y};
                                operation.pos += Vec3{path_center, 0.0f};
                                for (auto& command : operation.path) {
                                    if (command.type == PathCommandType::Close) continue;
                                    command.p0 -= path_center;
                                    if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo)
                                        command.p1 -= path_center;
                                    if (command.type == PathCommandType::CubicTo) command.p2 -= path_center;
                                }
                            }
                            if (source.morph_animation) {
                                AnimatedValue<float> progress{0.0f};
                                for (const auto& key : source.morph_animation->keyframes)
                                    progress.add_keyframe(key.frame, key.value.front(),
                                        detail::animation_keyframe_easing(*source.morph_animation, key));
                                MorphPathParams morph;
                                morph.progress = std::clamp(progress.evaluate(ctx.effective_frame()), 0.0f, 1.0f);
                                morph.target = convert_mask_path(source.morph_target);
                                if (source.type == "path" && source.size[0] == 0.0f && source.size[1] == 0.0f)
                                    for (auto& command : morph.target) {
                                        if (command.type == PathCommandType::Close) continue;
                                        command.p0 -= path_center;
                                        if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo)
                                            command.p1 -= path_center;
                                        if (command.type == PathCommandType::CubicTo) command.p2 -= path_center;
                                    }
                                PathShape path;
                                path.commands = std::move(operation.path);
                                operation.path = apply_path_operators(path, {
                                    PathOperatorSpec{PathOperatorKind::Morph, std::move(morph), 0}}).front().commands;
                                if (source.type == "path" && source.size[0] == 0.0f &&
                                    source.size[1] == 0.0f) {
                                    float min_x = std::numeric_limits<float>::max();
                                    float min_y = std::numeric_limits<float>::max();
                                    float max_x = std::numeric_limits<float>::lowest();
                                    float max_y = std::numeric_limits<float>::lowest();
                                    for (const auto& command : operation.path) {
                                        if (command.type == PathCommandType::Close) continue;
                                        const auto include = [&](Vec2 point) {
                                            min_x = std::min(min_x, point.x);
                                            min_y = std::min(min_y, point.y);
                                            max_x = std::max(max_x, point.x);
                                            max_y = std::max(max_y, point.y);
                                        };
                                        include(command.p0);
                                        if (command.type == PathCommandType::QuadraticTo ||
                                            command.type == PathCommandType::CubicTo)
                                            include(command.p1);
                                        if (command.type == PathCommandType::CubicTo)
                                            include(command.p2);
                                    }
                                    operation.size = Vec2{max_x - min_x, max_y - min_y};
                                }
                            }
                            return operation;
                        };
                        std::vector<LayerPlan::PathMaskPlan> plan_masks;
                        if (layer.path_mask) plan_masks.push_back(*layer.path_mask);
                        plan_masks.insert(plan_masks.end(), layer.masks.begin(), layer.masks.end());
                        Mask composite;
                        const auto primary = build_operation(plan_masks.front());
                        composite.type = primary.type;
                        composite.pos = primary.pos;
                        composite.size = primary.size;
                        composite.radius = primary.radius;
                        composite.path = primary.path;
                        composite.feather = primary.feather;
                        composite.expansion = primary.expansion;
                        composite.opacity = primary.opacity;
                        composite.inverted = primary.inverted;
                        for (std::size_t index = 1; index < plan_masks.size(); ++index)
                            composite.operations.push_back(build_operation(plan_masks[index]));
                        builder.mask_composite(std::move(composite));
                    }
                    if (layer.type == LayerType::Video &&
                        layer.position_dimensions >= 2) {
                        const float sx = std::abs(layer.scale[0]) > 1e-6f
                            ? std::abs(layer.scale[0]) : 1.0f;
                        const float sy = std::abs(layer.scale[1]) > 1e-6f
                            ? std::abs(layer.scale[1]) : 1.0f;
                        const float displayed_w = layer.size_dimensions == 2
                            ? layer.size[0] : static_cast<float>(plan.canvas.width) * sx;
                        const float displayed_h = layer.size_dimensions == 2
                            ? layer.size[1] : static_cast<float>(plan.canvas.height) * sy;
                        builder.position({
                            layer.position[0] + displayed_w * 0.5f -
                                static_cast<float>(plan.canvas.width) * 0.5f,
                            layer.position[1] + displayed_h * 0.5f -
                                static_cast<float>(plan.canvas.height) * 0.5f,
                            layer.position[2]});
                    }
                };
                const bool full_canvas_backdrop = has_3d_layer &&
                    layer.type != LayerType::Text &&
                    layer.type != LayerType::Shape &&

                    layer.position_dimensions == 0 &&
                    (layer.size_dimensions != 2 ||
                     (layer.size[0] >= static_cast<float>(plan.canvas.width) &&
                      layer.size[1] >= static_cast<float>(plan.canvas.height)));
                // Full-canvas color, image and video plates are backdrops, not
                // camera-facing cards. Keep them in screen space around any
                // projected 3D content, independent of the concrete media type.
                if (layer.type == LayerType::Adjustment)
                    scene.adjustment_layer(layer.id, author_layer);
                else if (layer.screen_space || full_canvas_backdrop)
                    scene.screen_layer(layer.id, author_layer);
                else scene.layer(layer.id, author_layer);
            }
            return scene.build();
        };
        definition.camera = detail::make_camera_descriptor(plan);
        definition.scene_content_fingerprint = content_fingerprint;

        const auto composition_t0 = chronon3d::profiling::now();
        CompiledComposition compiled_value;
        std::shared_ptr<const Composition> composition;
        if (canonical_program != nullptr) {
            // The CLI already owns the canonical CompiledProgram. The old
            // video executor only needs a scene-evaluation compatibility
            // object, so do not invoke compile_composition again here.
            composition = std::make_shared<const Composition>(
                definition.composition, definition.scene,
                definition.scene_content_fingerprint);
            compiled_value.composition = composition;
            compiled_value.fingerprint = canonical_program->fingerprint();
        } else {
            // Standalone callers of the historical prepared-plan API retain
            // its original composition compiler behavior.
            Composition definition_composition(
                definition.composition,
                definition.scene,
                definition.scene_content_fingerprint);
            if (definition.camera.has_value()) {
                definition_composition.default_camera_descriptor(
                    *definition.camera);
            }
            auto compiled =
                chronon3d::compile_composition(definition_composition, {});
            if (!compiled)
                return PlanDecodeError{"composition", compiled.error().message};

            compiled_value = std::move(compiled).value();
            auto compiled_view =
                std::make_shared<const CompiledComposition>(compiled_value);
            composition = std::make_shared<Composition>(
                definition.composition,
                [compiled_view](const FrameContext& context) {
                    auto evaluated = chronon3d::evaluate(
                        *compiled_view,
                        CompositionEvaluateContext{.frame_context = context},
                        context.local_time());
                    if (!evaluated) {
                        throw std::runtime_error(
                            "compiled render-plan evaluation failed: " +
                            evaluated.error().message);
                    }
                    const auto camera = evaluated.value().camera;
                    Scene scene = std::move(evaluated.value().scene);
                    if (camera.has_value()) scene.set_camera_2_5d(*camera);
                    return scene;
                },
                content_fingerprint);
        }
        const double composition_stage_ms = chronon3d::profiling::duration_ms(
            composition_t0, chronon3d::profiling::now());
        if (canonical_program != nullptr) {
            compatibility_view_ms = composition_stage_ms;
        } else {
            composition_compile_ms = composition_stage_ms;
        }
        compiled_value.asset_manifest =
            std::make_shared<const chronon3d::assets::PreparedAssetManifest>(assets);
        compiled_value.render_budget = plan.budget;

        PreparedRenderPlan prepared;
        prepared.compiled_composition = std::move(compiled_value);
        prepared.composition =
            std::shared_ptr<const Composition>(std::move(composition));
        prepared.assets = std::move(assets);
        prepared.resources = std::move(resources);
        prepared.fingerprint = fingerprints;
        prepared.job_id = plan.job_id;
        prepared.canvas = plan.canvas;
        prepared.output = plan.output;
        prepared.render_budget = plan.budget;
        spdlog::info(
            "[plan-compile-profile] assets={:.2f}ms fingerprint={:.2f}ms "
            "media_resolution={:.2f}ms materialize={:.2f}ms "
            "composition_compile={:.2f}ms compatibility_view={:.2f}ms "
            "total={:.2f}ms",
            asset_prepare_ms, fingerprint_ms, media_resolution_ms,
            materialize_layout_ms, composition_compile_ms, compatibility_view_ms,
            chronon3d::profiling::duration_ms(
                compile_total_t0, chronon3d::profiling::now()));
        return prepared;
    } catch (const std::exception& error) {
        return PlanDecodeError{"", error.what()};
    }
}

Result<CompiledComposition, PlanDecodeError> make_video_compatibility_view(
    const RenderPlan& plan,
    chronon3d::assets::AssetResolver& resolver,
    const chronon3d::assets::PreparedAssetManifest& assets,
    const compiler::CompiledProgram& program,
    const RenderPlanFingerprintOptions& fingerprint_options) {
    // Reuse the canonical prepared-plan machinery with an explicit program
    // owner. The implementation's canonical_program branch performs asset
    // and text materialization but skips the legacy composition compiler.
    auto prepared = compile_render_plan(
        plan, resolver, fingerprint_options, &program);
    if (!prepared) return std::move(prepared).error();
    auto result = std::move(prepared).value().compiled_composition;
    // The canonical compiler's manifest is authoritative. `assets` is kept
    // as an explicit parameter to make the ownership contract visible at the
    // boundary and to support callers that already preflighted assets.
    if (!result.asset_manifest) {
        result.asset_manifest = std::make_shared<const chronon3d::assets::PreparedAssetManifest>(assets);
    }
    return result;
}

}  // namespace chronon3d::render_plan
