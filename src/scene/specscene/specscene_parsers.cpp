#include "../../specscene/parser/specscene_parsers.hpp"
#include <chronon3d/core/string_utils.hpp>
#include <fmt/format.h>

namespace chronon3d::specscene {

using chronon3d::lower_copy;

Vec2 read_vec2(const toml::table& tbl, const char* key, Vec2 fallback) {
    const auto node = tbl[key];
    if (const auto* arr = node.as_array()) {
        if (arr->size() > 0) fallback.x = (*arr)[0].value_or<f32>(static_cast<f32>(fallback.x));
        if (arr->size() > 1) fallback.y = (*arr)[1].value_or<f32>(static_cast<f32>(fallback.y));
        return fallback;
    }
    if (const auto* subt = node.as_table()) {
        fallback.x = (*subt)["x"].value_or<f32>(static_cast<f32>(fallback.x));
        fallback.y = (*subt)["y"].value_or<f32>(static_cast<f32>(fallback.y));
    }
    return fallback;
}

Vec3 read_vec3(const toml::table& tbl, const char* key, Vec3 fallback) {
    const auto node = tbl[key];
    if (const auto* arr = node.as_array()) {
        if (arr->size() > 0) fallback.x = (*arr)[0].value_or<f32>(static_cast<f32>(fallback.x));
        if (arr->size() > 1) fallback.y = (*arr)[1].value_or<f32>(static_cast<f32>(fallback.y));
        if (arr->size() > 2) fallback.z = (*arr)[2].value_or<f32>(static_cast<f32>(fallback.z));
        return fallback;
    }
    if (const auto* subt = node.as_table()) {
        fallback.x = (*subt)["x"].value_or<f32>(static_cast<f32>(fallback.x));
        fallback.y = (*subt)["y"].value_or<f32>(static_cast<f32>(fallback.y));
        fallback.z = (*subt)["z"].value_or<f32>(static_cast<f32>(fallback.z));
    }
    return fallback;
}

Color read_color(const toml::table& tbl, const char* key, Color fallback) {
    const auto node = tbl[key];
    if (const auto* arr = node.as_array()) {
        if (arr->size() > 0) fallback.r = (*arr)[0].value_or<f32>(static_cast<f32>(fallback.r));
        if (arr->size() > 1) fallback.g = (*arr)[1].value_or<f32>(static_cast<f32>(fallback.g));
        if (arr->size() > 2) fallback.b = (*arr)[2].value_or<f32>(static_cast<f32>(fallback.b));
        if (arr->size() > 3) fallback.a = (*arr)[3].value_or<f32>(static_cast<f32>(fallback.a));
        return fallback;
    }
    if (const auto* subt = node.as_table()) {
        fallback.r = (*subt)["r"].value_or<f32>(static_cast<f32>(fallback.r));
        fallback.g = (*subt)["g"].value_or<f32>(static_cast<f32>(fallback.g));
        fallback.b = (*subt)["b"].value_or<f32>(static_cast<f32>(fallback.b));
        fallback.a = (*subt)["a"].value_or<f32>(static_cast<f32>(fallback.a));
    }
    return fallback;
}

BlendMode parse_blend_mode(std::string value, BlendMode fallback) {
    value = lower_copy(std::move(value));
    if (value == "add") return BlendMode::Add;
    if (value == "multiply") return BlendMode::Multiply;
    if (value == "screen") return BlendMode::Screen;
    if (value == "overlay") return BlendMode::Overlay;
    if (value == "normal") return BlendMode::Normal;
    return fallback;
}

DepthRole parse_depth_role(std::string value, DepthRole fallback) {
    value = lower_copy(std::move(value));
    if (value == "farbackground" || value == "far_background") return DepthRole::FarBackground;
    if (value == "background") return DepthRole::Background;
    if (value == "midground") return DepthRole::Midground;
    if (value == "subject") return DepthRole::Subject;
    if (value == "atmosphere") return DepthRole::Atmosphere;
    if (value == "foreground") return DepthRole::Foreground;
    if (value == "overlay") return DepthRole::Overlay;
    if (value == "none") return DepthRole::None;
    return fallback;
}

void note(std::vector<std::string>* diagnostics, std::string message) {
    if (diagnostics) diagnostics->push_back(std::move(message));
}

std::optional<VisualDesc> parse_visual(const toml::table& tbl, std::vector<std::string>* diagnostics) {
    const std::string type = lower_copy(read_scalar<std::string>(tbl, "type", ""));
    if (type.empty()) {
        note(diagnostics, "visual entry is missing required field `type`");
        return std::nullopt;
    }

    if (type == "rect") {
        RectParams p;
        p.size  = read_vec2(tbl, "size", p.size);
        p.color = read_color(tbl, "color", p.color);
        p.pos   = read_vec3(tbl, "pos", p.pos);
        return p;
    }
    if (type == "rounded_rect") {
        RoundedRectParams p;
        p.size   = read_vec2(tbl, "size", p.size);
        p.radius = read_scalar<f32>(tbl, "radius", p.radius);
        p.color  = read_color(tbl, "color", p.color);
        p.pos    = read_vec3(tbl, "pos", p.pos);
        return p;
    }
    if (type == "circle") {
        CircleParams p;
        p.radius = read_scalar<f32>(tbl, "radius", p.radius);
        p.color  = read_color(tbl, "color", p.color);
        p.pos    = read_vec3(tbl, "pos", p.pos);
        return p;
    }
    if (type == "line") {
        LineParams p;
        p.from      = read_vec3(tbl, "from", p.from);
        p.to        = read_vec3(tbl, "to", p.to);
        p.thickness = read_scalar<f32>(tbl, "thickness", p.thickness);
        p.color     = read_color(tbl, "color", p.color);
        return p;
    }
    if (type == "image") {
        ImageParams p;
        p.path    = read_scalar<std::string>(tbl, "path", p.path);
        p.size    = read_vec2(tbl, "size", p.size);
        p.pos     = read_vec3(tbl, "pos", p.pos);
        p.opacity = read_scalar<f32>(tbl, "opacity", p.opacity);
        return p;
    }
    note(diagnostics, fmt::format("unknown visual type `{}`", type));
    return std::nullopt;
}

std::vector<VisualDesc> parse_visuals(const toml::table& layer_tbl,
                                      std::vector<std::string>* diagnostics) {
    std::vector<VisualDesc> visuals;
    const auto node = layer_tbl["visuals"];
    if (const auto* arr = node.as_array()) {
        for (std::size_t i = 0; i < arr->size(); ++i) {
            const auto* item = arr->get(i);
            if (const auto* item_tbl = item ? item->as_table() : nullptr) {
                if (auto vd = parse_visual(*item_tbl, diagnostics)) {
                    visuals.push_back(std::move(*vd));
                }
            } else {
                note(diagnostics, fmt::format("layer.visuals[{}] is not a table", i));
            }
        }
    }
    return visuals;
}

std::optional<LayerDesc> parse_layer(const toml::table& tbl, std::vector<std::string>* diagnostics) {
    LayerDesc layer;
    layer.id   = read_scalar<std::string>(tbl, "id", "");
    layer.name = read_scalar<std::string>(tbl, "name", layer.id.empty() ? "Layer" : layer.id);

    if (layer.id.empty()) {
        note(diagnostics, fmt::format("layer `{}` is missing required field `id`", layer.name));
        return std::nullopt;
    }

    layer.time_range.from     = read_scalar<Frame>(tbl, "from", 0);
    layer.time_range.duration = read_scalar<Frame>(tbl, "duration", -1);
    layer.position            = AnimatedValue<Vec3>(read_vec3(tbl, "position", Vec3{0.0f, 0.0f, 0.0f}));
    layer.rotation            = AnimatedValue<Vec3>(read_vec3(tbl, "rotation", Vec3{0.0f, 0.0f, 0.0f}));
    layer.scale               = AnimatedValue<Vec3>(read_vec3(tbl, "scale", Vec3{1.0f, 1.0f, 1.0f}));
    layer.opacity             = read_animated_scalar<f32>(tbl, "opacity", 1.0f);
    layer.is_3d               = read_scalar<bool>(tbl, "is_3d", false);
    layer.depth_role          = parse_depth_role(read_scalar<std::string>(tbl, "depth_role", "none"), DepthRole::None);
    layer.depth_offset        = read_scalar<f32>(tbl, "depth_offset", 0.0f);
    layer.blend_mode          = parse_blend_mode(read_scalar<std::string>(tbl, "blend_mode", "normal"), BlendMode::Normal);
    layer.parent              = std::nullopt;
    layer.visuals              = parse_visuals(tbl, diagnostics);

    auto parse_trans = [&](const toml::table& t, const char* prefix, LayerTransitionSpec& spec) {
        std::string sub_tbl_key = prefix;
        if (const auto* sub_tbl = t[sub_tbl_key].as_table()) {
            spec.transition_id = read_scalar<std::string>(*sub_tbl, "id", "none");
            spec.duration = read_scalar<double>(*sub_tbl, "duration", 0.4);
            spec.delay = read_scalar<double>(*sub_tbl, "delay", 0.0);
            spec.direction = string_to_transition_direction(read_scalar<std::string>(*sub_tbl, "direction", "none"));
            spec.easing = string_to_easing(read_scalar<std::string>(*sub_tbl, "easing", "linear"));
        } else {
            std::string id_key = std::string(prefix) + "_id";
            std::string dur_key = std::string(prefix) + "_duration";
            std::string delay_key = std::string(prefix) + "_delay";
            std::string dir_key = std::string(prefix) + "_direction";
            std::string ease_key = std::string(prefix) + "_easing";
            if (t.contains(id_key)) {
                spec.transition_id = read_scalar<std::string>(t, id_key.c_str(), "none");
                spec.duration = read_scalar<double>(t, dur_key.c_str(), 0.4);
                spec.delay = read_scalar<double>(t, delay_key.c_str(), 0.0);
                spec.direction = string_to_transition_direction(read_scalar<std::string>(t, dir_key.c_str(), "none"));
                spec.easing = string_to_easing(read_scalar<std::string>(t, ease_key.c_str(), "linear"));
            }
        }
    };
    parse_trans(tbl, "transition_in", layer.transition_in);
    parse_trans(tbl, "transition_out", layer.transition_out);

    return layer;
}

std::optional<Camera2_5DAuthoring> parse_camera_2_5d(const toml::table& tbl,
                                                      std::vector<std::string>* diagnostics) {
    Camera2_5DAuthoring cam;
    cam.enabled = read_scalar<bool>(tbl, "enabled", true);
    cam.position.set(read_vec3(tbl, "position", cam.position.value_at(0)));
    cam.point_of_interest = read_vec3(tbl, "point_of_interest", cam.point_of_interest);
    cam.point_of_interest_enabled = tbl.contains("point_of_interest");
    cam.rotation.set(read_vec3(tbl, "rotation", cam.rotation.value_at(0)));
    cam.zoom = read_animated_scalar<f32>(tbl, "zoom", cam.zoom.value_at(0));
    return cam;
}

Camera parse_render_camera(const toml::table& tbl) {
    Camera cam;
    cam.fov_deg    = read_scalar<f32>(tbl, "fov_deg", cam.fov_deg);
    cam.near_plane = read_scalar<f32>(tbl, "near_plane", cam.near_plane);
    cam.far_plane  = read_scalar<f32>(tbl, "far_plane", cam.far_plane);
    cam.transform.position = read_vec3(tbl, "position", cam.transform.position);
    cam.transform.rotation = math::camera_rotation_quat(read_vec3(tbl, "rotation", Vec3{0.0f, 0.0f, 0.0f}));
    return cam;
}

std::optional<SpecSceneDocument> parse_document(const toml::table& root,
                                                std::vector<std::string>* diagnostics) {
    SpecSceneDocument doc;

    const toml::table* scene_tbl = &root;
    if (const auto* nested = root["scene"].as_table()) {
        scene_tbl = nested;
    }

    doc.scene.name = read_scalar<std::string>(*scene_tbl, "name", "Untitled");
    doc.scene.width = read_scalar<i32>(*scene_tbl, "width", doc.scene.width);
    doc.scene.height = read_scalar<i32>(*scene_tbl, "height", doc.scene.height);

    if (const auto fps_num = scene_tbl->get("fps_numerator"); fps_num && fps_num->is_integer()) {
        doc.scene.frame_rate.numerator = static_cast<i32>(fps_num->value_or<i64>(static_cast<i64>(doc.scene.frame_rate.numerator)));
        doc.scene.frame_rate.denominator = read_scalar<i32>(*scene_tbl, "fps_denominator", doc.scene.frame_rate.denominator);
    } else if (const auto fps = scene_tbl->get("fps"); fps && fps->is_integer()) {
        doc.scene.frame_rate.numerator = static_cast<i32>(fps->value_or<i64>(static_cast<i64>(doc.scene.frame_rate.numerator)));
        doc.scene.frame_rate.denominator = 1;
    }
    doc.scene.duration = read_scalar<Frame>(*scene_tbl, "duration", doc.scene.duration);

    if (const auto* cam_tbl = root["camera"].as_table()) {
        auto cam = parse_camera_2_5d(*cam_tbl, diagnostics);
        if (cam) doc.scene.camera = std::move(*cam);
    }

    if (const auto* cam_tbl = root["render_camera"].as_table()) {
        doc.render_camera = parse_render_camera(*cam_tbl);
        doc.has_render_camera = true;
    }

    const auto parse_layers_from = [&](const auto& node) {
        if (const auto* arr = node.as_array()) {
            for (std::size_t i = 0; i < arr->size(); ++i) {
                const auto* item = arr->get(i);
                if (const auto* layer_tbl = item ? item->as_table() : nullptr) {
                    if (auto layer = parse_layer(*layer_tbl, diagnostics)) {
                        doc.scene.layers.push_back(std::move(*layer));
                    }
                } else {
                    note(diagnostics, fmt::format("layer[{}] is not a table", i));
                }
            }
        }
    };

    parse_layers_from(root["layer"]);
    parse_layers_from(root["layers"]);

    if (doc.scene.layers.empty()) {
        note(diagnostics, "specscene contains no layers");
    }

    return doc;
}

} // namespace chronon3d::specscene
