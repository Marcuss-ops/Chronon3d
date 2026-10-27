#include "render_plan_decoder_detail.hpp"
#include <chronon3d/render_plan/animation_property_registry.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

namespace chronon3d::render_plan::detail {

std::optional<Frame> optional_frame(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return Frame{object.at(key).get<std::int64_t>()};
}

void decode_layer_asset_fields(LayerPlan& layer, const nlohmann::json& value) {
    layer.asset = value.value("asset", std::string{});
    layer.input_color_space = value.value("input_color_space", std::string{"srgb"});
    layer.ocio_input_space = value.value("ocio_input_space", std::string{});
    if (layer.type != LayerType::Image && layer.type != LayerType::Video &&
        value.contains("ocio_input_space"))
        throw std::runtime_error("ocio_input_space is only valid for image or video layers");
    if (layer.type != LayerType::Image &&
        !(layer.type == LayerType::Mesh && !layer.asset.empty()) &&
        value.contains("input_color_space"))
        throw std::runtime_error("input_color_space is only valid for image layers");
    if (layer.input_color_space != "srgb" && layer.input_color_space != "linearsrgb" &&
        layer.input_color_space != "rec709" && layer.input_color_space != "displayp3")
        throw std::runtime_error(
            "image input_color_space must be srgb, linearsrgb, rec709, or displayp3");
    layer.source = value.value("source", std::string{});
    if (layer.type == LayerType::Mesh) {
        for (const auto* key : {"text", "shape", "particle_emitter", "light", "style", "background_look", "mask", "masks", "text_animators", "spans", "fill_gradient_animation"})
            if (value.contains(key))
                throw std::runtime_error(std::string("mesh layers cannot contain '") + key + "'");
        if (value.contains("animation") && value.at("animation").is_object() &&
            value.at("animation").contains("tracks") && !value.at("animation").at("tracks").empty())
            throw std::runtime_error("mesh layers cannot contain per-vertex animation tracks");
    }
    if (layer.type == LayerType::Mesh && !layer.source.empty())
        throw std::runtime_error("mesh layers cannot carry a video source reference");
    decode_layer_inline_mesh(layer, value);
    if (layer.type == LayerType::Mesh && !layer.asset.empty() && !layer.inline_mesh)
        throw std::runtime_error("mesh texture assets require inline_mesh geometry");
    if (layer.type == LayerType::Mesh && !layer.asset.empty() &&
        layer.input_color_space != "srgb")
        throw std::runtime_error("inline mesh textures currently require srgb input_color_space");
}

void decode_layer_size_color_fields(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("size")) {
        const auto& size = value.at("size");
        layer.size_dimensions = size.size();
        for (std::size_t i = 0; i < size.size() && i < layer.size.size(); ++i) {
            layer.size[i] = size.at(i).get<float>();
            if (!std::isfinite(layer.size[i]))
                throw std::runtime_error("layer.size components must be finite");
        }
    }
    if (value.contains("color")) {
        const auto& color = value.at("color");
        if (!color.is_array() || color.size() != 4)
            throw std::runtime_error("layer.color must contain RGBA");
        for (std::size_t i = 0; i < layer.color.size(); ++i) {
            layer.color[i] = color.at(i).get<float>();
            if (!std::isfinite(layer.color[i]))
                throw std::runtime_error("layer.color components must be finite");
        }
    }

}

void decode_layer_backdrop_filter(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("backdrop_filter")) {
        const auto& raw_bf = value.at("backdrop_filter");
        BackdropFilterPlan filter;
        filter.blur = raw_bf.value("blur", filter.blur);
        filter.brightness = raw_bf.value("brightness", filter.brightness);
        filter.saturation = raw_bf.value("saturation", filter.saturation);
        filter.refraction_strength = raw_bf.value("refraction_strength", filter.refraction_strength);
        if (raw_bf.contains("refraction_field"))
            filter.refraction_field = decode_field2d_plan(raw_bf.at("refraction_field"));
        filter.rim_strength = raw_bf.value("rim_strength", filter.rim_strength);
        filter.rim_width = raw_bf.value("rim_width", filter.rim_width);
        filter.chromatic_dispersion = raw_bf.value("chromatic_dispersion", filter.chromatic_dispersion);
        filter.grain = raw_bf.value("grain", filter.grain);
        filter.opacity = raw_bf.value("opacity", filter.opacity);
        filter.validate();
        layer.backdrop_filter = filter;
    }
}

void validate_adjustment_layer(LayerPlan& layer, const nlohmann::json& value) {
    if (layer.type == LayerType::Adjustment) {
        if (layer.effects.empty())
            throw std::runtime_error("adjustment layers require at least one effect");
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.key() != "id" && it.key() != "type" && it.key() != "effects" &&
                it.key() != "effect_param_tracks" && it.key() != "background_look")
                throw std::runtime_error(
                    "adjustment layers support only 'id', 'type', and a non-empty 'effects' stack; found '" +
                    it.key() + "'");
        }
        layer.type = LayerType::Adjustment;
    }
}

void decode_layer_render_flags(LayerPlan& layer, const nlohmann::json& value) {
    layer.enable_3d = value.value("enable_3d", false);
    if (value.contains("casts_shadows"))
        layer.casts_shadows = value.at("casts_shadows").get<bool>();
    if (value.contains("accepts_shadows"))
        layer.accepts_shadows = value.at("accepts_shadows").get<bool>();
    layer.screen_space = value.value("screen_space", false);
    if (value.contains("safe_insets")) {
        const auto& insets = value.at("safe_insets");
        if (!insets.is_object())
            throw std::runtime_error("layer.safe_insets must be an object");
        std::array<float, 4> parsed{
            insets.value("top", 0.0f), insets.value("right", 0.0f),
            insets.value("bottom", 0.0f), insets.value("left", 0.0f)};
        for (const float inset : parsed)
            if (!std::isfinite(inset) || inset < 0.0f)
                throw std::runtime_error("layer.safe_insets values must be finite and non-negative");
        layer.safe_insets = parsed;
    }
    if (value.contains("depth_role")) {
        layer.depth_role = value.at("depth_role").get<std::string>();
        static constexpr std::array<std::string_view, 7> kRoles{
            "far_background", "background", "midground", "subject",
            "atmosphere", "foreground", "overlay"};
        if (std::find(kRoles.begin(), kRoles.end(), *layer.depth_role) == kRoles.end())
            throw std::runtime_error(
                "unsupported depth_role '" + *layer.depth_role +
                "'; valid roles: far_background, background, midground, subject, atmosphere, foreground, overlay");
    }
    if (value.contains("depth_offset"))
        layer.depth_offset = value.at("depth_offset").get<float>();

}

void decode_layer_frame_crop_fields(LayerPlan& layer, const nlohmann::json& value) {
    layer.start_frame = optional_frame(value, "start_frame");
    layer.duration_frames = optional_frame(value, "duration_frames");
    if (value.contains("fit")) layer.fit = fit_mode(value.at("fit").get<std::string>());
    if (value.contains("crop")) {
        if (layer.type != LayerType::Image)
            throw std::runtime_error("crop is only valid for image layers");
        const auto& raw_crop = value.at("crop");
        ImageCropPlan crop;
        crop.enabled = raw_crop.value("enabled", true);
        if (raw_crop.contains("origin")) crop.origin = raw_crop.at("origin").get<std::array<float, 2>>();
        if (raw_crop.contains("size")) crop.size = raw_crop.at("size").get<std::array<float, 2>>();
        crop.flip_x = raw_crop.value("flip_x", false);
        for (std::size_t i = 0; i < 2; ++i) {
            const float origin = crop.origin[i];
            const float extent = crop.size[i];
            if (!std::isfinite(origin) || !std::isfinite(extent) || origin < 0.0f ||
                extent <= 0.0f || origin + extent > 1.0f)
                throw std::runtime_error("crop origin and size must define a finite normalized rectangle");
        }
        layer.crop = crop;
    }
    if (value.contains("radius"))
        layer.radius = value.at("radius").get<float>();

}

void decode_layer_animation(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("animation")) {
        AnimationPlan animation;
        for (const auto& track_value : value.at("animation").at("tracks")) {
            auto track = decode_animation_track(track_value);
            // Fail closed on unregistered property names: a track the decode
            // pipeline does not know must never survive into a compiled plan
            // where the compiler's property dispatch would silently drop it.
            if (!render_plan::find_animation_property(track.property)) {
                std::string message =
                    "layers[].animation track uses an unregistered property: " +
                    track.property;
                if (const auto nearest =
                        render_plan::nearest_animation_property_name(track.property);
                    !nearest.empty())
                    message += " (did you mean '" + std::string{nearest} + "?')";
                throw std::runtime_error(message);
            }
            animation.tracks.push_back(std::move(track));
        }
        layer.animation = std::move(animation);
    }

}

void decode_layer_motion_blur(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("motion_blur")) {
        const auto& raw = value.at("motion_blur");
        MotionBlurPlan motion_blur;
        motion_blur.shutter_angle = raw.value("shutter_angle", 180.0f);
        motion_blur.samples = raw.value("samples", std::uint32_t{12});
        if (!std::isfinite(motion_blur.shutter_angle) ||
            motion_blur.shutter_angle <= 0.0f || motion_blur.shutter_angle > 360.0f ||
            motion_blur.samples == 0 || motion_blur.samples > 64)
            throw std::runtime_error("motion_blur requires shutter_angle in (0, 360] and samples in [1, 64]");
        if (!layer.animation)
            throw std::runtime_error("motion_blur requires an animated layer position track");
        bool has_position = false;
        for (const auto& track : layer.animation->tracks) {
            has_position = has_position || track.property == "position" ||
                track.property == "position_x" || track.property == "position_y" ||
                track.property == "position_z";
            if (track.property == "scale" || track.property.starts_with("scale_") ||
                track.property == "rotation" || track.property.starts_with("rotation_"))
                throw std::runtime_error("motion_blur currently requires translational motion; scale/rotation tracks need temporal accumulation");
        }
        if (!has_position)
            throw std::runtime_error("motion_blur requires an animated layer position track");
        layer.motion_blur = motion_blur;
    }

}

void decode_layer_final_fields(LayerPlan& layer, const nlohmann::json& value) {
    decode_layer_text_animators(layer, value);
    if (value.contains("blend_mode")) {
        if (const auto mode = blend_mode(value.at("blend_mode").get<std::string>()))
            layer.blend_mode = mode;
    }
    if (value.contains("opacity"))
        layer.opacity = value.at("opacity").get<float>();
    layer.loop = value.value("loop", false);

}

}  // namespace chronon3d::render_plan::detail
