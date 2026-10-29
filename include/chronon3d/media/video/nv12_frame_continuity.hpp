#pragma once

// Destination-continuity tracker for the RGBA surface -> NV12 encoder
// boundary (media/video/nv12_region_policy.hpp is the launch authority; this
// is the proof authority it demands).
//
// The boundary may convert only the changed sub-regions of a frame when — and
// only when — every pixel it does NOT convert already holds this frame's value
// in the DESTINATION. A pooled encoder frame is handed out by the hwframe pool
// with no such guarantee, so the guarantee is established by bookkeeping
// instead: every conversion into a physical pool buffer is recorded here, and
// a buffer that last held stream frame M can receive frame N through the union
// of the damage of frames M+1..N. That union is exactly the set of pixels in
// which frame N can differ from frame M; converting it makes the buffer hold
// frame N in full.
//
// Fail-closed rules, all covered by tests/video/test_nv12_frame_continuity.cpp:
//   * a buffer with no recorded history is converted whole;
//   * a gap in the damage log (any stream frame never noted) is unknown
//     damage, which widens every spanning plan to the whole frame;
//   * `note_frame_damage` with an empty set means the producer could not bound
//     the change and is recorded as whole-frame damage;
//   * `note_unchanged_frame` is the only "nothing changed" assertion and is
//     emitted for DuplicateFrame packets (P1), which never touch the pixel
//     path;
//   * once the accumulated union covers half the canvas or more, the whole
//     frame is converted: one full conversion is cheaper than a wall of
//     micro-region launches, and the result is identical.
//
// Buffer identity is the physical NV12 Y-plane pointer of the pooled frame
// (stable for the pool buffer while it is allocated), NOT the AVFrame object,
// which the encoder recycles across different pool buffers.

#include <chronon3d/math/raster_utils.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace chronon3d::media::video {

/// The conversion this frame may perform into one destination buffer.
struct ContinuityConversionScope {
    /// Regions to convert, in destination pixel space. Empty when the whole
    /// frame must be converted (see Nv12FrameContinuity semantics).
    std::vector<raster::BBox> regions;

    /// True when pixels outside `regions` are proven to already hold this
    /// frame's value. Only then may `regions` be handed to
    /// plan_nv12_conversion(..., /*destination_retains_previous_frame=*/true).
    bool destination_retains_previous_frame{false};

    /// True when the whole frame must be converted.
    bool full_frame{true};
};

class Nv12FrameContinuity {
public:
    /// History depth in stream frames. It must cover the encoder's in-flight
    /// ring (kCudaEncodeRingSlots = 34) with margin, so a recycled pool buffer
    /// is still inside the logged window.
    static constexpr std::size_t kDefaultHistoryDepth = 64;

    /// Region count above which the accumulated union collapses to a single
    /// bounding box (kernel-launch count bound, not a correctness knob).
    static constexpr std::size_t kMaxRegions = 16;

    explicit Nv12FrameContinuity(std::size_t history_depth = kDefaultHistoryDepth)
        : history_depth_(history_depth == 0 ? 1 : history_depth) {}

    /// Records bounded damage for one stream frame: a non-empty region set
    /// that covers every pixel in which this frame differs from the previous
    /// one. An empty set is not "no change" — it is unknown damage and is
    /// recorded as whole-frame (use note_unchanged_frame for no change).
    void note_frame_damage(std::int64_t stream_frame,
                           std::span<const raster::BBox> damage) {
        if (damage.empty()) {
            note(stream_frame, {}, /*whole_frame=*/true);
            return;
        }
        std::vector<raster::BBox> regions(damage.begin(), damage.end());
        note(stream_frame, std::move(regions), /*whole_frame=*/false);
    }

    /// Records a stream frame proven identical to its predecessor (the
    /// DuplicateFrame packet path). No pixel of any buffer changes.
    void note_unchanged_frame(std::int64_t stream_frame) {
        note(stream_frame, {}, /*whole_frame=*/false);
    }

    /// Plans the conversion that writes `stream_frame` into the physical
    /// buffer identified by `buffer_identity`. Returns a whole-frame scope
    /// whenever continuity cannot be proven from the recorded history.
    [[nodiscard]] ContinuityConversionScope plan_conversion(
        const void* buffer_identity, std::int64_t stream_frame,
        std::uint32_t width, std::uint32_t height) const {
        ContinuityConversionScope scope;
        const auto held = buffers_.find(buffer_identity);
        if (held == buffers_.end()) return scope;  // unknown buffer: whole frame

        const std::int64_t held_frame = held->second;
        if (held_frame >= stream_frame) return scope;  // out of order: defensive

        // Walk the damage log from the buffer's held frame forward. Every
        // stream frame in (held_frame, stream_frame] must be logged — a gap is
        // unknown damage, and so is a logged whole-frame entry.
        std::vector<raster::BBox> accumulated;
        bool whole_frame_damage = false;
        for (std::int64_t expected = held_frame + 1; expected <= stream_frame;
             ++expected) {
            const auto entry = damage_log_.find(expected);
            if (entry == damage_log_.end() || entry->second.whole_frame) {
                whole_frame_damage = true;
                break;
            }
            accumulated.insert(accumulated.end(),
                               entry->second.damage.begin(),
                               entry->second.damage.end());
        }
        if (whole_frame_damage) return scope;

        // Efficiency guard: once the union covers half the canvas, convert it
        // all. Same pixels, one launch instead of many.
        const std::int64_t frame_area =
            static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height);
        const auto bbox_area = [](const raster::BBox& region) {
            return static_cast<std::int64_t>(std::max(0, region.x1 - region.x0)) *
                   static_cast<std::int64_t>(std::max(0, region.y1 - region.y0));
        };
        std::int64_t accumulated_area = 0;
        for (const auto& region : accumulated) {
            accumulated_area += bbox_area(region);
        }
        if (accumulated.empty() ||
            (frame_area > 0 && accumulated_area * 2 >= frame_area)) {
            return scope;
        }
        if (accumulated.size() > kMaxRegions) {
            raster::BBox merged = accumulated.front();
            for (const auto& region : accumulated) {
                merged.x0 = std::min(merged.x0, region.x0);
                merged.y0 = std::min(merged.y0, region.y0);
                merged.x1 = std::max(merged.x1, region.x1);
                merged.y1 = std::max(merged.y1, region.y1);
            }
            accumulated.assign(1, merged);
            if (frame_area > 0 && bbox_area(merged) * 2 >= frame_area) {
                return scope;
            }
        }

        scope.regions = std::move(accumulated);
        scope.destination_retains_previous_frame = true;
        scope.full_frame = false;
        return scope;
    }

    /// Publishes that `buffer_identity` now holds `stream_frame`'s pixels.
    /// Call only after the conversion was successfully submitted.
    void commit(const void* buffer_identity, std::int64_t stream_frame) {
        buffers_[buffer_identity] = stream_frame;
    }

    /// Forgets the continuity claim for one buffer (its content is no longer
    /// a stream frame's pixels, e.g. after a failed conversion attempt).
    void invalidate(const void* buffer_identity) {
        buffers_.erase(buffer_identity);
    }

    /// Session reset: all continuity claims are dropped.
    void reset() {
        buffers_.clear();
        damage_log_.clear();
        last_noted_frame_ = std::nullopt;
    }

    /// Bookkeeping view for tests/diagnostics: stream frames held by buffers.
    [[nodiscard]] std::size_t tracked_buffer_count() const noexcept {
        return buffers_.size();
    }

private:
    struct DamageEntry {
        std::vector<raster::BBox> damage;
        bool whole_frame{false};
    };

    void note(std::int64_t stream_frame, std::vector<raster::BBox> damage,
              bool whole_frame) {
        if (last_noted_frame_ && stream_frame <= *last_noted_frame_) {
            // Out-of-order or repeated note: the pipeline is strictly
            // sequential; drop rather than corrupt the ordered log.
            return;
        }
        last_noted_frame_ = stream_frame;
        damage_log_[stream_frame] = DamageEntry{std::move(damage), whole_frame};
        // Trim history older than the window. Buffer claims outside the
        // window degrade to whole-frame conversions through the "log does not
        // reach back" check in plan_conversion.
        while (!damage_log_.empty() &&
               damage_log_.begin()->first + static_cast<std::int64_t>(history_depth_) <=
                   stream_frame) {
            damage_log_.erase(damage_log_.begin());
        }
    }

    std::size_t history_depth_;
    std::map<std::int64_t, DamageEntry> damage_log_;
    std::unordered_map<const void*, std::int64_t> buffers_;
    std::optional<std::int64_t> last_noted_frame_;
};

} // namespace chronon3d::media::video
