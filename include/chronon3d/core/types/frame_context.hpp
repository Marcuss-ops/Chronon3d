#pragma once

#include <chronon3d/core/types/time.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <algorithm>
#include <cmath>
#include <memory_resource>
#include <optional>
#include <string>

namespace chronon3d {

namespace runtime { class RenderRuntime; }
namespace registry { class ShapeRegistry; }
class AssetRegistry;
class FontEngine;

/// Parameters for constructing a FrameContext. `frame_time` is appended to the
/// legacy aggregate layout so existing positional/designated construction stays
/// source-compatible while exact media time is available to new callers.
struct FrameContextParams {
    SampleTime global_time{};
    std::optional<SampleTime> local_time{};
    Frame duration{0};

    i32 width{1920};
    i32 height{1080};
    std::string assets_root{};
    AssetRegistry* assets{nullptr};
    std::pmr::memory_resource* resource{std::pmr::get_default_resource()};
    registry::ShapeRegistry* shape_registry{nullptr};
    FontEngine* font_engine{nullptr};
    const chronon3d::runtime::RenderRuntime* runtime{nullptr};

    std::optional<FrameTimeContext> frame_time{};
};

/// Evaluation context for a single frame. Exact presentation time is carried
/// independently from SampleTime so original media PTS/time-base information
/// is not collapsed into floating-point seconds. DTS intentionally has no
/// representation here.
class FrameContext {
public:
    i32 width{1920};
    i32 height{1080};
    std::string assets_root{};
    AssetRegistry* assets{nullptr};
    std::pmr::memory_resource* resource{std::pmr::get_default_resource()};
    registry::ShapeRegistry* shape_registry{nullptr};
    FontEngine* font_engine{nullptr};
    const chronon3d::runtime::RenderRuntime* runtime{nullptr};

    [[nodiscard]] SampleTime global_time() const noexcept { return global_time_; }
    [[nodiscard]] SampleTime local_time() const noexcept { return local_time_; }
    [[nodiscard]] Frame duration() const noexcept { return duration_; }

    [[nodiscard]] const FrameTimeContext& frame_time() const noexcept { return frame_time_; }
    [[nodiscard]] RationalTime presentation_time() const noexcept { return frame_time_.presentation_time; }
    [[nodiscard]] RationalTime presentation_duration() const noexcept { return frame_time_.duration; }
    [[nodiscard]] i64 timeline_tick() const noexcept { return frame_time_.timeline_tick; }
    [[nodiscard]] bool discontinuity() const noexcept { return frame_time_.discontinuity; }

    [[nodiscard]] Frame frame() const noexcept { return local_time_.integral_frame(); }
    [[nodiscard]] double frame_fraction() const noexcept { return local_time_.fraction(); }
    [[nodiscard]] FrameRate frame_rate() const noexcept { return local_time_.frame_rate; }

    [[nodiscard]] double fps() const noexcept { return local_time_.fps(); }
    [[nodiscard]] double effective_frame() const noexcept { return local_time_.frame; }
    [[nodiscard]] TimeSeconds seconds() const { return local_time_.seconds(); }

    [[nodiscard]] double progress() const {
        if (duration_ <= 0) return 0.0;
        return std::clamp(local_time_.frame / static_cast<double>(duration_), 0.0, 1.0);
    }

    [[nodiscard]] bool is_first_frame() const { return frame() == 0; }
    [[nodiscard]] bool is_last_frame() const { return duration_ > 0 && frame() >= duration_ - 1; }

    [[nodiscard]] FrameContext with_global_time(SampleTime new_global) const {
        FrameContext dup = *this;
        dup.global_time_ = new_global;
        dup.local_time_ = new_global;
        dup.frame_time_ = frame_time_from_sample(new_global);
        return dup;
    }

    [[nodiscard]] FrameContext with_frame(Frame f) const {
        return with_global_time(SampleTime::from_frame_int(f, local_time_.frame_rate));
    }

    [[nodiscard]] FrameContext with_frame_rate(FrameRate rate) const {
        return with_global_time(SampleTime::from_frame(local_time_.frame, rate));
    }

    [[nodiscard]] FrameContext with_duration(Frame d) const {
        FrameContext dup = *this;
        dup.duration_ = d;
        return dup;
    }

    [[nodiscard]] FrameContext with_local_time(SampleTime new_local, Frame new_duration) const {
        FrameContext dup = *this;
        dup.local_time_ = new_local;
        dup.duration_ = new_duration;
        return dup;
    }

private:
    friend FrameContext make_frame_context(const FrameContextParams&);

    [[nodiscard]] static FrameTimeContext frame_time_from_sample(SampleTime time) {
        constexpr i64 kSubframeTicks = 65536;
        const i64 subframe_value = static_cast<i64>(
            std::llround(time.frame * static_cast<double>(kSubframeTicks)));
        const RationalTime pts{
            subframe_value,
            Rational{
                static_cast<i64>(time.frame_rate.denominator),
                static_cast<i64>(time.frame_rate.numerator) * kSubframeTicks}}
        ;
        return FrameTimeContext{
            .output_frame = time.integral_frame(),
            .presentation_time = pts,
            .duration = RationalTime{
                1,
                Rational{time.frame_rate.denominator, time.frame_rate.numerator}},
            .timeline_tick = subframe_value,
            .discontinuity = false,
        };
    }

    SampleTime global_time_{};
    SampleTime local_time_{};
    Frame duration_{0};
    FrameTimeContext frame_time_{};
};

[[nodiscard]] inline FrameContext make_frame_context(const FrameContextParams& p) {
    FrameContext ctx;
    ctx.width = p.width;
    ctx.height = p.height;
    ctx.assets_root = p.assets_root;
    ctx.assets = p.assets;
    ctx.resource = p.resource;
    ctx.shape_registry = p.shape_registry;
    ctx.font_engine = p.font_engine;
    ctx.runtime = p.runtime;

    ctx.global_time_ = p.global_time;
    ctx.local_time_ = p.local_time.value_or(p.global_time);
    ctx.duration_ = p.duration;
    ctx.frame_time_ = p.frame_time.value_or(FrameContext::frame_time_from_sample(p.global_time));
    return ctx;
}

} // namespace chronon3d
