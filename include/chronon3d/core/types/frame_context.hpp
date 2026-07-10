#pragma once

#include <chronon3d/core/types/time.hpp>
#include <chronon3d/runtime/render_runtime.hpp>  // WP-9 PR 9.0 — full def for font_engine_or_null()
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
    Frame frame{0};
    Frame local_frame{0};    // alias/local offset for sequences
    f32   frame_time{0.0f};  // fractional frame for motion blur subsampling (0.0 = integral frame)
    Frame duration{0};
    FrameRate frame_rate{30, 1};
    i32 width{1920};
    i32 height{1080};
    std::string assets_root;
    AssetRegistry* assets{nullptr};  // PR 2 — migration path: prefer this over TLS AssetRegistry API
    std::pmr::memory_resource* resource{std::pmr::get_default_resource()};
    FontEngine* font_engine{nullptr};  // codex/agent2-font-bind-fixes:
                                       // render pipeline populates from
                                       // SoftwareRenderer::font_engine().
                                       // Composition lambdas may bind this
                                       // onto the SceneBuilder via
                                       // SceneBuilder(ctx) ctor OR explicit
                                       // s.font_engine(ctx.font_engine).
                                       // nullptr = legacy path (engine must
                                       // be supplied via SceneBuilder-level
                                       // setter); backwards-compatible with
                                       // any tests that build FrameContext
                                       // by hand without an engine.
                                       //
                                       // WP-9 PR 9.0 (deprecated, prefer):
                                       // composition lambdas should now
                                       // read `ctx.runtime->font_engine()`
                                       // when available and fall back to
                                       // this direct field otherwise.

    // ── WP-9 PR 9.0 — Runtime accessor threaded into composition ctx ─
    /// Non-owning pointer to the per-runtime RenderRuntime.  Render-time
    /// injection point: Composition's evaluate(Frame, f32, RenderRuntime&,
    /// FontEngine*, ...) overload sets ctx.runtime = &runtime.  The
    /// render pipeline may also populate this when SoftwareRenderer
    /// constructs the per-frame FrameContext for graph execution.
    ///
    /// Composition lambdas should prefer `ctx.runtime->font_engine()`
    /// over `ctx.font_engine` when ctx.runtime is non-null.  Null-safe
    /// default keeps legacy callers (e.g. hand-built FrameContext in
    /// tests) compiling without a runtime.
    const chronon3d::runtime::RenderRuntime* runtime{nullptr};

    [[nodiscard]] double fps() const { return frame_rate.fps(); }

    // Effective time: integral frame + fractional offset.
    [[nodiscard]] double effective_frame() const {
        return static_cast<double>(frame) + static_cast<double>(frame_time);
    }

    [[nodiscard]] TimeSeconds seconds() const {
        return effective_frame()
             * static_cast<double>(frame_rate.denominator)
             / static_cast<double>(frame_rate.numerator);
    }

    [[nodiscard]] double progress() const {
        if (duration <= 0) return 0.0;
        return std::clamp(
            effective_frame() / static_cast<double>(duration),
            0.0,
            1.0
        );
    }

    [[nodiscard]] bool is_first_frame() const { return frame == 0; }
    [[nodiscard]] bool is_last_frame() const { return duration > 0 && frame >= duration - 1; }

    // ── WP-9 PR 9.0 — Runtime-aware FontEngine accessor helper ────────
    /// AGENTS.md §5 anti-duplication helper.  Returns the
    /// runtime-supplied FontEngine when ctx.runtime is wired (preferred
    /// path per ADR-020 migration), or the legacy direct ctx.font_engine
    /// field when no runtime is available.  Null-safe default allows
    /// hand-built FrameContext (e.g. tests, install_consumer examples)
    /// to call this without allocating a RenderRuntime.
    ///
    /// Composition lambdas should call this method directly:
    ///     `s.font_engine(ctx.font_engine_or_null())` ; or
    ///     `if (auto* engine = ctx.font_engine_or_null()) { ... }`
    /// instead of repeating the `runtime ? runtime->font_engine() : font_engine`
    /// ternary at every call site (6 sites consolidated by this helper).
    [[nodiscard]] FontEngine* font_engine_or_null() const noexcept {
        return runtime ? runtime->font_engine() : font_engine;
    }
};

} // namespace chronon3d
