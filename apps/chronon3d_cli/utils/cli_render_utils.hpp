#pragma once

#include <chronon3d/core/composition_registry.hpp>
#include <chronon3d/core/frame.hpp>
#include <chronon3d/renderer/software/render_settings.hpp>
#include <chronon3d/renderer/software/software_renderer.hpp>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d {
namespace cli {

/// Result of resolving a composition from a specscene file or registry id.
struct ResolvedComposition {
    std::shared_ptr<Composition> comp;
    bool from_specscene{false};
    std::vector<std::string> diagnostics;

    /// True if the composition was successfully resolved.
    explicit operator bool() const { return comp != nullptr; }
};

/// Resolve a composition from a specscene file path or a registry composition id.
/// Logs errors via spdlog on failure — check the result with operator bool().
ResolvedComposition resolve_composition(const CompositionRegistry& registry,
                                        const std::string& comp_id);

/// Create and configure a SoftwareRenderer from the given settings.
std::shared_ptr<SoftwareRenderer> create_renderer(
    const CompositionRegistry& registry,
    const RenderSettings& settings);

} // namespace cli
} // namespace chronon3d
