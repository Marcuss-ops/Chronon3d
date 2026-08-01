#pragma once

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::render_plan {

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
    std::string job_id;
    CanvasSpec canvas;
    OutputSpec output;
    std::vector<AudioTrackPlan> audio_tracks;
    std::uint64_t content_fingerprint{0};
};

Result<PreparedRenderPlan, PlanDecodeError>
compile_render_plan(const RenderPlan& plan,
                    const chronon3d::assets::AssetResolver& resolver);

}  // namespace chronon3d::render_plan
