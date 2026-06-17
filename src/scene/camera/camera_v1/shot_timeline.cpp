// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/shot_timeline.cpp
//
// ShotTimeline implementation with 5 built-in transitions.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/shot_timeline.hpp>

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <stdexcept>

namespace chronon3d::camera_v1 {

// =========================================================================
// ShotTimeline
// =========================================================================

ShotTimeline& ShotTimeline::add_shot(CameraShot shot) {
    if (!shots_.empty() && shot.start_frame < shots_.back().end_frame) {
        shot.start_frame = shots_.back().end_frame;  // auto-fix gaps
    }
    shots_.push_back(std::move(shot));
    return *this;
}

const CameraShot* ShotTimeline::find_shot(int frame) const {
    for (auto& s : shots_) {
        if (frame >= s.start_frame && frame < s.end_frame)
            return &s;
    }
    return nullptr;
}

ShotTimeline::ShotPair ShotTimeline::find_pair(int frame) const {
    for (int i = 0; i < static_cast<int>(shots_.size()); ++i) {
        if (frame >= shots_[i].start_frame && frame < shots_[i].end_frame) {
            const CameraShot* next = (i + 1 < static_cast<int>(shots_.size()))
                ? &shots_[i + 1] : nullptr;
            return {i, &shots_[i], next};
        }
    }
    return {-1, nullptr, nullptr};
}

int ShotTimeline::total_frames() const {
    if (shots_.empty()) return 0;
    return shots_.back().end_frame;
}

// =========================================================================
// Built-in transitions
// =========================================================================

namespace {

// --- Cut ------------------------------------------------------------------
class CutTransition final : public CameraTransition {
public:
    std::string id() const override { return "camera.transition.cut"; }
    Camera2_5D evaluate(float t, const Camera2_5D&, const Camera2_5D& to_cam) const override {
        return (t < 1.0f) ? Camera2_5D{} : to_cam;  // instant at t=1
    }
};

// --- SmoothBlend ----------------------------------------------------------
class SmoothBlendTransition final : public CameraTransition {
public:
    std::string id() const override { return "camera.transition.smooth_blend"; }
    Camera2_5D evaluate(float t, const Camera2_5D& from, const Camera2_5D& to) const override {
        Camera2_5D out = from;
        out.position = glm::mix(from.position, to.position, t);
        if (from.point_of_interest_enabled && to.point_of_interest_enabled) {
            out.point_of_interest = glm::mix(from.point_of_interest, to.point_of_interest, t);
        }
        out.rotation = glm::mix(from.rotation, to.rotation, t);
        out.fov_deg = from.fov_deg + (to.fov_deg - from.fov_deg) * t;
        out.zoom = from.zoom + (to.zoom - from.zoom) * t;
        return out;
    }
};

// --- Push -----------------------------------------------------------------
class PushTransition final : public CameraTransition {
public:
    std::string id() const override { return "camera.transition.push"; }
    Camera2_5D evaluate(float t, const Camera2_5D& from, const Camera2_5D& to) const override {
        // Push: move the camera through an arc from from to to.
        // Uses a quadratic ease-out for a dynamic feel.
        float et = t * (2.0f - t);  // ease-out
        Camera2_5D out = from;
        out.position = glm::mix(from.position, to.position, et);
        if (from.point_of_interest_enabled && to.point_of_interest_enabled) {
            out.point_of_interest = glm::mix(from.point_of_interest, to.point_of_interest, t);
        }
        return out;
    }
};

// --- WhipPan --------------------------------------------------------------
class WhipPanTransition final : public CameraTransition {
public:
    std::string id() const override { return "camera.transition.whip_pan"; }
    Camera2_5D evaluate(float t, const Camera2_5D& from, const Camera2_5D& to) const override {
        // Whip pan: fast pan with motion blur feel.
        // Uses cubic ease for a whip-like acceleration.
        float et = t * t * (3.0f - 2.0f * t);  // smoothstep gives snap at end
        Camera2_5D out = from;
        out.position = from.position;  // keep position
        out.rotation = glm::mix(from.rotation, to.rotation, et);
        if (from.point_of_interest_enabled && to.point_of_interest_enabled) {
            out.point_of_interest = glm::mix(from.point_of_interest, to.point_of_interest, et);
        }
        return out;
    }
};

// --- FocusHandoff ---------------------------------------------------------
class FocusHandoffTransition final : public CameraTransition {
public:
    std::string id() const override { return "camera.transition.focus_handoff"; }
    Camera2_5D evaluate(float t, const Camera2_5D& from, const Camera2_5D& to) const override {
        // Focus handoff: rack focus while keeping position.
        Camera2_5D out = from;
        out.position = from.position;
        out.rotation = from.rotation;
        out.focus_distance = from.focus_distance + (to.focus_distance - from.focus_distance) * t;
        return out;
    }
};

} // namespace

// =========================================================================
// Default transition factories
// =========================================================================

std::shared_ptr<CameraTransition> ShotTimelineResolver::default_cut() {
    return std::make_shared<CutTransition>();
}
std::shared_ptr<CameraTransition> ShotTimelineResolver::default_smooth_blend() {
    return std::make_shared<SmoothBlendTransition>();
}
std::shared_ptr<CameraTransition> ShotTimelineResolver::default_push() {
    return std::make_shared<PushTransition>();
}
std::shared_ptr<CameraTransition> ShotTimelineResolver::default_whip_pan() {
    return std::make_shared<WhipPanTransition>();
}
std::shared_ptr<CameraTransition> ShotTimelineResolver::default_focus_handoff() {
    return std::make_shared<FocusHandoffTransition>();
}

// =========================================================================
// ShotTimelineResolver
// =========================================================================

ShotTimelineResolver::ShotTimelineResolver(std::shared_ptr<ShotTimeline> timeline)
    : timeline_(std::move(timeline)) {
    // Register built-in transitions.
    transitions_[CameraTransitionKind::Cut]          = default_cut();
    transitions_[CameraTransitionKind::SmoothBlend]  = default_smooth_blend();
    transitions_[CameraTransitionKind::Push]         = default_push();
    transitions_[CameraTransitionKind::WhipPan]      = default_whip_pan();
    transitions_[CameraTransitionKind::FocusHandoff] = default_focus_handoff();
}

void ShotTimelineResolver::set_transition(CameraTransitionKind kind,
                                           std::shared_ptr<CameraTransition> t) {
    if (t) transitions_[kind] = std::move(t);
}

std::shared_ptr<CameraTransition> ShotTimelineResolver::get_transition(
        CameraTransitionKind kind) const {
    auto it = transitions_.find(kind);
    if (it != transitions_.end()) return it->second;
    return default_cut();  // fallback
}

Camera2_5D ShotTimelineResolver::evaluate(int frame, ConstraintSession& session) const {
    if (!timeline_ || timeline_->empty()) return {};

    auto pair = timeline_->find_pair(frame);
    if (!pair.current) return {};

    const CameraShot& shot = *pair.current;

    // Check if we're in a transition overlap with the next shot.
    // Cut transitions should have transition_frames=0; if misconfigured, skip overlap.
    if (pair.next && shot.transition_frames > 0 &&
        shot.transition_in != CameraTransitionKind::Cut &&
        frame >= shot.end_frame - shot.transition_frames) {
        int overlap_start = shot.end_frame - shot.transition_frames;
        float t = static_cast<float>(frame - overlap_start) /
                  static_cast<float>(std::max(1, shot.transition_frames));
        t = std::clamp(t, 0.0f, 1.0f);

        // Evaluate both shots.
        auto ctx = CameraMotionContext::at(frame);
        ctx.base_target = shot.program.base_cam().point_of_interest;

        ConstraintSession s_from, s_to;
        Camera2_5D from_cam = shot.program.evaluate(ctx, s_from).camera;
        Camera2_5D to_cam   = pair.next->program.evaluate(ctx, s_to).camera;

        auto transition = get_transition(shot.transition_in);
        return transition->evaluate(t, from_cam, to_cam);
    }

    // No transition — evaluate the current shot directly.
    auto ctx = CameraMotionContext::at(frame);
    ctx.base_target = shot.program.base_cam().point_of_interest;
    return shot.program.evaluate(ctx, session).camera;
}

// =========================================================================
// CameraTransitionRegistry
// =========================================================================

CameraTransitionRegistry& CameraTransitionRegistry::instance() {
    static CameraTransitionRegistry r;
    return r;
}

void CameraTransitionRegistry::register_transition(CameraTransitionKind kind, Factory f) {
    if (!f) throw std::invalid_argument("CameraTransitionRegistry: null factory");
    std::lock_guard<std::mutex> lk(mu_);
    factories_[kind] = std::move(f);
}

std::shared_ptr<CameraTransition> CameraTransitionRegistry::create(
        CameraTransitionKind kind) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = factories_.find(kind);
    return (it != factories_.end()) ? it->second() : nullptr;
}

bool CameraTransitionRegistry::has(CameraTransitionKind kind) const {
    std::lock_guard<std::mutex> lk(mu_);
    return factories_.count(kind) > 0;
}

} // namespace chronon3d::camera_v1
