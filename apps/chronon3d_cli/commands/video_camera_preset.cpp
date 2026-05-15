#include "video_camera_preset.hpp"

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
        return preset;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return std::nullopt;
    }
}

} // namespace cli
} // namespace chronon3d
