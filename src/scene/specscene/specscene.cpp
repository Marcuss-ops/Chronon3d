#include <chronon3d/specscene/model/specscene.hpp>
#include <chronon3d/scene/model/core/depth_role.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/core/string_utils.hpp>
#include "../parser/specscene_parsers.hpp"
#include <fmt/format.h>
#include <filesystem>

namespace chronon3d::specscene {

using chronon3d::lower_copy;

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

} // namespace chronon3d::specscene
