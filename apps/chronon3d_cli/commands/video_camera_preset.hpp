#pragma once

#include <chronon3d/core/frame.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d {
namespace cli {

struct CameraVideoPreset {
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
};

std::optional<std::filesystem::path> locate_chronon_toml();
std::size_t count_camera_presets(std::string* error = nullptr);
std::optional<CameraVideoPreset> load_camera_preset(const std::string& preset_name,
                                                    std::filesystem::path* source_path = nullptr,
                                                    std::string* error = nullptr);

} // namespace cli
} // namespace chronon3d
