#include "video_camera_preset.hpp"
#include "../commands.hpp"

#include <toml++/toml.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <functional>
#include <utility>

namespace chronon3d {
namespace cli {

namespace {

std::optional<std::filesystem::path> find_config_from(const std::filesystem::path& start) {
    std::filesystem::path current = start;
    if (current.empty()) {
        current = std::filesystem::current_path();
    }

    while (true) {
        const auto candidate = current / "chronon.toml";
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }

        if (!current.has_parent_path() || current.parent_path() == current) {
            break;
        }
        current = current.parent_path();
    }

    return std::nullopt;
}

template <typename T>
std::optional<T> read_optional(const toml::table& tbl, std::string_view key) {
    if (!tbl.contains(key)) {
        return std::nullopt;
    }
    return tbl[key].value_or<T>(T{});
}

std::optional<Vec3> read_vec3_node(const toml::node& node) {
    if (const auto* arr = node.as_array()) {
        if (arr->size() >= 3) {
            const f32 x = (*arr)[0].value_or<f32>(0.0f);
            const f32 y = (*arr)[1].value_or<f32>(0.0f);
            const f32 z = (*arr)[2].value_or<f32>(0.0f);
            return Vec3{x, y, z};
        }
    }
    if (const auto* tbl = node.as_table()) {
        return Vec3{
            (*tbl)["x"].value_or<f32>(0.0f),
            (*tbl)["y"].value_or<f32>(0.0f),
            (*tbl)["z"].value_or<f32>(0.0f),
        };
    }
    return std::nullopt;
}

std::optional<Vec3> read_optional_vec3(const toml::table& tbl, std::string_view key) {
    const auto* node = tbl.get(key);
    if (!node) {
        return std::nullopt;
    }
    return read_vec3_node(*node);
}

template <typename Fn>
auto read_optional_nested(const toml::table& tbl, std::string_view key, Fn&& fn)
    -> std::optional<std::invoke_result_t<Fn, const toml::table&>> {
    const auto* node = tbl.get(key);
    if (!node) {
        return std::nullopt;
    }
    const auto* subt = node->as_table();
    if (!subt) {
        return std::nullopt;
    }
    return std::invoke(std::forward<Fn>(fn), *subt);
}

template <typename T, typename Fn>
auto transform_optional(const std::optional<T>& value, Fn&& fn)
    -> std::optional<std::invoke_result_t<Fn, const T&>> {
    if (!value) {
        return std::nullopt;
    }
    return std::invoke(std::forward<Fn>(fn), *value);
}

std::string resolve_path(const std::filesystem::path& base, const std::string& value) {
    if (value.empty()) {
        return {};
    }

    std::filesystem::path path(value);
    if (path.is_absolute()) {
        return path.lexically_normal().string();
    }

    return (base / path).lexically_normal().string();
}

CameraVideoPreset::Pose parse_pose(const toml::table& tbl) {
    CameraVideoPreset::Pose pose;
    pose.position = read_optional_vec3(tbl, "position");
    pose.rotation = read_optional_vec3(tbl, "rotation");
    if (const auto zoom = read_optional<f64>(tbl, "zoom")) {
        pose.zoom = static_cast<float>(*zoom);
    }
    return pose;
}

CameraVideoPreset::Primary parse_primary(const toml::table& tbl) {
    CameraVideoPreset::Primary primary;
    primary.from = read_optional_nested(tbl, "from", parse_pose);
    primary.to = read_optional_nested(tbl, "to", parse_pose);
    if (const auto duration = read_optional<chronon3d::i64>(tbl, "duration")) {
        primary.duration = static_cast<Frame>(*duration);
    }
    primary.easing = read_optional<std::string>(tbl, "easing");
    if (const auto enabled = read_optional<bool>(tbl, "enabled")) {
        primary.enabled = *enabled;
    }

    return primary;
}

CameraVideoPreset::Idle parse_idle(const toml::table& tbl) {
    CameraVideoPreset::Idle idle;
    if (const auto enabled = read_optional<bool>(tbl, "enabled")) {
        idle.enabled = *enabled;
    }
    idle.position_amplitude = read_optional_vec3(tbl, "position_amplitude");
    idle.rotation_amplitude_deg = read_optional_vec3(tbl, "rotation_amplitude_deg");
    if (const auto zoom = read_optional<f64>(tbl, "zoom_amplitude")) {
        idle.zoom_amplitude = static_cast<float>(*zoom);
    }
    if (const auto hz = read_optional<f64>(tbl, "frequency_hz")) {
        idle.frequency_hz = static_cast<float>(*hz);
    }
    if (const auto phase = read_optional<f64>(tbl, "phase_offset")) {
        idle.phase_offset = static_cast<float>(*phase);
    }
    if (const auto base_on_final = read_optional<bool>(tbl, "base_on_final")) {
        idle.base_on_final = *base_on_final;
    }
    return idle;
}

std::optional<CameraVideoPreset::Pose> parse_motion_pose(const toml::table& preset_tbl) {
    const auto* motion_node = preset_tbl.get("motion");
    const auto* motion_tbl = motion_node ? motion_node->as_table() : nullptr;
    if (!motion_tbl) {
        return std::nullopt;
    }
    return read_optional_nested(*motion_tbl, "pose", parse_pose);
}

std::optional<CameraVideoPreset::Primary> parse_motion_primary(const toml::table& preset_tbl) {
    const auto* motion_node = preset_tbl.get("motion");
    const auto* motion_tbl = motion_node ? motion_node->as_table() : nullptr;
    if (!motion_tbl) {
        return std::nullopt;
    }
    return read_optional_nested(*motion_tbl, "primary", parse_primary);
}

std::optional<CameraVideoPreset::Idle> parse_motion_idle(const toml::table& preset_tbl) {
    const auto* motion_node = preset_tbl.get("motion");
    const auto* motion_tbl = motion_node ? motion_node->as_table() : nullptr;
    if (!motion_tbl) {
        return std::nullopt;
    }
    return read_optional_nested(*motion_tbl, "idle", parse_idle);
}

} // namespace

std::optional<std::filesystem::path> locate_chronon_toml() {
    return find_config_from(std::filesystem::current_path());
}

std::size_t count_camera_presets(std::string* error) {
    const auto config_path = locate_chronon_toml();
    if (!config_path) {
        if (error) {
            *error = "chronon.toml not found";
        }
        return 0;
    }

    try {
        const auto config = toml::parse_file(config_path->string());
        const auto* presets = config["preset"].as_table();
        return presets ? presets->size() : 0U;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return 0;
    }
}

std::optional<CameraVideoPreset> load_camera_preset(const std::string& preset_name,
                                                    std::filesystem::path* source_path,
                                                    std::string* error) {
    const auto config_path = locate_chronon_toml();
    if (!config_path) {
        if (error) {
            *error = "chronon.toml not found";
        }
        return std::nullopt;
    }

    try {
        const auto config = toml::parse_file(config_path->string());
        const auto* presets = config["preset"].as_table();
        if (!presets) {
            if (error) {
                *error = "missing [preset] table in chronon.toml";
            }
            return std::nullopt;
        }

        const auto* node = presets->get(preset_name);
        const auto* tbl = node ? node->as_table() : nullptr;
        if (!tbl) {
            if (error) {
                *error = "missing preset '" + preset_name + "'";
            }
            return std::nullopt;
        }

        if (source_path) {
            *source_path = *config_path;
        }

        const auto base_dir = config_path->parent_path();
        CameraVideoPreset preset;
        preset.axis = read_optional<std::string>(*tbl, "axis");
        preset.reference_image = read_optional<std::string>(*tbl, "reference_image");
        if (!preset.reference_image) {
            preset.reference_image = read_optional<std::string>(*tbl, "reference");
        }
        if (preset.reference_image) {
            *preset.reference_image = resolve_path(base_dir, *preset.reference_image);
        }
        preset.output = read_optional<std::string>(*tbl, "output");
        if (preset.output) {
            *preset.output = resolve_path(base_dir, *preset.output);
        }
        preset.start = transform_optional(read_optional<chronon3d::i64>(*tbl, "start"), [](chronon3d::i64 value) {
            return static_cast<Frame>(value);
        });
        preset.end = transform_optional(read_optional<chronon3d::i64>(*tbl, "end"), [](chronon3d::i64 value) {
            return static_cast<Frame>(value);
        });
        preset.width = transform_optional(read_optional<chronon3d::i64>(*tbl, "width"), [](chronon3d::i64 value) {
            return static_cast<int>(value);
        });
        preset.height = transform_optional(read_optional<chronon3d::i64>(*tbl, "height"), [](chronon3d::i64 value) {
            return static_cast<int>(value);
        });
        preset.fps = transform_optional(read_optional<chronon3d::i64>(*tbl, "fps"), [](chronon3d::i64 value) {
            return static_cast<int>(value);
        });
        preset.crf = transform_optional(read_optional<chronon3d::i64>(*tbl, "crf"), [](chronon3d::i64 value) {
            return static_cast<int>(value);
        });
        preset.codec = read_optional<std::string>(*tbl, "codec");
        preset.encode_preset = read_optional<std::string>(*tbl, "encode_preset");
        if (!preset.encode_preset) {
            preset.encode_preset = read_optional<std::string>(*tbl, "preset");
        }
        preset.use_modular_graph = read_optional<bool>(*tbl, "graph");
        if (!preset.use_modular_graph) {
            preset.use_modular_graph = read_optional<bool>(*tbl, "use_modular_graph");
        }
        preset.motion_blur = read_optional<bool>(*tbl, "motion_blur");
        preset.motion_blur_samples = transform_optional(
            read_optional<chronon3d::i64>(*tbl, "motion_blur_samples"),
            [](chronon3d::i64 value) { return static_cast<int>(value); });
        preset.shutter_angle = transform_optional(
            read_optional<chronon3d::f64>(*tbl, "shutter_angle"),
            [](chronon3d::f64 value) { return static_cast<float>(value); });
        preset.ssaa = transform_optional(
            read_optional<chronon3d::f64>(*tbl, "ssaa"),
            [](chronon3d::f64 value) { return static_cast<float>(value); });
        preset.pose = parse_motion_pose(*tbl);
        preset.primary = parse_motion_primary(*tbl);
        preset.idle = parse_motion_idle(*tbl);
        return preset;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return std::nullopt;
    }
}

void apply_preset_to_args(const CameraVideoPreset& p, VideoCameraArgs& t) {
    if (p.axis)                t.axis                 = *p.axis;
    if (p.reference_image)     t.reference_image      = *p.reference_image;
    if (p.output)              t.output               = *p.output;
    if (p.start)               t.start                = *p.start;
    if (p.end)                 t.end                  = *p.end;
    if (p.width)               t.width                = *p.width;
    if (p.height)              t.height               = *p.height;
    if (p.fps)                 t.fps                  = *p.fps;
    if (p.crf)                 t.crf                  = *p.crf;
    if (p.codec)               t.codec                = *p.codec;
    if (p.encode_preset)       t.encode_preset        = *p.encode_preset;
    if (p.use_modular_graph)   t.use_modular_graph    = *p.use_modular_graph;
    if (p.motion_blur)         t.motion_blur          = *p.motion_blur;
    if (p.motion_blur_samples) t.motion_blur_samples  = *p.motion_blur_samples;
    if (p.shutter_angle)       t.shutter_angle        = *p.shutter_angle;
    if (p.ssaa)                t.ssaa                 = *p.ssaa;
}

} // namespace cli
} // namespace chronon3d
