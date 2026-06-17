// ==============================================================================
// chronon3d/src/scene/registry/camera_constraint_registry.cpp
//
// CameraConstraintRegistry — singleton + 5 built-in constraints (P5 complete).
//
// Built-ins:
//   camera.look_at         — quaternion-stable look-at (not placeholder)
//   camera.keep_horizon    — zero roll, preserve yaw/pitch
//   camera.damped_follow   — per-constraint EMA smoothing via state slot
//   camera.distance        — clamp camera→target distance
//   camera.rotation_limit  — clamp pitch/yaw/roll angles
//
// All factories accept CameraConstraintParams (variant).
// Stateful constraints use session.active_state() (per-constraint slot).
// ==============================================================================
#include <chronon3d/scene/registry/camera_constraint_registry.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_constraint_resolver.hpp>
#include <chronon3d/animation/path/spatial_bezier_path.hpp>  // quat_look_along, quat_to_camera_euler

#include <map>
#include <mutex>
#include <stdexcept>
#include <cmath>
#include <glm/glm.hpp>

namespace chronon3d::camera_v1 {

// =========================================================================
// CameraConstraintRegistry — singleton + registration.
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
    if (factories_.count(id)) {
        throw std::invalid_argument("CameraConstraintRegistry: duplicate id '" + id + "'");
    }
    factories_.emplace(std::move(id), f);
}

std::shared_ptr<CameraConstraint> CameraConstraintRegistry::create(const std::string& id) const {
    // Default params: use DampedFollowParams as a neutral default.
    DampedFollowParams defaults;
    return create(id, defaults);
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
    for (std::size_t i = 0; i < stack_.size(); ++i) {
        session.active_index = i;  // per-constraint state slot
        r = stack_[i]->evaluate(r.camera, ctx, session);
        if (!r.ok) break;
    }
    return r;
}

// =========================================================================
// Helper: stable quaternion look-at rotation.
// =========================================================================
namespace {

inline void rotate_toward_target(Camera2_5D& cam, Vec3 target) {
    Vec3 dir = target - cam.position;
    float d = glm::length(dir);
    if (d < 1e-4f) return;
    dir = dir / d;
    const Quat orientation = quat_look_along(dir);
    cam.rotation = quat_to_camera_euler(orientation, cam.rotation.z);
    cam.point_of_interest = target;
    cam.point_of_interest_enabled = true;
}

} // namespace

// =========================================================================
// Built-in constraint: camera.look_at
// P5 complete — uses quaternion-stable look-at.
// =========================================================================
class LookAtConstraint final : public CameraConstraint {
public:
    explicit LookAtConstraint(const LookAtParams& /*params*/) {}
    std::string id() const override { return "camera.look_at"; }
    ConstraintResult evaluate(const Camera2_5D& in,
                              const CameraMotionContext& ctx,
                              ConstraintSession& /*session*/) const override {
        ConstraintResult r;
        Vec3 dir = ctx.base_target - in.position;
        float d = glm::length(dir);
        if (d < 1e-3f) {
            r.camera = in;
            r.ok = false;
            r.reason = "target-coincident";
            return r;
        }
        r.camera = in;
        rotate_toward_target(r.camera, ctx.base_target);
        r.ok = true;
        return r;
    }
};

// =========================================================================
// Built-in constraint: camera.keep_horizon
// =========================================================================
class KeepHorizonConstraint final : public CameraConstraint {
public:
    explicit KeepHorizonConstraint(const KeepHorizonParams& /*params*/) {}
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
// Built-in constraint: camera.damped_follow (P5 — per-constraint state)
// =========================================================================
class DampedFollowConstraint final : public CameraConstraint {
public:
    explicit DampedFollowConstraint(const DampedFollowParams& p) : damping_(p.damping) {}
    std::string id() const override { return "camera.damped_follow"; }
    ConstraintResult evaluate(const Camera2_5D& in,
                              const CameraMotionContext& ctx,
                              ConstraintSession& session) const override {
        ConstraintResult r;
        auto& state = session.active_state();  // per-constraint slot

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
        if (dt <= 0.0f) { r.camera = state.previous_camera; r.ok = true; return r; }

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
// Built-in constraint: camera.distance
// Clamps camera→target distance to [min_distance, max_distance].
// =========================================================================
class DistanceConstraint final : public CameraConstraint {
public:
    explicit DistanceConstraint(const DistanceParams& p)
        : min_d_(p.min_distance), max_d_(p.max_distance) {
        if (min_d_ <= 0.0f) min_d_ = 1.0f;
        if (max_d_ < min_d_) max_d_ = min_d_ * 10.0f;
    }
    std::string id() const override { return "camera.distance"; }
    ConstraintResult evaluate(const Camera2_5D& in,
                              const CameraMotionContext& /*ctx*/,
                              ConstraintSession& /*session*/) const override {
        ConstraintResult r;
        Vec3 target = in.point_of_interest_enabled
            ? in.point_of_interest : Vec3{in.position.x, in.position.y, in.position.z - 1000.0f};

        Vec3 to_target = target - in.position;
        float dist = glm::length(to_target);
        if (dist < 1e-3f) {
            r.camera = in;
            r.ok = false;
            r.reason = "distance-zero";
            return r;
        }
        Vec3 dir = to_target / dist;

        float clamped = std::clamp(dist, min_d_, max_d_);
        r.camera = in;
        r.camera.position = target - dir * clamped;
        r.ok = true;
        return r;
    }
private:
    float min_d_, max_d_;
};

// =========================================================================
// Built-in constraint: camera.rotation_limit
// Clamps pitch/yaw/roll to [-max, +max] per axis.
// =========================================================================
class RotationLimitConstraint final : public CameraConstraint {
public:
    explicit RotationLimitConstraint(const RotationLimitParams& p)
        : max_pitch_(std::max(0.0f, p.max_pitch_deg)),
          max_yaw_(std::max(0.0f, p.max_yaw_deg)),
          max_roll_(std::max(0.0f, p.max_roll_deg)) {}
    std::string id() const override { return "camera.rotation_limit"; }
    ConstraintResult evaluate(const Camera2_5D& in,
                              const CameraMotionContext& /*ctx*/,
                              ConstraintSession& /*session*/) const override {
        ConstraintResult r;
        r.camera = in;
        r.camera.rotation.x = std::clamp(in.rotation.x, -max_pitch_, max_pitch_);
        r.camera.rotation.y = std::clamp(in.rotation.y, -max_yaw_, max_yaw_);
        r.camera.rotation.z = std::clamp(in.rotation.z, -max_roll_, max_roll_);
        r.ok = true;
        return r;
    }
private:
    float max_pitch_, max_yaw_, max_roll_;
};

// =========================================================================
// Factory functions — extract params from variant, dispatch to constructor.
// =========================================================================
namespace {

template <typename T, typename Impl>
std::shared_ptr<CameraConstraint> make_from_params(const CameraConstraintParams& params) {
    if (auto* p = std::get_if<T>(&params)) {
        return std::make_shared<Impl>(*p);
    }
    // Fallback: use default-constructed params.
    return std::make_shared<Impl>(T{});
}

} // namespace

static std::shared_ptr<CameraConstraint> make_look_at(const CameraConstraintParams& p) {
    return make_from_params<LookAtParams, LookAtConstraint>(p);
}
static std::shared_ptr<CameraConstraint> make_keep_horizon(const CameraConstraintParams& p) {
    return make_from_params<KeepHorizonParams, KeepHorizonConstraint>(p);
}
static std::shared_ptr<CameraConstraint> make_damped_follow(const CameraConstraintParams& p) {
    return make_from_params<DampedFollowParams, DampedFollowConstraint>(p);
}
static std::shared_ptr<CameraConstraint> make_distance(const CameraConstraintParams& p) {
    return make_from_params<DistanceParams, DistanceConstraint>(p);
}
static std::shared_ptr<CameraConstraint> make_rotation_limit(const CameraConstraintParams& p) {
    return make_from_params<RotationLimitParams, RotationLimitConstraint>(p);
}

// =========================================================================
// register_default_camera_constraints — called once at startup.
// Checks each ID individually for idempotent registration.
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
