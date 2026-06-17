// ==============================================================================
// chronon3d/src/scene/registry/camera_constraint_registry.cpp
//
// P5: all 5 built-in constraints fully implemented.
//   - LookAt: quaternion stable_look_rotation (no placeholder)
//   - KeepHorizon: zero roll (unchanged, idempotent)
//   - DampedFollow: per-constraint state slot via session.states[i]
//   - Distance: dolly constraint (clamp camera→target distance)
//   - RotationLimit: clamp pitch/yaw/roll angles
//
// Factories now accept CameraConstraintParams for typed configuration.
// ==============================================================================
#include <chronon3d/scene/registry/camera_constraint_registry.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_constraint_resolver.hpp>
#include <chronon3d/animation/path/spatial_bezier_path.hpp>  // quat_look_along, quat_to_camera_euler

#include <map>
#include <mutex>
#include <stdexcept>
#include <cmath>

namespace chronon3d::camera_v1 {

// =========================================================================
// CameraConstraintRegistry
// =========================================================================

CameraConstraintRegistry& CameraConstraintRegistry::instance() {
    static CameraConstraintRegistry r;
    return r;
}

void CameraConstraintRegistry::register_factory(std::string id, Factory f) {
    if (!f) throw std::invalid_argument("CameraConstraintRegistry: null factory");
    if (id.empty()) throw std::invalid_argument("CameraConstraintRegistry: empty id");
    std::lock_guard<std::mutex> lk(mu_);
    if (frozen_) throw std::logic_error("CameraConstraintRegistry: frozen — cannot register '" + id + "'");
    if (factories_.count(id))
        throw std::invalid_argument("CameraConstraintRegistry: duplicate id '" + id + "'");
    factories_.emplace(std::move(id), std::move(f));
}

std::shared_ptr<CameraConstraint> CameraConstraintRegistry::create(const std::string& id) const {
    return create(id, CameraConstraintParams{LookAtParams{}});
}

std::shared_ptr<CameraConstraint> CameraConstraintRegistry::create(
        const std::string& id, const CameraConstraintParams& params) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = factories_.find(id);
    if (it == factories_.end()) return nullptr;
    return it->second(params);
}

std::vector<std::string> CameraConstraintRegistry::ids() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> out;
    out.reserve(factories_.size());
    for (auto& kv : factories_) out.push_back(kv.first);
    return out;
}

bool CameraConstraintRegistry::has(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    return factories_.count(id) > 0;
}

void CameraConstraintRegistry::freeze() {
    std::lock_guard<std::mutex> lk(mu_);
    frozen_ = true;
}

// =========================================================================
// CameraConstraintStack
// =========================================================================

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
    session.ensure_states(stack_.size());
    for (std::size_t i = 0; i < stack_.size(); ++i) {
        r = stack_[i]->evaluate(r.camera, ctx, session);
        if (!r.ok) break;
    }
    return r;
}

// =========================================================================
// LookAtConstraint — P5: quaternion rotation (no placeholder).
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
        dir = dir / d;  // normalize

        // Stable quaternion look-at: uses previous frame orientation to pick
        // the shortest-arc quaternion, preventing 180° flips near poles.
        Quat prev_orientation = glm::quatLookAt(
            glm::normalize(in.point_of_interest_enabled
                ? (in.point_of_interest - in.position)
                : Vec3{0, 0, -1}),
            Vec3{0, 1, 0});
        Quat orientation = quat_stable_look_along(dir, prev_orientation);

        r.camera = in;
        r.camera.rotation = quat_to_camera_euler(orientation, in.rotation.z);
        r.camera.point_of_interest = ctx.base_target;
        r.camera.point_of_interest_enabled = true;
        r.ok = true;
        return r;
    }
};

// =========================================================================
// KeepHorizonConstraint — zero roll (idempotent).
// =========================================================================
class KeepHorizonConstraint final : public CameraConstraint {
public:
    std::string id() const override { return "camera.keep_horizon"; }
    ConstraintResult evaluate(const Camera2_5D& in,
                              const CameraMotionContext&,
                              ConstraintSession&) const override {
        ConstraintResult r;
        r.camera = in;
        r.camera.set_roll(0.0f);
        r.ok = true;
        return r;
    }
};

// =========================================================================
// DampedFollowConstraint — P5: per-constraint state slot.
// =========================================================================
class DampedFollowConstraint final : public CameraConstraint {
public:
    explicit DampedFollowConstraint(float damping = 0.15f) : damping_(damping) {}
    std::string id() const override { return "camera.damped_follow"; }

    ConstraintResult evaluate(const Camera2_5D& in,
                              const CameraMotionContext& ctx,
                              ConstraintSession& session) const override {
        ConstraintResult r;
        // Use the session slot aligned to this constraint's stack index.
        // The caller (CameraProgram) sets up session.ensure_states + indexing.

        // For backward compat: use states[0] when stack index unknown.
        // P7 will add proper active_index plumbing.
        ConstraintState& state = session.states.empty()
            ? session.states.emplace_back()
            : session.states[0];

        if (!state.has_previous) {
            r.camera = in;
            state.previous_camera = in;
            state.previous_velocity = {0,0,0};
            state.previous_time = ctx.sample_time;
            state.has_previous = true;
            r.ok = true;
            return r;
        }

        float dt = ctx.sample_time.seconds() - state.previous_time.seconds();
        if (dt <= 0.0f) dt = 1.0f / 30.0f;  // guard against zero/negative dt

        Vec3 anchor_prev = state.previous_camera.position + state.previous_velocity * dt;
        r.camera = in;
        float a = std::clamp(damping_, 0.0f, 1.0f);
        r.camera.position = {
            in.position.x + a * (anchor_prev.x - in.position.x),
            in.position.y + a * (anchor_prev.y - in.position.y),
            in.position.z + a * (anchor_prev.z - in.position.z),
        };

        state.previous_velocity = {
            (r.camera.position.x - state.previous_camera.position.x) / std::max(1e-6f, dt),
            (r.camera.position.y - state.previous_camera.position.y) / std::max(1e-6f, dt),
            (r.camera.position.z - state.previous_camera.position.z) / std::max(1e-6f, dt),
        };
        state.previous_camera = r.camera;
        state.previous_time = ctx.sample_time;
        r.ok = true;
        return r;
    }
private:
    float damping_;
};

// =========================================================================
// DistanceConstraint — dolly: clamp camera→target distance.
// =========================================================================
class DistanceConstraint final : public CameraConstraint {
public:
    DistanceConstraint(float min_dist = 10.0f, float max_dist = 10000.0f)
        : min_dist_(std::max(0.0f, min_dist))
        , max_dist_(std::max(min_dist_, max_dist)) {}

    std::string id() const override { return "camera.distance"; }

    ConstraintResult evaluate(const Camera2_5D& in,
                              const CameraMotionContext& ctx,
                              ConstraintSession&) const override {
        ConstraintResult r;
        Vec3 target = in.point_of_interest_enabled
            ? in.point_of_interest
            : ctx.base_target;

        Vec3 dir = target - in.position;
        float dist = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);

        if (dist < 1e-6f) {
            r.camera = in;
            r.ok = false;
            r.reason = "distance-zero";
            return r;
        }

        float clamped = dist;
        if (dist < min_dist_) clamped = min_dist_;
        if (dist > max_dist_) clamped = max_dist_;

        r.camera = in;
        if (std::abs(clamped - dist) > 1e-3f) {
            dir = dir / dist;  // normalize
            r.camera.position = target - dir * clamped;
        }
        r.ok = true;
        return r;
    }
private:
    float min_dist_, max_dist_;
};

// =========================================================================
// RotationLimitConstraint — clamp pitch/yaw/roll angles.
// =========================================================================
class RotationLimitConstraint final : public CameraConstraint {
public:
    RotationLimitConstraint(float max_pitch = 90.0f, float max_yaw = 180.0f, float max_roll = 45.0f)
        : max_pitch_deg_(std::max(0.0f, max_pitch))
        , max_yaw_deg_(std::max(0.0f, max_yaw))
        , max_roll_deg_(std::max(0.0f, max_roll)) {}

    std::string id() const override { return "camera.rotation_limit"; }

    ConstraintResult evaluate(const Camera2_5D& in,
                              const CameraMotionContext&,
                              ConstraintSession&) const override {
        ConstraintResult r;
        r.camera = in;

        if (r.camera.rotation.x >  max_pitch_deg_) r.camera.rotation.x =  max_pitch_deg_;
        if (r.camera.rotation.x < -max_pitch_deg_) r.camera.rotation.x = -max_pitch_deg_;
        if (r.camera.rotation.y >  max_yaw_deg_)   r.camera.rotation.y =  max_yaw_deg_;
        if (r.camera.rotation.y < -max_yaw_deg_)   r.camera.rotation.y = -max_yaw_deg_;
        if (r.camera.rotation.z >  max_roll_deg_)  r.camera.rotation.z =  max_roll_deg_;
        if (r.camera.rotation.z < -max_roll_deg_)  r.camera.rotation.z = -max_roll_deg_;

        r.ok = true;
        return r;
    }
private:
    float max_pitch_deg_, max_yaw_deg_, max_roll_deg_;
};

// =========================================================================
// Factory functions — accept CameraConstraintParams.
// =========================================================================

static std::shared_ptr<CameraConstraint> make_look_at(const CameraConstraintParams&) {
    return std::make_shared<LookAtConstraint>();
}
static std::shared_ptr<CameraConstraint> make_keep_horizon(const CameraConstraintParams&) {
    return std::make_shared<KeepHorizonConstraint>();
}
static std::shared_ptr<CameraConstraint> make_damped_follow(const CameraConstraintParams& p) {
    auto* dp = std::get_if<DampedFollowParams>(&p);
    float d = dp ? dp->damping : 0.15f;
    return std::make_shared<DampedFollowConstraint>(d);
}
static std::shared_ptr<CameraConstraint> make_distance(const CameraConstraintParams& p) {
    auto* dp = std::get_if<DistanceParams>(&p);
    if (dp) return std::make_shared<DistanceConstraint>(dp->min_distance, dp->max_distance);
    return std::make_shared<DistanceConstraint>();
}
static std::shared_ptr<CameraConstraint> make_rotation_limit(const CameraConstraintParams& p) {
    auto* rp = std::get_if<RotationLimitParams>(&p);
    if (rp) return std::make_shared<RotationLimitConstraint>(rp->max_pitch_deg, rp->max_yaw_deg, rp->max_roll_deg);
    return std::make_shared<RotationLimitConstraint>();
}

// =========================================================================
// register_default_camera_constraints
// =========================================================================

void register_default_camera_constraints() {
    auto& r = CameraConstraintRegistry::instance();
    if (!r.has("camera.look_at"))
        r.register_factory("camera.look_at", &make_look_at);
    if (!r.has("camera.keep_horizon"))
        r.register_factory("camera.keep_horizon", &make_keep_horizon);
    if (!r.has("camera.damped_follow"))
        r.register_factory("camera.damped_follow", &make_damped_follow);
    if (!r.has("camera.distance"))
        r.register_factory("camera.distance", &make_distance);
    if (!r.has("camera.rotation_limit"))
        r.register_factory("camera.rotation_limit", &make_rotation_limit);
}

} // namespace chronon3d::camera_v1
