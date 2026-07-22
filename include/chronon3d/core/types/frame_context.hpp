#pragma once

#include <chronon3d/core/types/time.hpp>
#include <chronon3d/runtime/render_runtime.hpp>  // WP-9 PR 9.0 — full def for runtime->font_engine()
#include <algorithm>
#include <memory_resource>
#include <optional>
#include <string>

namespace chronon3d {

// (RenderRuntime is now fully included — forward decl removed)
class AssetRegistry;  // forward declaration for migration path
class FontEngine;     // TICKET-A4 follow-up — codex/agent2-font-bind-fixes:
                      // WP-8 PR 8.0 strict binding means composition
                      // lambdas MUST see a FontEngine in ctx (engine is
                      // injected by the render pipeline).  Forward-declare
                      // here so the existing include budget stays constant.

/// Parameters for constructing a FrameContext. Keeps designated-initializer
/// syntax available for callers while allowing FrameContext itself to have
/// private temporal fields.
struct FrameContextParams {
    SampleTime global_time{};
    std::optional<SampleTime> local_time{};
    Frame duration{0};

    i32 width{1920};
    i32 height{1080};
    std::string assets_root{};
    AssetRegistry* assets{nullptr};
    std::pmr::memory_resource* resource{std::pmr::get_default_resource()};
    FontEngine* font_engine{nullptr};
    const chronon3d::runtime::RenderRuntime* runtime{nullptr};
};

/// Evaluation context for a single frame.
///
/// Temporal state is private and accessed through canonical accessors.
/// `global_time()` is the timeline coordinate in absolute terms;
/// `local_time()` is the coordinate after applying sequence/layer remapping.
/// They coincide whenever no remapping is in effect.
class FrameContext {
public:
    // ── Non-temporal public data ───────────────────────────────────────────
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

    // ── Temporal accessors ───────────────────────────────────────────────
    [[nodiscard]] SampleTime global_time() const noexcept { return global_time_; }
    [[nodiscard]] SampleTime local_time() const noexcept { return local_time_; }

    [[nodiscard]] Frame duration() const noexcept { return duration_; }

    [[nodiscard]] Frame frame() const noexcept { return local_time_.integral_frame(); }
    [[nodiscard]] double frame_fraction() const noexcept { return local_time_.fraction(); }
    [[nodiscard]] FrameRate frame_rate() const noexcept { return local_time_.frame_rate; }

    [[nodiscard]] double fps() const noexcept { return local_time_.fps(); }
    [[nodiscard]] double effective_frame() const noexcept { return local_time_.frame; }
    [[nodiscard]] TimeSeconds seconds() const { return local_time_.seconds(); }

    [[nodiscard]] double progress() const {
        if (duration_ <= 0) return 0.0;
        return std::clamp(
            local_time_.frame / static_cast<double>(duration_),
            0.0,
            1.0
        );
    }

    [[nodiscard]] bool is_first_frame() const { return frame() == 0; }
    [[nodiscard]] bool is_last_frame() const { return duration_ > 0 && frame() >= duration_ - 1; }

    // ── Structural helper ────────────────────────────────────────────────
    // Returns a copy of this context with a new local_time and duration.
    // Useful for sequence/layer remapping without rebuilding non-temporal
    // state.
    [[nodiscard]] FrameContext with_local_time(SampleTime new_local, Frame new_duration) const {
        FrameContext dup = *this;
        dup.local_time_ = new_local;
        dup.duration_ = new_duration;
        return dup;
    }

private:
    friend FrameContext make_frame_context(const FrameContextParams&);

    SampleTime global_time_{};
    SampleTime local_time_{};
    Frame duration_{0};
};

/// Centralized factory for FrameContext. All production code should construct
/// FrameContext through this function so that temporal invariants are kept in
/// a single place.
[[nodiscard]] inline FrameContext make_frame_context(const FrameContextParams& p) {
    FrameContext ctx;
    ctx.width = p.width;
    ctx.height = p.height;
    ctx.assets_root = p.assets_root;
    ctx.assets = p.assets;
    ctx.resource = p.resource;
    ctx.font_engine = p.font_engine;
    ctx.runtime = p.runtime;

    ctx.global_time_ = p.global_time;
    ctx.local_time_ = p.local_time.value_or(p.global_time);
    ctx.duration_ = p.duration;
    return ctx;
}

} // namespace chronon3d
