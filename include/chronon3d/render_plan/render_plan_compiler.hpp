#pragma once

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::render_plan {

struct RenderJobFingerprint {
    chronon3d::assets::ContentDigest content_digest{};
    chronon3d::assets::ContentDigest request_digest{};
};

/// Immutable output of render-plan preparation.
///
/// `compiled_composition` is the canonical execution value.  The
/// `composition` member is a temporary source-compatibility view for the
/// legacy CLI/runtime boundary; it is built from the same
/// CompositionDefinition and has no independent compilation path.
struct PreparedRenderPlan {
    CompiledComposition compiled_composition;

    // Transitional adapter.  New consumers must use compiled_composition.
    std::shared_ptr<const Composition> composition;
    chronon3d::assets::PreparedAssetManifest assets;
    chronon3d::assets::PreparedAssetStore resources;
    RenderJobFingerprint fingerprint;
    std::string job_id;
    CanvasSpec canvas;
    OutputSpec output;
    std::vector<AudioTrackPlan> audio_tracks;
};

Result<PreparedRenderPlan, PlanDecodeError>
compile_render_plan(const RenderPlan& plan,
                    chronon3d::assets::AssetResolver& resolver);

}  // namespace chronon3d::render_plan
