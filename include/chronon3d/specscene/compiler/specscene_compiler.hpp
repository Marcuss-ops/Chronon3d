#pragma once

#include <chronon3d/specscene/model/specscene.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::specscene {

/// Compile a SpecSceneDocument into a renderable Composition.
///
/// Converts the parsed scene description into a timeline-based Composition
/// ready for rendering.  Returns std::nullopt and fills diagnostics on failure.
[[nodiscard]] std::optional<Composition> compile_document(
    const SpecSceneDocument& doc,
    std::vector<std::string>* diagnostics = nullptr);

/// Compile a .specscene / .toml file directly into a Composition.
///
/// Equivalent to load_file() + compile_document().
[[nodiscard]] std::optional<Composition> compile_file(
    const std::filesystem::path& path,
    std::vector<std::string>* diagnostics = nullptr);

} // namespace chronon3d::specscene
