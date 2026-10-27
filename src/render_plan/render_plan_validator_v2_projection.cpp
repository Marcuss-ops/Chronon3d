#include <chronon3d/render_plan/easing_contract.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include "render_plan_validator_detail.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace chronon3d::render_plan {
namespace {
void strip_per_key_timing_for_v2_projection(nlohmann::json& value,
                                             bool keyframes = false) {
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it)
            strip_per_key_timing_for_v2_projection(it.value(), it.key() == "keyframes");
    } else if (value.is_array()) {
        if (keyframes) {
            for (auto& key : value) {
                if (key.is_object()) {
                    key.erase("interpolation");
                    key.erase("easing");
                    key.erase("bezier_curve");
                    key.erase("roving");
                    key.erase("temporal_in_dx");
                    key.erase("temporal_in_dy");
                    key.erase("temporal_out_dx");
                    key.erase("temporal_out_dy");
                    key.erase("spring");
                }
            }
        }
        for (auto& item : value)
            strip_per_key_timing_for_v2_projection(item, false);
    }
}

// The frozen V2 layer-track property enum, verbatim. A V3 plan may animate
// properties the legacy enum cannot express; those tracks are dropped from the
// validation projection because the V3 extension schema is their authority.
// Listing the V2-expressible set (instead of erasing the V3-only one) means a
// newly added V3 paint track cannot silently make CLI validation reject a plan
// the extension schema accepts — which is exactly how stroke_width,
// stroke_color and shadow_<n>_* came to be unrenderable at the interface.
[[nodiscard]] bool is_v2_layer_track_property(std::string_view property) {
    static constexpr std::array<std::string_view, 15> v2_properties{
        "position", "position_x", "position_y", "position_z",
        "scale", "scale_x", "scale_y", "scale_z",
        "rotation", "rotation_x", "rotation_y", "rotation_z",
        "opacity", "blur", "stroke_dash_offset"};
    return std::find(v2_properties.begin(), v2_properties.end(), property) !=
           v2_properties.end();
}


}  // namespace

nlohmann::json validation_detail::v2_projection(const nlohmann::json& root) {
    nlohmann::json projected = root;
    projected.erase("temporal_motion_blur");
    projected.erase("audio");
    projected.erase("warnings");
    projected["schema"] = kRenderPlanSchemaV2;
    projected["version"] = 2;
    if (!projected.contains("layers") || !projected["layers"].is_array())
        return projected;
    projected.erase("camera");
    projected.erase("lighting_rig");

    for (auto& layer : projected["layers"]) {
        if (!layer.is_object()) continue;
        // These V3-only mesh material switches are validated by the extension
        // schema above; remove them from the frozen V2 compatibility view.
        layer.erase("casts_shadows");
        layer.erase("accepts_shadows");
        if (layer.contains("animation"))
            strip_per_key_timing_for_v2_projection(layer["animation"]);
        if (layer.value("type", std::string{}) == "mesh") {
            layer["type"] = "color";
            if (!layer.contains("color")) layer["color"] = {1.0, 1.0, 1.0, 1.0};
        }
        const bool shape_layer = layer.value("type", std::string{}) == "shape";
        const bool adjustment_layer = layer.value("type", std::string{}) == "adjustment";
        if (shape_layer || adjustment_layer) {
            // The base V2 validator is authoritative for every common field;
            // supply only the neutral V2 layer discriminator/payload needed
            // to pass V2's closed enum and required-color condition.
            layer["type"] = "color";
            if (!layer.contains("color")) layer["color"] = {1.0, 1.0, 1.0, 1.0};
        }
        if (layer.contains("shape") && layer["shape"].is_object()) {
            auto& shape = layer["shape"];
            if (shape.contains("fill") && shape["fill"].is_array())
                layer["color"] = shape["fill"];
            // Gradient, field and mesh fills have no V2 color-array equivalent.
            // Remove only from the validation projection; the source plan is
            // unchanged and the V3 decoder retains the typed fill payload.
            if (shape.contains("fill")) shape.erase("fill");
            if (shape.contains("stroke") && shape["stroke"].is_object()) {
                auto& stroke = shape["stroke"];
                if (stroke.contains("color") && stroke["color"].is_array())
                    stroke["color"] = "#FFFFFF";
            }
        }
        if (layer.contains("style") && layer["style"].is_object()) {
            if (layer["style"].contains("gradient")) layer["style"].erase("gradient");
            // V3 text direction augments the frozen V2 text style contract.
            layer["style"].erase("direction");
            // Backdrop blur is a V3 graph-composite extension. The V2
            // projection validates common style fields only; the V3 schema
            // below validates the typed backdrop payload itself.
            layer["style"].erase("backdrop");
            if (layer["style"].contains("stroke") && layer["style"]["stroke"].is_object()) {
                auto& stroke = layer["style"]["stroke"];
                if (stroke.contains("gradient")) stroke.erase("gradient");
                if (!stroke.contains("color")) stroke["color"] = "#FFFFFF";
                if (!stroke.contains("width")) stroke["width"] = 0.0;
            }
        }
        // Layer-level fill tracks are V3 extensions. Remove only these tracks
        // from the frozen V2 projection while retaining validation of all
        // shared animation properties.
        layer.erase("fill_gradient_animation");
        // V3 layout descriptors and their stagger authoring data are validated
        // by the extension schema and do not belong to the frozen V2 shape.
        layer.erase("flow");
        layer.erase("grid_layout");
        layer.erase("stagger_group");
        layer.erase("follow_path");
        layer.erase("auto_orient");
        layer.erase("effect_param_tracks");
        layer.erase("skew");
        layer.erase("skew_axis");
        // Typed background looks are expanded by the V3 decoder and are not
        // representable in the frozen V2 projection.
        layer.erase("background_look");
        layer.erase("backdrop_filter");
        layer.erase("transition_in");
        layer.erase("transition_out");
        if (layer.contains("animation") && layer["animation"].is_object() &&
            layer["animation"].contains("tracks") && layer["animation"]["tracks"].is_array()) {
            auto& tracks = layer["animation"]["tracks"];
            // Procedural scalar drivers are V3-only fields. The V3 extension
            // schema validates their strict shape; remove them from the
            // frozen V2 projection so the legacy additionalProperties:false
            // rule does not reject valid V3 tracks.
            for (auto& track : tracks) {
                if (!track.is_object()) continue;
                track.erase("oscillate");
                track.erase("random_hold");
                track.erase("posterize_time");
                track.erase("data_feed");
                track.erase("audio_react");
                track.erase("loop_mode");
                track.erase("wiggle");
                track.erase("spring");
                if (track.value("easing", std::string{}) == detail::easing_name::kSpring)
                    track.erase("easing");
                if (!track.contains("keyframes"))
                    track["keyframes"] = nlohmann::json::array({
                        nlohmann::json{{"frame", 0}, {"value", 0.0}}});
                if (track.contains("keyframes") && track["keyframes"].is_array())
                    for (auto& keyframe : track["keyframes"])
                        if (keyframe.is_object()) keyframe.erase("easing_xy");
            }
            tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [](const auto& track) {
                if (!track.is_object()) return false;
                return !is_v2_layer_track_property(track.value("property", std::string{}));
            }), tracks.end());
            if (tracks.empty()) layer.erase("animation");
        }
        layer.erase("shape");
        layer.erase("inline_mesh");
        layer.erase("parent");
        layer.erase("track_matte");
        layer.erase("time_remap");
        layer.erase("transition_in");
        layer.erase("transition_out");
        // V3-only fields are validated by the extension schema below.
        layer.erase("safe_insets");
        layer.erase("font");
        layer.erase("spans");
        layer.erase("motion_blur");
        // V3 owns text-animator validation: its animator_property_track
        // override extends the frozen V2 property enum with the per-glyph
        // 'rotation' and 'fill_color' tracks. The extension schema validates
        // the whole text_animators block with the V2-level strictness, so the
        // projection must not re-validate it against the frozen V2 enum.
        layer.erase("text_animators");
        // V3 text counters have their own strict extension contract; keep them
        // out of the frozen V2 projection after the extension pass validates.
        layer.erase("text_counter");
        // V3 path masks are validated by the extension schema above. The V2
        // projection exists only to retain validation of fields shared with
        // the legacy schema, which correctly rejects this V3-only field.
        layer.erase("mask");
        layer.erase("masks");
        layer.erase("effects");
        // V3 particle emitters add path/sprite sampling and rotation fields.
        // Their complete shape has already passed the extension schema; keep
        // the shared V2 emitter fields in this projection, but remove only
        // those V3 additions before applying V2's additionalProperties:false.
        if (layer.contains("particle_emitter") && layer["particle_emitter"].is_object()) {
            auto& emitter = layer["particle_emitter"];
            for (const auto* key : {"shape", "path_id", "path_points", "sprite_asset",
                                    "sprite_scale", "rotation", "angular_velocity",
                                    "environment", "depth_range", "bokeh"})
                emitter.erase(key);
        }
        if (layer.contains("style") && layer["style"].is_object()) {
            layer["style"].erase("gradient");
            if (layer["style"].contains("stroke") &&
                layer["style"]["stroke"].is_object())
                layer["style"]["stroke"].erase("gradient");
        }
    }
    return projected;
}


}  // namespace chronon3d::render_plan
