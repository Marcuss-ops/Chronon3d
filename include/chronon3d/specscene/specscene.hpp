#pragma once

#include <chronon3d/description/scene_description.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::specscene {

// Chronon3d specscene format:
// - TOML document
// - top-level scene fields: name, width, height, fps_numerator, fps_denominator, duration
// - optional [camera] table for the 2.5D scene camera
// - optional [render_camera] table for the 3D renderer camera
// - one or more [[layer]] tables
// - each layer may contain a visuals = [ { ... }, ... ] array of inline tables

struct SpecSceneDocument {
    SceneDescription scene;
    Camera render_camera{};
    bool has_render_camera{false};
};

[[nodiscard]] bool is_specscene_file(const std::filesystem::path& path);

[[nodiscard]] std::optional<SpecSceneDocument> load_file(
    const std::filesystem::path& path,
    std::vector<std::string>* diagnostics = nullptr);

[[nodiscard]] std::optional<Composition> compile_file(
    const std::filesystem::path& path,
    std::vector<std::string>* diagnostics = nullptr);

} // namespace chronon3d::specscene
