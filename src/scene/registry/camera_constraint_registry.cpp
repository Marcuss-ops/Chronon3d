// ==============================================================================
// chronon3d/src/scene/registry/camera_constraint_registry.cpp
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_constraint_resolver.hpp>

#include <map>
#include <mutex>
#include <stdexcept>
#include <cmath>

namespace chronon3d::camera_v1 {

CameraConstraintStack& CameraConstraintStack::add(std::shared_ptr<CameraConstraint> c) {
    if (!c) throw std::invalid_argument("CameraConstraintStack: null constraint");
    stack_.push_back(std::move(c));
    return *this;
}

CameraConstraintStack& CameraConstraintStack::insert(std::size_t i, std::shared_ptr<CameraConstraint> c) {
    if (!c) throw std::invalid_argument("CameraConstraintStack: null constraint");
    if (i > stack_.size()) i = stack_.size();
    stack_.insert(stack_.begin() + static_cast<std::ptrdiff_t>(i), std::move(c));
    return *this;
}

ConstraintResult CameraConstraintStack::resolve(const Camera2_5D& start,
                                                const CameraMotionContext& ctx,
                                                ConstraintSession& session) const {
    ConstraintResult r;
    r.camera = start;
    for (auto& c : stack_) {
        r = c->evaluate(r.camera, ctx, session);
        if (!r.ok) break;
    }
    return r;
}

// =========================================================================
// Built-in constraint: camera.look_at
// Forces the camera to look at a fixed target (Vec3 from ctx.base_target).
// Idempotent: if the camera is already looking there, no-op.
// Robustness: if distance is < 1e-3, returns ok=false with reason.
// =========================================================================
class LookAtConstraint final : public CameraConstraint {
public:
    std::string id() const override { return "camera.look_at"; }
    ConstraintResult evaluate(const Camera2_5D& in,
                              const CameraMotionContext& ctx,
                              ConstraintSession& /*session*/) const override {
        ConstraintResult r;
        Vec3 dir = ctx.base_target - in.position;
        float d = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
        if (d < 1e-3f) {
            r.camera = in;
            r.ok = false;
            r.reason = "target-coincident";
            return r;
        }
        // P5 placeholder: target bookkeeping only; rotation update lands when the
        // P5 plumbing is merged (PUT rotation helper into engine).
        // Mark ok=false with a clear reason so consumers can distinguish
        // "constraint is incomplete" from "constraint succeeded".
        r.camera = in;
        r.camera.point_of_interest = ctx.base_target;
        r.camera.point_of_interest_enabled = true;
        r.ok = false;
        r.reason = "rotation-not-yet-plumbed-P5";
        return r;
    }
};

// =========================================================================
// Built-in constraint: camera.keep_horizon
// Removes roll from the camera. We treat "roll=0" as the canonical horizon.
// Idempotent.
// =========================================================================
class KeepHorizonConstraint final : public CameraConstraint {
public:
    std::string id() const override { return "camera.keep_horizon"; }
    ConstraintResult evaluate(const Camera2_5D& in,
                              const CameraMotionContext& /*ctx*/,
                              ConstraintSession& /*session*/) const override {
        ConstraintResult r;
        r.camera = in;
        r.camera.set_roll(0.0f);
        r.ok = true;
        return r;
    }
};

// =========================================================================
// Built-in constraint: camera.damped_follow (stateful)
// Smooths the camera position toward ctx.base_position with damping f.
// Stateful: reads session.previous_camera / previous_time to apply EMA.
// =========================================================================
class DampedFollowConstraint final : public CameraConstraint {
public:
    explicit DampedFollowConstraint(float damping = 0.15f) : damping_(damping) {}
    std::string id() const override { return "camera.damped_follow"; }
    ConstraintResult evaluate(const Camera2_5D& in,
                              const CameraMotionContext& ctx,
                              ConstraintSession& session) const override {
        ConstraintResult r;
        if (!session.has_previous) {
            r.camera = in;
            session.previous_camera = in;
            session.previous_velocity = {0,0,0};
            session.previous_time = ctx.sample_time;
            session.has_previous = true;
            r.ok = true;
            return r;
        }
        // Velocity extrapolation: use the previously-measured velocity (set by
        // an earlier constraint in the stack) projected forward by sample-time
        // delta, then blend the *extrapolated* anchor with the live input.
        float dt = ctx.sample_time.seconds() - session.previous_time.seconds();
        Vec3 anchor_prev = session.previous_camera.position + session.previous_velocity * dt;
        r.camera = in;
        float a = std::clamp(damping_, 0.0f, 1.0f);
        r.camera.position = {
            in.position.x + a * (anchor_prev.x - in.position.x),
            in.position.y + a * (anchor_prev.y - in.position.y),
            in.position.z + a * (anchor_prev.z - in.position.z),
        };
        // Update velocity for the next sample (forward-difference from session).
        session.previous_velocity = {
            (r.camera.position.x - session.previous_camera.position.x) / std::max(1e-6f, dt),
            (r.camera.position.y - session.previous_camera.position.y) / std::max(1e-6f, dt),
            (r.camera.position.z - session.previous_camera.position.z) / std::max(1e-6f, dt),
        };
        session.previous_camera = r.camera;
        session.previous_time = ctx.sample_time;
        r.ok = true;
        return r;
    }
private:
    float damping_;
};

// =========================================================================
// (CameraConstraintRegistry implementation moved to
//  include/chronon3d/scene/registry/camera_constraint_registry.hpp
//  so that camera_program.cpp can use it too.)
// =========================================================================

// ---------------------------------------------------------------------------
// Built-in registrations (called once from chronon3d_camera_v1::register_defaults).
// ---------------------------------------------------------------------------
static std::shared_ptr<CameraConstraint> make_look_at() {
    return std::make_shared<LookAtConstraint>();
}
static std::shared_ptr<CameraConstraint> make_keep_horizon() {
    return std::make_shared<KeepHorizonConstraint>();
}
static std::shared_ptr<CameraConstraint> make_damped_follow_default() {
    return std::make_shared<DampedFollowConstraint>(0.15f);
}

void register_default_camera_constraints() {
    auto& r = CameraConstraintRegistry::instance();
    if (!r.ids().size()) {
        r.register_factory("camera.look_at", &make_look_at);
        r.register_factory("camera.keep_horizon", &make_keep_horizon);
        r.register_factory("camera.damped_follow", &make_damped_follow_default);
    }
}

} // namespace chronon3d::camera_v1
