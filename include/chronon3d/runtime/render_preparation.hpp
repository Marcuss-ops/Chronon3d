#pragma once

#include <chronon3d/assets/asset_preflight_resolver.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#include <chronon3d/runtime/resource_preparation.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>

#include <optional>
#include <string>

namespace chronon3d {

class SoftwareRenderer;

namespace runtime {

struct RenderPreparationOptions {
    PreflightMode preflight_mode{PreflightMode::FullComposition};
    PreparationOptions resources{};
    bool warmup_renderer{true};
    RendererWarmupOptions warmup{};
    Frame reference_frame{0};
};

struct RenderPreparationResult {
    AssetPreflightResult preflight{};
    std::optional<PreparationError> preparation_error{};
    std::optional<PreparedAssets> prepared_assets{};
    RendererWarmupResult warmup{};
    bool warmup_performed{false};

    [[nodiscard]] bool ok() const noexcept {
        return preflight.ok() && !preparation_error.has_value();
    }

    [[nodiscard]] std::string diagnostic() const;
};

/// Prepare every resource required by a composition before encoding starts.
///
/// This is a synchronous barrier: composition evaluation and asset checks are
/// completed before optional renderer warmup. It owns no resolver, cache, or
/// mutable render state; all services remain owned by the renderer runtime.
[[nodiscard]] RenderPreparationResult prepare_render(
    SoftwareRenderer* renderer,
    const Composition& composition,
    const RenderPreparationOptions& options = {});

/// Prepare an immutable compiled composition without re-entering the
/// authoring registry or creating a second runtime composition.
[[nodiscard]] RenderPreparationResult prepare_render(
    SoftwareRenderer* renderer,
    const CompiledComposition& compiled,
    const RenderPreparationOptions& options = {});

} // namespace runtime
} // namespace chronon3d
