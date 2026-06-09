#pragma once

#include <chronon3d/description/scene_description.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::specscene {

// Chronon3d specscene format:
// - TOML document
// - top-level scene fields: name, width, height, fps, duration
// - optional [render_camera] table for the 3D renderer camera
// - one or more [[layer]] tables

struct SpecSceneDocument {
    SceneDescription scene;
    Camera render_camera{};
    bool has_render_camera{false};
};

[[nodiscard]] bool is_specscene_file(const std::filesystem::path& path);

[[nodiscard]] std::optional<SpecSceneDocument> load_file(
    const std::filesystem::path& path,
    std::vector<std::string>* diagnostics = nullptr);

} // namespace chronon3d::specscene
