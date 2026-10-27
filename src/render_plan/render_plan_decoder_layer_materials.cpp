#include "render_plan_decoder_detail.hpp"
#include <chronon3d/render_plan/easing_contract.hpp>
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>
#include <chronon3d/effects/effect_ids.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>

namespace chronon3d::render_plan::detail {

std::optional<Color> decode_text_span_color(const std::string& encoded) {
    if (encoded.size() != 7 && encoded.size() != 9) return std::nullopt;
    const auto rgb = parse_hex_color(std::string_view(encoded).substr(0, 7));
    if (!rgb) return std::nullopt;
    if (encoded.size() == 7) return rgb;
    const auto nibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    const int high = nibble(encoded[7]);
    const int low = nibble(encoded[8]);
    if (high < 0 || low < 0) return std::nullopt;
    return Color{rgb->r, rgb->g, rgb->b,
        static_cast<float>(high * 16 + low) / 255.0f};
}

void decode_background_look(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("background_look")) {
        const auto& raw_look = value.at("background_look");
        if (!raw_look.is_object())
            throw std::runtime_error("background_look must be an object");
        const auto look_type = raw_look.value("type", std::string{});
        const auto add_effect = [&](LayerPlan::EffectPlan effect) {
            if (layer.effects.size() >= 64)
                throw std::runtime_error("background_look expansion exceeds the 64-effect layer limit");
            layer.effects.push_back(std::move(effect));
        };
        if (look_type == "film_grade") {
            LayerPlan::EffectPlan grain;
            grain.kind = LayerPlan::EffectKindPlan::Noise;
            grain.amount = raw_look.value("grain", 0.06f);
            grain.seed = raw_look.value("seed", std::uint32_t{0});
            grain.noise_size = raw_look.value("grain_size", 1.0f);
            grain.grain_shadow_response = raw_look.value("shadow_response", 0.7f);
            grain.grain_midtone_response = raw_look.value("midtone_response", 1.0f);
            grain.grain_highlight_response = raw_look.value("highlight_response", 0.65f);
            if (!std::isfinite(grain.amount) || grain.amount < 0.0f || grain.amount > 1.0f ||
                !std::isfinite(grain.noise_size) || grain.noise_size <= 0.0f || grain.noise_size > 128.0f ||
                !std::isfinite(grain.grain_shadow_response) || grain.grain_shadow_response < 0.0f || grain.grain_shadow_response > 4.0f ||
                !std::isfinite(grain.grain_midtone_response) || grain.grain_midtone_response < 0.0f || grain.grain_midtone_response > 4.0f ||
                !std::isfinite(grain.grain_highlight_response) || grain.grain_highlight_response < 0.0f || grain.grain_highlight_response > 4.0f)
                throw std::runtime_error("film_grade grain settings are outside supported ranges");
            add_effect(std::move(grain));
            LayerPlan::EffectPlan vignette;
            vignette.kind = LayerPlan::EffectKindPlan::Vignette;
            vignette.effect_color = {0.0f, 0.0f, 0.0f, 1.0f};
            vignette.amount = raw_look.value("vignette", 0.22f);
            vignette.radius = raw_look.value("vignette_radius", 0.72f);
            vignette.softness = raw_look.value("vignette_softness", 0.55f);
            if (!std::isfinite(vignette.amount) || vignette.amount < 0.0f || vignette.amount > 1.0f ||
                !std::isfinite(vignette.radius) || vignette.radius < 0.0f || vignette.radius > 1.0f ||
                !std::isfinite(vignette.softness) || vignette.softness < 0.0f || vignette.softness > 1.0f)
                throw std::runtime_error("film_grade vignette settings are outside [0, 1]");
            add_effect(std::move(vignette));
            if (raw_look.contains("lut")) {
                LayerPlan::EffectPlan lut;
                lut.kind = LayerPlan::EffectKindPlan::Lut3D;
                lut.asset = raw_look.at("lut").get<std::string>();
                lut.amount = raw_look.value("lut_amount", 1.0f);
                if (lut.asset.empty() || !std::isfinite(lut.amount) || lut.amount < 0.0f || lut.amount > 1.0f)
                    throw std::runtime_error("film_grade LUT requires a non-empty asset and amount in [0, 1]");
                if (layer.effects.size() >= 64)
                    throw std::runtime_error("background_look expansion exceeds the 64-effect layer limit");
                layer.effects.insert(layer.effects.end() - 2, std::move(lut));
            }
        } else if (look_type == "aurora") {
            LayerPlan::EffectPlan noise;
            noise.kind = LayerPlan::EffectKindPlan::FractalNoise;
            noise.seed = raw_look.value("seed", std::uint32_t{0});
            noise.octaves = raw_look.value("octaves", 5U);
            noise.frequency = raw_look.value("frequency", 0.009f);
            noise.amplitude = raw_look.value("amplitude", 0.55f);
            noise.evolution = raw_look.value("evolution", 0.12f);
            noise.contrast = raw_look.value("contrast", 1.4f);
            noise.rgb_noise = true;
            if (noise.octaves == 0 || noise.octaves > 16 || !std::isfinite(noise.frequency) || noise.frequency <= 0.0f || noise.frequency > 10.0f ||
                !std::isfinite(noise.amplitude) || noise.amplitude < 0.0f || noise.amplitude > 4.0f || !std::isfinite(noise.evolution) || std::abs(noise.evolution) > 10000.0f ||
                !std::isfinite(noise.contrast) || noise.contrast < 0.0f || noise.contrast > 16.0f)
                throw std::runtime_error("aurora settings are outside supported FractalNoise ranges");
            add_effect(std::move(noise));
            LayerPlan::EffectPlan warp;
            warp.kind = LayerPlan::EffectKindPlan::WaveWarp;
            warp.amount = raw_look.value("warp", 0.025f);
            warp.warp_wavelength = raw_look.value("wavelength", 0.35f);
            warp.warp_speed = raw_look.value("speed", 0.3f);
            warp.wave_type = 0;
            if (!std::isfinite(warp.amount) || std::abs(warp.amount) > 1.0f || !std::isfinite(warp.warp_wavelength) || warp.warp_wavelength <= 0.0f ||
                !std::isfinite(warp.warp_speed) || std::abs(warp.warp_speed) > 1000.0f)
                throw std::runtime_error("aurora warp settings are outside supported WaveWarp ranges");
            add_effect(std::move(warp));
        } else if (look_type == "liquid") {
            LayerPlan::EffectPlan wave;
            wave.kind = LayerPlan::EffectKindPlan::WaveWarp;
            wave.amount = raw_look.value("amplitude", 0.025f);
            wave.warp_wavelength = raw_look.value("wavelength", 0.2f);
            wave.warp_speed = raw_look.value("speed", 0.8f);
            if (!std::isfinite(wave.amount) || std::abs(wave.amount) > 1.0f || !std::isfinite(wave.warp_wavelength) || wave.warp_wavelength <= 0.0f ||
                !std::isfinite(wave.warp_speed) || std::abs(wave.warp_speed) > 1000.0f)
                throw std::runtime_error("liquid settings are outside supported WaveWarp ranges");
            add_effect(std::move(wave));
            LayerPlan::EffectPlan ripple;
            ripple.kind = LayerPlan::EffectKindPlan::Ripple;
            ripple.warp_center = {0.5f, 0.5f};
            ripple.warp_radius = raw_look.value("radius", 0.55f);
            ripple.warp_speed = raw_look.value("ripple_speed", 0.35f);
            ripple.warp_width = raw_look.value("ripple_width", 0.08f);
            ripple.warp_height = raw_look.value("ripple_height", 0.015f);
            ripple.warp_damping = raw_look.value("damping", 0.6f);
            if (!std::isfinite(ripple.warp_radius) || ripple.warp_radius <= 0.0f || ripple.warp_radius > 2.0f ||
                !std::isfinite(ripple.warp_speed) || std::abs(ripple.warp_speed) > 1000.0f || !std::isfinite(ripple.warp_width) || ripple.warp_width <= 0.0f ||
                !std::isfinite(ripple.warp_height) || std::abs(ripple.warp_height) > 1.0f || !std::isfinite(ripple.warp_damping) || ripple.warp_damping < 0.0f || ripple.warp_damping > 1.0f)
                throw std::runtime_error("liquid ripple settings are outside supported ranges");
            add_effect(std::move(ripple));
        } else {
            throw std::runtime_error("background_look.type must be film_grade, aurora, or liquid");
        }
    }
}

void decode_effect_parameter_tracks(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("effect_param_tracks")) {
        if (!value.at("effect_param_tracks").is_array() || value.at("effect_param_tracks").size() > 256)
            throw std::runtime_error("effect_param_tracks must be an array of at most 256 entries");
        for (const auto& raw_track : value.at("effect_param_tracks")) {
            const auto effect_id = raw_track.at("effect_id").get<std::string>();
            const auto param = raw_track.at("param").get<std::string>();
            if (!chronon3d::effects::is_canonical_effect_id(effect_id))
                throw std::runtime_error("effect parameter track uses an unknown canonical effect_id: " + effect_id);
            const auto* definition = chronon3d::effects::find_animatable_effect_param(effect_id, param);
            if (!definition)
                throw std::runtime_error("effect parameter is not declared animatable: " + effect_id + "." + param);
            const bool target_exists = std::any_of(layer.effects.begin(), layer.effects.end(),
                [effect_id](const LayerPlan::EffectPlan& effect) {
                    return effect.kind == LayerPlan::EffectKindPlan::Catalog
                        ? effect.canonical_effect_id == effect_id
                        : effect_id_for_plan_kind(effect.kind) == effect_id;
                });
            if (!target_exists)
                throw std::runtime_error("effect parameter track does not target an effect in this layer: " + effect_id);

            auto raw_animation = raw_track;
            raw_animation.erase("effect_id");
            raw_animation.erase("param");
            raw_animation["property"] = "opacity";
            auto track = decode_animation_track(raw_animation);
            if (track.keyframes.size() < 1)
                throw std::runtime_error("effect parameter tracks require keyframes");
            for (const auto& key : track.keyframes) {
                if (key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                    key.value.front() < definition->minimum || key.value.front() > definition->maximum)
                    throw std::runtime_error("effect parameter keyframe is outside the declared parameter range: " + effect_id + "." + param);
            }
            const auto duplicate = std::find_if(layer.effect_param_tracks.begin(), layer.effect_param_tracks.end(),
                [&](const LayerPlan::EffectParamTrackPlan& existing) {
                    return existing.effect_id == effect_id && existing.param == param;
                });
            if (duplicate != layer.effect_param_tracks.end())
                throw std::runtime_error("duplicate effect parameter track: " + effect_id + "." + param);
            layer.effect_param_tracks.push_back({effect_id, param, std::move(track)});
        }
    }
}

void decode_layer_text_spans(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("spans")) {
        if (layer.type != LayerType::Text)
            throw std::runtime_error("layer.spans is only valid for text layers");
        if (!value.at("spans").is_array() || value.at("spans").size() > 65536)
            throw std::runtime_error("layer.spans must be an array of at most 65536 entries");
        std::size_t previous_end = 0;
        std::vector<std::string> semantic_ids;
        const auto boundary = [&](std::size_t offset) {
            return offset == 0 || offset == layer.text.size() ||
                (static_cast<unsigned char>(layer.text[offset]) & 0xc0U) != 0x80U;
        };
        for (const auto& raw_span : value.at("spans")) {
            if (!raw_span.is_object())
                throw std::runtime_error("each text span must be an object");
            for (auto field = raw_span.begin(); field != raw_span.end(); ++field)
                if (field.key() != "start" && field.key() != "end" &&
                    field.key() != "semantic_id" && field.key() != "style")
                    throw std::runtime_error("text span contains unsupported field '" + field.key() + "'");
            const auto validOffset = [&](const nlohmann::json& offset) {
                if (!offset.is_number_integer()) return false;
                if (offset.is_number_unsigned())
                    return offset.get<std::uint64_t>() <= layer.text.size();
                const auto signed_offset = offset.get<std::int64_t>();
                return signed_offset >= 0 && static_cast<std::uint64_t>(signed_offset) <= layer.text.size();
            };
            if (!raw_span.contains("start") || !validOffset(raw_span.at("start")) ||
                !raw_span.contains("end") || !validOffset(raw_span.at("end")))
                throw std::runtime_error("text span start/end must be non-negative byte offsets within text");
            TextSpanOverride span;
            span.byte_start = raw_span.at("start").get<std::size_t>();
            span.byte_end = raw_span.at("end").get<std::size_t>();
            if (span.byte_start < previous_end || span.byte_start >= span.byte_end ||
                span.byte_end > layer.text.size() || !boundary(span.byte_start) || !boundary(span.byte_end))
                throw std::runtime_error("text spans must be sorted, non-overlapping UTF-8 byte ranges within text");
            previous_end = span.byte_end;
            if (raw_span.contains("semantic_id")) {
                if (!raw_span.at("semantic_id").is_string())
                    throw std::runtime_error("text span semantic_id must be a string");
                span.semantic_id = raw_span.at("semantic_id").get<std::string>();
                if (span.semantic_id->empty() || span.semantic_id->size() > 256)
                    throw std::runtime_error("text span semantic_id must contain 1..256 bytes");
                if (std::find(semantic_ids.begin(), semantic_ids.end(), *span.semantic_id) != semantic_ids.end())
                    throw std::runtime_error("text span semantic_id values must be unique per layer");
                semantic_ids.push_back(*span.semantic_id);
            }
            const auto styleIt = raw_span.find("style");
            if (styleIt != raw_span.end()) {
                const auto& style = *styleIt;
                if (!style.is_object() || style.empty())
                    throw std::runtime_error("text span style must be a non-empty object when present");
                for (auto field = style.begin(); field != style.end(); ++field)
                    if (field.key() != "color" && field.key() != "font_size_mult" &&
                        field.key() != "tracking" && field.key() != "stroke" && field.key() != "highlight")
                        throw std::runtime_error("text span style contains unsupported field '" + field.key() + "'");
                if (style.contains("color")) {
                    if (!style.at("color").is_string())
                        throw std::runtime_error("text span color must be a hex string");
                    const auto color = decode_text_span_color(style.at("color").get<std::string>());
                    if (!color) throw std::runtime_error("text span has invalid color");
                    span.color = *color;
                }
                if (style.contains("font_size_mult")) {
                    if (!style.at("font_size_mult").is_number())
                        throw std::runtime_error("text span font_size_mult must be a number");
                    const float mult = style.at("font_size_mult").get<float>();
                    if (!std::isfinite(mult) || mult <= 0.0f || mult > 100.0f)
                        throw std::runtime_error("text span font_size_mult must be finite and in (0, 100]");
                    span.font_size_multiplier = mult;
                }
                if (style.contains("tracking")) {
                    if (!style.at("tracking").is_number())
                        throw std::runtime_error("text span tracking must be a number");
                    const float tracking = style.at("tracking").get<float>();
                    if (!std::isfinite(tracking) || tracking < -1000.0f || tracking > 1000.0f)
                        throw std::runtime_error("text span tracking must be finite in [-1000, 1000]");
                    span.tracking = tracking;
                }
                if (style.contains("stroke")) {
                    const auto& raw_stroke = style.at("stroke");
                    if (!raw_stroke.is_object() || raw_stroke.empty())
                        throw std::runtime_error("text span stroke must be a non-empty object");
                    for (auto field = raw_stroke.begin(); field != raw_stroke.end(); ++field)
                        if (field.key() != "color" && field.key() != "width_em")
                            throw std::runtime_error("text span stroke contains unsupported field '" + field.key() + "'");
                    TextStrokeStyle stroke;
                    if (raw_stroke.contains("color")) {
                        if (!raw_stroke.at("color").is_string())
                            throw std::runtime_error("text span stroke color must be a hex string");
                        const auto color = decode_text_span_color(raw_stroke.at("color").get<std::string>());
                        if (!color) throw std::runtime_error("text span has invalid stroke color");
                        stroke.color = *color;
                    }
                    if (raw_stroke.contains("width_em")) {
                        if (!raw_stroke.at("width_em").is_number())
                            throw std::runtime_error("text span stroke width_em must be a number");
                        stroke.width_em = raw_stroke.at("width_em").get<float>();
                    }
                    if (!std::isfinite(stroke.width_em) || stroke.width_em < 0.0f || stroke.width_em > 100.0f)
                        throw std::runtime_error("text span stroke width_em must be finite and in [0, 100]");
                    span.stroke = stroke;
                }
                if (style.contains("highlight")) {
                    const auto& raw_highlight = style.at("highlight");
                    if (!raw_highlight.is_object() || raw_highlight.empty())
                        throw std::runtime_error("text span highlight must be a non-empty object");
                    for (auto field = raw_highlight.begin(); field != raw_highlight.end(); ++field)
                        if (field.key() != "background" && field.key() != "padding_em" && field.key() != "radius_em")
                            throw std::runtime_error("text span highlight contains unsupported field '" + field.key() + "'");
                    TextHighlightStyle highlight;
                    if (raw_highlight.contains("background")) {
                        if (!raw_highlight.at("background").is_string())
                            throw std::runtime_error("text span highlight background must be a hex string");
                        const auto color = decode_text_span_color(
                            raw_highlight.at("background").get<std::string>());
                        if (!color) throw std::runtime_error("text span has invalid highlight background color");
                        highlight.background = *color;
                    }
                    if (raw_highlight.contains("padding_em")) {
                        if (!raw_highlight.at("padding_em").is_number())
                            throw std::runtime_error("text highlight padding_em must be a number");
                        highlight.padding_em = raw_highlight.at("padding_em").get<float>();
                    }
                    if (raw_highlight.contains("radius_em")) {
                        if (!raw_highlight.at("radius_em").is_number())
                            throw std::runtime_error("text highlight radius_em must be a number");
                        highlight.radius_em = raw_highlight.at("radius_em").get<float>();
                    }
                    if ((highlight.padding_em && (!std::isfinite(*highlight.padding_em) ||
                            *highlight.padding_em < 0.0f || *highlight.padding_em > 100.0f)) ||
                        (highlight.radius_em && (!std::isfinite(*highlight.radius_em) ||
                            *highlight.radius_em < 0.0f || *highlight.radius_em > 100.0f)))
                        throw std::runtime_error("text highlight padding_em and radius_em must be finite in [0, 100]");
                    span.highlight = highlight;
                }
            }
            layer.text_spans.push_back(std::move(span));
        }
    }


}

} // namespace chronon3d::render_plan::detail
