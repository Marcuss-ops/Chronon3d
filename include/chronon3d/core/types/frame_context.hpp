#pragma once

#include <chronon3d/core/types/time.hpp>
#include <chronon3d/runtime/render_runtime.hpp>  // WP-9 PR 9.0 — full def for runtime->font_engine()
#include <algorithm>
#include <memory_resource>
#include <string>

namespace chronon3d {
// (RenderRuntime is now fully included — forward decl removed)
class AssetRegistry;  // forward declaration for migration path
class FontEngine;     // TICKET-A4 follow-up — codex/agent2-font-bind-fixes:
                      // WP-8 PR 8.0 strict binding means composition
                      // lambdas MUST see a FontEngine in ctx (engine is
                      // injected by the render pipeline).  Forward-declare
                      // here so the existing include budget stays constant.

struct FrameContext {
    // Canonical temporal coordinate. All frame/time derived accessors below
    // read from this value; `frame` is kept as a convenience mirror of
    // sample_time.integral_frame() for code that still needs an integer id.
    SampleTime sample_time{};
    Frame frame{0};
    Frame duration{0};
    FrameRate frame_rate{30, 1};
    i32 width{1920};
    i32 height{1080};
    std::string assets_root;
    AssetRegistry* assets{nullptr};  // PR 2 — migration path: prefer this over TLS AssetRegistry API
    std::pmr::memory_resource* resource{std::pmr::get_default_resource()};
    // ── WP-9 PR 9.0 — Runtime accessor threaded into composition ctx ─
    // P1-16: the canonical access path is `ctx.runtime->font_engine()`.
    // The legacy `ctx.font_engine` direct field is kept for backward
    // compatibility during the migration; it is populated automatically
    // by Composition::evaluate_double when a runtime is supplied, and
    // callers may still set it directly in hand-built FrameContext.
    // New code should prefer `ctx.runtime->font_engine()`.
    FontEngine* font_engine{nullptr};
    const chronon3d::runtime::RenderRuntime* runtime{nullptr};

    [[nodiscard]] double fps() const { return sample_time.fps(); }

    // Effective time: integral frame + fractional offset.
    [[nodiscard]] double effective_frame() const {
        return sample_time.frame;
    }

    [[nodiscard]] TimeSeconds seconds() const {
        return sample_time.seconds();
    }

    [[nodiscard]] double progress() const {
        if (duration <= 0) return 0.0;
        return std::clamp(
            sample_time.frame / static_cast<double>(duration),
            0.0,
            1.0
        );
    }

    [[nodiscard]] bool is_first_frame() const { return frame == 0; }
    [[nodiscard]] bool is_last_frame() const { return duration > 0 && frame >= duration - 1; }

};

} // namespace chronon3d
