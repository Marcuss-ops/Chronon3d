#pragma once

// ═════════════════════════════════════════════════════════════════════════════
//  SequenceDirector — beat-based timing reference for motion_studio.
//
//  A beat is a named Frame offset.  Composition authors register them up
//  front and then read frames from the director anywhere they need a
//  start-time for a layer animation.
//
//    director.beat("hero_in",    Frame{0});
//    director.beat("cards_in",   Frame{10});
//    director.beat("typing_done", Frame{120});
//
//    l.position(...)            // optional static
//    l.soft_pop( director.frame("hero_in") );
//    l.opacity_anim().key( director.frame("cards_in"), 0.0f, … )
//                     .key( director.frame("cards_in") + 20, 1.0f, … );
//
//  `.after(beat, offset)` returns the frame for a beat plus an offset and
//  is convenient inside chained expressions.  `.with(beat)` is a fluent
//  syntactic helper for parallel groups — semantically identical to using
//  the bare beat frame: the scheduling side-table records the wiring so
//  future code (auto-stagger, telemetry-driven timings) can introspect it,
//  but no engine mutation happens here.
//
//  Header-only by design: the data structure is a small map + vector and
//  the methods are one or two lines each.
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/frame.hpp>

#include <map>
#include <string>
#include <vector>

namespace chronon3d::motion_studio {

class SequenceDirector {
public:
    struct ScheduledPlay {
        std::string layer_id;
        std::string preset;
        Frame       start;
    };

    // ── Registration ──────────────────────────────────────────────────

    /// Register a beat.  Overwrites any previous frame for the same name.
    SequenceDirector& beat(const std::string& name, Frame frame);
    SequenceDirector& beat(const std::string& name, int frame_int);

    /// Lookup.
    [[nodiscard]] Frame frame(const std::string& name) const;
    [[nodiscard]] bool  has(const std::string& name)   const;
    [[nodiscard]] std::size_t beat_count() const { return beats_.size(); }

    /// `frame(beat_name) + offset`.  Throws via Vector assert-style if the
    /// beat is missing; callers should pre-register.
    [[nodiscard]] Frame relative_frame(const std::string& base, Frame offset) const;

    /// Float-seconds helper.  Uses 30 fps by default for fps.
    [[nodiscard]] Frame seconds_from(const std::string& base, f32 seconds, f32 fps = 30.0f) const;

    // ── Scheduling side-table (parallel groups, future-aware) ─────────

    /// Schedule a preset play to start at the most-recently-set "with" beat.
    /// Returns *this for chaining.
    SequenceDirector& play(const std::string& layer_id, const std::string& preset) {
        Frame start = 0;
        if (with_beat_ && beats_.count(*with_beat_)) {
            start = beats_[*with_beat_] + pending_offset_;
        }
        scheduled_.push_back({layer_id, preset, start});
        pending_offset_ = 0; // consume the offset
        return *this;
    }

    /// Set the active "with" beat for subsequent `.play(...)` calls.
    /// Equivalent to picking the start frame for parallel plays.
    SequenceDirector& with(const std::string& beat) {
        with_beat_ = beat;
        pending_offset_ = 0;
        return *this;
    }

    /// Add an offset to the active "with" beat for the *next* play only.
    /// `with("y").after_(15)` plays at frame(y)+15 once, then resets.
    SequenceDirector& after_(Frame offset) {
        pending_offset_ = offset;
        return *this;
    }

    [[nodiscard]] const std::vector<ScheduledPlay>& scheduled() const { return scheduled_; }

    /// Clear side-table (beats kept).
    void clear_schedule() { scheduled_.clear(); with_beat_.reset(); pending_offset_ = 0; }

private:
    std::map<std::string, Frame> beats_;
    std::vector<ScheduledPlay>    scheduled_;
    std::optional<std::string>    with_beat_;
    Frame                          pending_offset_{0};
};

} // namespace chronon3d::motion_studio
