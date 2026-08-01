#pragma once

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
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
/// Composition remains the current runtime execution value, while the plan
/// metadata travels with it so callers do not have to keep a second decoded
/// RenderPlan alive.  Asset byte hashes and a prepared asset manifest are
/// intentionally a follow-up: the resolver is already the only path used
/// during preparation, and this value is the stable insertion point for that
/// manifest.
struct PreparedRenderPlan {
    std::shared_ptr<const Composition> composition;
    chronon3d::assets::PreparedAssetManifest assets;
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
