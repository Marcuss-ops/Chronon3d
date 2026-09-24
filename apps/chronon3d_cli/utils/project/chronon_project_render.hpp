#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::cli {

/// If project_root contains chronon.json, run its built standalone renderer.
/// Returns nullopt when no project manifest is present; otherwise returns the
/// subprocess exit code. Errors are returned through diagnostic.
[[nodiscard]] std::optional<int> dispatch_project_render(
    const std::filesystem::path& project_root,
    std::string_view composition,
    std::string_view output,
    std::string& diagnostic);

} // namespace chronon3d::cli
