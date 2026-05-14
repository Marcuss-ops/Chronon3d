#include <chronon3d/specscene/specscene.hpp>
#include <chronon3d/evaluation/legacy_scene_adapter.hpp>
#include <chronon3d/evaluation/timeline_evaluator.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/layer/depth_role.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/transform.hpp>
#include <toml++/toml.h>
#include <fmt/format.h>
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace chronon3d::specscene {

namespace {

std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

template <typename T>
T read_scalar(const toml::table& tbl, const char* key, T fallback) {
    return tbl[key].value_or<T>(T{fallback});
}

template <typename T>
AnimatedValue<T> read_animated_scalar(const toml::table& tbl, const char* key, T fallback) {
    const auto node = tbl[key];
    if (const auto* str = node.as_string()) {
        AnimatedValue<T> av(fallback);
        av.expression(str->get());
        return av;
    }
    return AnimatedValue<T>(read_scalar<T>(tbl, key, fallback));
}

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

TextAlign parse_text_align(std::string value, TextAlign fallback = TextAlign::Left) {
    value = lower_copy(std::move(value));
    if (value == "center") return TextAlign::Center;
    if (value == "right") return TextAlign::Right;
    if (value == "left") return TextAlign::Left;
    return fallback;
}

BlendMode parse_blend_mode(std::string value, BlendMode fallback = BlendMode::Normal) {
    value = lower_copy(std::move(value));
    if (value == "add") return BlendMode::Add;
    if (value == "multiply") return BlendMode::Multiply;
    if (value == "screen") return BlendMode::Screen;
    if (value == "overlay") return BlendMode::Overlay;
    if (value == "normal") return BlendMode::Normal;
    return fallback;
}

DepthRole parse_depth_role(std::string value, DepthRole fallback = DepthRole::None) {
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

template <typename T>
void read_style_text(const toml::table& tbl, T& style) {
    style.font_path  = read_scalar<std::string>(tbl, "font_path", style.font_path);
    style.size       = read_scalar<f32>(tbl, "size", style.size);
    style.color      = read_color(tbl, "color", style.color);
    style.align      = parse_text_align(read_scalar<std::string>(tbl, "align", "left"), style.align);
    style.line_height = read_scalar<f32>(tbl, "line_height", style.line_height);
    style.tracking   = read_scalar<f32>(tbl, "tracking", style.tracking);
    style.max_lines  = read_scalar<int>(tbl, "max_lines", style.max_lines);
    style.auto_scale = read_scalar<bool>(tbl, "auto_scale", style.auto_scale);
    style.min_size   = read_scalar<f32>(tbl, "min_size", style.min_size);
    style.max_size   = read_scalar<f32>(tbl, "max_size", style.max_size);
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
    if (type == "text") {
        TextParams p;
        p.content = read_scalar<std::string>(tbl, "content", p.content);
        p.pos     = read_vec3(tbl, "pos", p.pos);
        read_style_text(tbl, p.style);
        if (p.style.font_path.empty()) p.style.font_path = "assets/fonts/Inter-Bold.ttf";
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
    if (type == "fake_extruded_text") {
        FakeExtrudedTextParams p;
        p.text               = read_scalar<std::string>(tbl, "text", p.text);
        p.font_path          = read_scalar<std::string>(tbl, "font_path", p.font_path);
        p.pos                = read_vec3(tbl, "pos", p.pos);
        p.font_size          = read_scalar<f32>(tbl, "font_size", p.font_size);
        p.depth              = read_scalar<int>(tbl, "depth", p.depth);
        p.extrude_dir        = read_vec2(tbl, "extrude_dir", p.extrude_dir);
        p.extrude_z_step     = read_scalar<f32>(tbl, "extrude_z_step", p.extrude_z_step);
        p.front_color        = read_color(tbl, "front_color", p.front_color);
        p.side_color         = read_color(tbl, "side_color", p.side_color);
        p.side_fade          = read_scalar<f32>(tbl, "side_fade", p.side_fade);
        p.align              = parse_text_align(read_scalar<std::string>(tbl, "align", "center"), p.align);
        p.highlight_opacity  = read_scalar<f32>(tbl, "highlight_opacity", p.highlight_opacity);
        p.bevel_size         = read_scalar<f32>(tbl, "bevel_size", p.bevel_size);
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

    return layer;
}

std::optional<Camera2_5DDesc> parse_camera_2_5d(const toml::table& tbl, std::vector<std::string>* diagnostics) {
    Camera2_5DDesc cam;
    cam.enabled = read_scalar<bool>(tbl, "enabled", true);
    cam.position.set(read_vec3(tbl, "position", cam.position.value_at(0)));
    cam.point_of_interest = read_vec3(tbl, "point_of_interest", cam.point_of_interest);
    cam.zoom = read_animated_scalar<f32>(tbl, "zoom", cam.zoom.value_at(0));
    return cam;
}

Camera parse_render_camera(const toml::table& tbl) {
    Camera cam;
    cam.fov_deg    = read_scalar<f32>(tbl, "fov_deg", cam.fov_deg);
    cam.near_plane = read_scalar<f32>(tbl, "near_plane", cam.near_plane);
    cam.far_plane  = read_scalar<f32>(tbl, "far_plane", cam.far_plane);
    cam.transform.position = read_vec3(tbl, "position", cam.transform.position);
    cam.transform.rotation = math::from_euler(read_vec3(tbl, "rotation", Vec3{0.0f, 0.0f, 0.0f}));
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

} // namespace

bool is_specscene_file(const std::filesystem::path& path) {
    auto ext = lower_copy(path.extension().string());
    return ext == ".specscene" || ext == ".toml";
}

std::optional<SpecSceneDocument> load_file(const std::filesystem::path& path,
                                           std::vector<std::string>* diagnostics) {
    try {
        const auto parsed = toml::parse_file(path.string());
        return parse_document(parsed, diagnostics);
    } catch (const std::exception& e) {
        note(diagnostics, fmt::format("failed to parse `{}`: {}", path.string(), e.what()));
        return std::nullopt;
    }
}

std::optional<Composition> compile_file(const std::filesystem::path& path,
                                        std::vector<std::string>* diagnostics) {
    auto doc = load_file(path, diagnostics);
    if (!doc) return std::nullopt;

    CompositionSpec spec;
    spec.name = doc->scene.name;
    spec.width = doc->scene.width;
    spec.height = doc->scene.height;
    spec.frame_rate = doc->scene.frame_rate;
    spec.duration = doc->scene.duration;

    SceneDescription scene = std::move(doc->scene);
    Composition comp(spec, [scene = std::move(scene)](const FrameContext& ctx) {
        TimelineEvaluator evaluator;
        const auto evaluated = evaluator.evaluate(scene, ctx.frame);
        LegacySceneAdapter adapter;
        return adapter.convert(evaluated, ctx.resource);
    });

    if (doc->has_render_camera) {
        comp.camera = doc->render_camera;
    }

    return comp;
}

} // namespace chronon3d::specscene
