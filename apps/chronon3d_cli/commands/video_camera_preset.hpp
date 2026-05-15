#pragma once

#include <chronon3d/core/frame.hpp>
#include <chronon3d/math/vec3.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d {
namespace cli {

struct CameraVideoPreset {
    struct Pose {
        std::optional<Vec3> position;
        std::optional<Vec3> rotation;
        std::optional<float> zoom;
    };

    struct Primary {
        std::optional<Pose> from;
        std::optional<Pose> to;
        std::optional<Frame> duration;
        std::optional<std::string> easing;
        std::optional<bool> enabled;
    };

    struct Idle {
        std::optional<bool> enabled;
        std::optional<Vec3> position_amplitude;
        std::optional<Vec3> rotation_amplitude_deg;
        std::optional<float> zoom_amplitude;
        std::optional<float> frequency_hz;
        std::optional<float> phase_offset;
        std::optional<bool> base_on_final;
    };

    std::optional<std::string> axis;
    std::optional<std::string> reference_image;
    std::optional<std::string> output;
    std::optional<Frame> start;
    std::optional<Frame> end;
    std::optional<int> width;
    std::optional<int> height;
    std::optional<int> fps;
    std::optional<int> crf;
    std::optional<std::string> codec;
    std::optional<std::string> encode_preset;
    std::optional<bool> use_modular_graph;
    std::optional<bool> motion_blur;
    std::optional<int> motion_blur_samples;
    std::optional<float> shutter_angle;
    std::optional<float> ssaa;
    std::optional<Pose> pose;
    std::optional<Primary> primary;
    std::optional<Idle> idle;
};

std::optional<std::filesystem::path> locate_chronon_toml();
std::size_t count_camera_presets(std::string* error = nullptr);
std::optional<CameraVideoPreset> load_camera_preset(const std::string& preset_name,
                                                    std::filesystem::path* source_path = nullptr,
                                                    std::string* error = nullptr);

} // namespace cli
} // namespace chronon3d
