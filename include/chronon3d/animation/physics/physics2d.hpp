#pragma once

// ============================================================================
// physics2d.hpp — deterministic 2D rigid-body simulation (AE Newton-lite).
//
// Scope (deliberately NOT a 3D engine): circles + axis-aligned boxes,
// static floors / walls / AABB platforms, uniform gravity, per-body mass,
// restitution (bounciness), linear air damping + Coulomb-ish ground
// friction. Semi-implicit Euler at a fixed substep (default 1/240 s).
//
// Headless contract (matches spring.hpp / particles.hpp):
//   - pure function of (config, bodies, duration): no clock, no RNG.
//   - simulate-then-bake: pre-simulate the whole shot and convert to
//     AnimationTrack<Vec2> (+ rotation) so the renderer can sample frame
//     500 without stepping 0..499 at render time (random-access).
//   - finite validation; throws std::invalid_argument on abuse.
//   - translation-only contact solver; boxes stay axis-aligned (rotation
//     state is integrated for rendering roll but does not steer contacts).
//     Documented limitation, not a silent error.
//
// Coordinate convention: +y is DOWN (screen space, like the particle
// sampler where gravity adds +y). Default gravity is (0, 980) px/s^2.
//
// Example — ball drop with e=0.65:
//   PhysicsWorldConfig cfg;
//   cfg.gravity = {0.0f, 980.0f};
//   PhysicsBody ball = PhysicsBody::circle({160, 40}, 20.0f, 1.0f, 0.65f);
//   ball.velocity = {60.0f, 0.0f};
//   cfg.floor_y = 400.0f; // static floor plane
//   auto tracks = bake_physics_simulation(cfg, {ball}, 2.0, 60.0f);
//   // tracks[0].position keys hold per-frame centers; apply via
//   // AnimatedValue<Vec2>::apply_track(track.position.keys()) or sample
//   // with sample_baked_position(tracks[0], frame).
// ============================================================================

#include <chronon3d/animation/core/animation_track.hpp>
#include <chronon3d/animation/core/keyframe.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace chronon3d::physics2d {

enum class BodyShape : std::uint8_t { Circle, Box };

struct PhysicsBody {
    BodyShape shape{BodyShape::Circle};
    Vec2 position{0.0f, 0.0f};
    Vec2 velocity{0.0f, 0.0f};
    f32 radius{10.0f};              // circle only
    Vec2 half_extents{10.0f, 10.0f}; // box only (half width/height)
    f32 mass{1.0f};                 // > 0, or 0 => static body
    f32 restitution{0.5f};          // in [0, 1]
    f32 friction{0.4f};             // ground friction in [0, 1]
    f32 rotation{0.0f};             // radians, rendering only
    f32 angular_velocity{0.0f};     // rad/s, rendering only

    [[nodiscard]] static PhysicsBody circle(Vec2 center, f32 r, f32 m = 1.0f,
                                           f32 e = 0.5f) {
        PhysicsBody b;
        b.shape = BodyShape::Circle;
        b.position = center;
        b.radius = r;
        b.mass = m;
        b.restitution = e;
        return b;
    }
    [[nodiscard]] static PhysicsBody box(Vec2 center, Vec2 half_extents,
                                        f32 m = 1.0f, f32 e = 0.3f) {
        PhysicsBody b;
        b.shape = BodyShape::Box;
        b.position = center;
        b.half_extents = half_extents;
        b.mass = m;
        b.restitution = e;
        return b;
    }
    [[nodiscard]] bool is_static() const noexcept { return mass <= 0.0f; }
};

struct StaticAABB {
    Vec2 min{0.0f, 0.0f};
    Vec2 max{0.0f, 0.0f};
};

struct PhysicsWorldConfig {
    Vec2 gravity{0.0f, 980.0f};
    f32 air_damping{0.0f};      // linear velocity loss per second in [0, 10]
    f32 substep_hz{240.0f};     // fixed integrator rate in [30, 2400]
    // Optional static planes (unset = +infinity => disabled).
    f32 floor_y{std::numeric_limits<f32>::infinity()}; // top surface y of floor
    f32 ceiling_y{-std::numeric_limits<f32>::infinity()};
    f32 wall_left{-std::numeric_limits<f32>::infinity()};
    f32 wall_right{std::numeric_limits<f32>::infinity()};
    std::vector<StaticAABB> platforms{};
    std::size_t max_bodies{1024};
};

struct BakedBodyTrack {
    AnimationTrack<Vec2> position{};
    AnimationTrack<f32> rotation{};
    Vec2 start_position{0.0f};
};

namespace detail {

inline void validate_config(const PhysicsWorldConfig& cfg) {
    if (!std::isfinite(cfg.gravity.x) || !std::isfinite(cfg.gravity.y))
        throw std::invalid_argument("physics gravity must be finite");
    if (!std::isfinite(cfg.air_damping) || cfg.air_damping < 0.0f ||
        cfg.air_damping > 10.0f)
        throw std::invalid_argument("physics air_damping must be in [0, 10]");
    if (!std::isfinite(cfg.substep_hz) || cfg.substep_hz < 30.0f ||
        cfg.substep_hz > 2400.0f)
        throw std::invalid_argument("physics substep_hz must be in [30, 2400]");
    for (const auto& p : cfg.platforms) {
        if (!std::isfinite(p.min.x) || !std::isfinite(p.min.y) ||
            !std::isfinite(p.max.x) || !std::isfinite(p.max.y) ||
            p.max.x <= p.min.x || p.max.y <= p.min.y)
            throw std::invalid_argument("physics platform requires finite min < max");
    }
}

inline void validate_body(const PhysicsBody& b) {
    if (!std::isfinite(b.position.x) || !std::isfinite(b.position.y) ||
        !std::isfinite(b.velocity.x) || !std::isfinite(b.velocity.y) ||
        !std::isfinite(b.rotation) || !std::isfinite(b.angular_velocity))
        throw std::invalid_argument("physics body state must be finite");
    if (!std::isfinite(b.mass) || b.mass < 0.0f)
        throw std::invalid_argument("physics body mass must be >= 0");
    if (b.mass > 0.0f && b.mass < 1e-9f)
        throw std::invalid_argument("physics body mass is too small");
    if (!std::isfinite(b.restitution) || b.restitution < 0.0f ||
        b.restitution > 1.0f)
        throw std::invalid_argument("physics restitution must be in [0, 1]");
    if (!std::isfinite(b.friction) || b.friction < 0.0f || b.friction > 1.0f)
        throw std::invalid_argument("physics friction must be in [0, 1]");
    if (b.shape == BodyShape::Circle) {
        if (!std::isfinite(b.radius) || b.radius <= 0.0f || b.radius > 1e6f)
            throw std::invalid_argument("physics circle radius must be finite and > 0");
    } else {
        if (!std::isfinite(b.half_extents.x) || !std::isfinite(b.half_extents.y) ||
            b.half_extents.x <= 0.0f || b.half_extents.y <= 0.0f ||
            b.half_extents.x > 1e6f || b.half_extents.y > 1e6f)
            throw std::invalid_argument("physics box half_extents must be finite and > 0");
    }
}

struct SimState {
    PhysicsBody body;
    f32 inv_mass{1.0f};
};

// Positional + velocity projection of body against a static AABB.
// Returns true when a contact was resolved.
inline bool resolve_vs_static_aabb(SimState& s, const StaticAABB& box) {
    if (s.body.is_static())
        return false;
    if (s.body.shape == BodyShape::Circle) {
        const Vec2 closest{std::clamp(s.body.position.x, box.min.x, box.max.x),
                           std::clamp(s.body.position.y, box.min.y, box.max.y)};
        Vec2 delta = s.body.position - closest;
        f32 dist = glm::length(delta);
        const bool inside = dist <= 1e-6f;
        if (!inside && dist >= s.body.radius)
            return false;
        Vec2 normal;
        f32 penetration;
        if (inside) {
            // Center inside the box: push out along the smallest axis.
            const f32 dx_left = s.body.position.x - box.min.x;
            const f32 dx_right = box.max.x - s.body.position.x;
            const f32 dy_top = s.body.position.y - box.min.y;
            const f32 dy_bottom = box.max.y - s.body.position.y;
            const f32 m = std::min({dx_left, dx_right, dy_top, dy_bottom});
            if (m == dx_left)
                normal = {-1.0f, 0.0f};
            else if (m == dx_right)
                normal = {1.0f, 0.0f};
            else if (m == dy_top)
                normal = {0.0f, -1.0f};
            else
                normal = {0.0f, 1.0f};
            penetration = s.body.radius + m;
        } else {
            normal = delta / dist;
            penetration = s.body.radius - dist;
        }
        s.body.position += normal * penetration;
        const f32 vn = glm::dot(s.body.velocity, normal);
        if (vn < 0.0f) {
            Vec2 v_n = normal * vn;
            Vec2 v_t = s.body.velocity - v_n;
            v_t *= (1.0f - s.body.friction * 0.5f);
            // Resting-contact slop: tiny bounces decay to sleep.
            const f32 bounce = (vn > -15.0f) ? 0.0f : -vn * s.body.restitution;
            s.body.velocity = v_t + normal * bounce;
            if (std::abs(s.body.velocity.x) < 1.0f && std::abs(normal.x) < 0.5f)
                s.body.velocity.x *= 0.9f;
        }
        return true;
    }
    // Box vs static box (both axis-aligned).
    const Vec2 bmin = s.body.position - s.body.half_extents;
    const Vec2 bmax = s.body.position + s.body.half_extents;
    if (bmax.x <= box.min.x || bmin.x >= box.max.x || bmax.y <= box.min.y ||
        bmin.y >= box.max.y)
        return false;
    const f32 push_left = bmax.x - box.min.x;
    const f32 push_right = box.max.x - bmin.x;
    const f32 push_up = bmax.y - box.min.y;
    const f32 push_down = box.max.y - bmin.y;
    const f32 m = std::min({push_left, push_right, push_up, push_down});
    if (m == push_up) {
        s.body.position.y -= push_up;
        if (s.body.velocity.y > 0.0f) {
            const f32 bounce = (s.body.velocity.y < 15.0f)
                                   ? 0.0f
                                   : -s.body.velocity.y * s.body.restitution;
            s.body.velocity.y = bounce;
            s.body.velocity.x *= (1.0f - s.body.friction);
            if (std::abs(s.body.velocity.x) < 1.0f)
                s.body.velocity.x = 0.0f;
        }
    } else if (m == push_down) {
        s.body.position.y += push_down;
        if (s.body.velocity.y < 0.0f)
            s.body.velocity.y = -s.body.velocity.y * s.body.restitution;
    } else if (m == push_left) {
        s.body.position.x -= push_left;
        if (s.body.velocity.x > 0.0f)
            s.body.velocity.x = -s.body.velocity.x * s.body.restitution;
    } else {
        s.body.position.x += push_right;
        if (s.body.velocity.x < 0.0f)
            s.body.velocity.x = -s.body.velocity.x * s.body.restitution;
    }
    return true;
}

inline void resolve_planes(SimState& s, const PhysicsWorldConfig& cfg) {
    if (s.body.is_static())
        return;
    const f32 bottom_offset =
        (s.body.shape == BodyShape::Circle) ? s.body.radius : s.body.half_extents.y;
    const f32 top_offset = bottom_offset;
    const f32 left_offset =
        (s.body.shape == BodyShape::Circle) ? s.body.radius : s.body.half_extents.x;
    const f32 right_offset = left_offset;
    if (std::isfinite(cfg.floor_y)) {
        const f32 bottom = s.body.position.y + bottom_offset;
        if (bottom > cfg.floor_y) {
            s.body.position.y = cfg.floor_y - bottom_offset;
            if (s.body.velocity.y > 0.0f) {
                const f32 impact = s.body.velocity.y;
                s.body.velocity.y = (impact < 15.0f)
                                        ? 0.0f
                                        : -impact * s.body.restitution;
                s.body.velocity.x *= (1.0f - s.body.friction);
                if (std::abs(s.body.velocity.x) < 1.0f)
                    s.body.velocity.x = 0.0f;
                // Rolling coupling for circles (rendering only).
                if (s.body.shape == BodyShape::Circle && s.body.radius > 1e-6f)
                    s.body.angular_velocity = s.body.velocity.x / s.body.radius;
            }
        }
    }
    if (std::isfinite(cfg.ceiling_y)) {
        const f32 top = s.body.position.y - top_offset;
        if (top < cfg.ceiling_y) {
            s.body.position.y = cfg.ceiling_y + top_offset;
            if (s.body.velocity.y < 0.0f)
                s.body.velocity.y = -s.body.velocity.y * s.body.restitution;
        }
    }
    if (std::isfinite(cfg.wall_left)) {
        if (s.body.position.x - left_offset < cfg.wall_left) {
            s.body.position.x = cfg.wall_left + left_offset;
            if (s.body.velocity.x < 0.0f)
                s.body.velocity.x = -s.body.velocity.x * s.body.restitution;
        }
    }
    if (std::isfinite(cfg.wall_right)) {
        if (s.body.position.x + right_offset > cfg.wall_right) {
            s.body.position.x = cfg.wall_right - right_offset;
            if (s.body.velocity.x > 0.0f)
                s.body.velocity.x = -s.body.velocity.x * s.body.restitution;
        }
    }
}

inline void resolve_body_pair(SimState& a, SimState& b) {
    if (a.body.is_static() && b.body.is_static())
        return;
    const f32 e = std::min(a.body.restitution, b.body.restitution);
    if (a.body.shape == BodyShape::Circle && b.body.shape == BodyShape::Circle) {
        Vec2 delta = b.body.position - a.body.position;
        const f32 dist = glm::length(delta);
        const f32 min_dist = a.body.radius + b.body.radius;
        if (dist >= min_dist || dist <= 1e-9f)
            return;
        const Vec2 n = delta / dist;
        const f32 pen = min_dist - dist;
        const f32 wa = a.body.is_static() ? 0.0f : a.inv_mass;
        const f32 wb = b.body.is_static() ? 0.0f : b.inv_mass;
        const f32 wsum = wa + wb;
        if (wsum <= 0.0f)
            return;
        a.body.position -= n * (pen * (wa / wsum));
        b.body.position += n * (pen * (wb / wsum));
        const Vec2 rel = b.body.velocity - a.body.velocity;
        const f32 vn = glm::dot(rel, n);
        if (vn < 0.0f) {
            const f32 j = -(1.0f + e) * vn / wsum;
            if (!a.body.is_static())
                a.body.velocity -= n * (j * wa);
            if (!b.body.is_static())
                b.body.velocity += n * (j * wb);
        }
        return;
    }
    // Circle-vs-box and box-vs-box approximated axis-aligned.
    auto as_aabb = [](const SimState& s) {
        if (s.body.shape == BodyShape::Circle)
            return StaticAABB{s.body.position - Vec2{s.body.radius},
                              s.body.position + Vec2{s.body.radius}};
        return StaticAABB{s.body.position - s.body.half_extents,
                          s.body.position + s.body.half_extents};
    };
    const StaticAABB aa = as_aabb(a), bb = as_aabb(b);
    if (aa.max.x <= bb.min.x || aa.min.x >= bb.max.x || aa.max.y <= bb.min.y ||
        aa.min.y >= bb.max.y)
        return;
    const f32 ox = std::min(aa.max.x, bb.max.x) - std::max(aa.min.x, bb.min.x);
    const f32 oy = std::min(aa.max.y, bb.max.y) - std::max(aa.min.y, bb.min.y);
    const f32 wa = a.body.is_static() ? 0.0f : a.inv_mass;
    const f32 wb = b.body.is_static() ? 0.0f : b.inv_mass;
    const f32 wsum = wa + wb;
    if (wsum <= 0.0f)
        return;
    if (ox < oy) {
        const f32 dir = (a.body.position.x < b.body.position.x) ? -1.0f : 1.0f;
        if (!a.body.is_static())
            a.body.position.x += dir * ox * (wa / wsum);
        if (!b.body.is_static())
            b.body.position.x -= dir * ox * (wb / wsum);
        const f32 rel = b.body.velocity.x - a.body.velocity.x;
        if (rel * dir > 0.0f) { // approaching (sign-aware)
            const f32 j = -(1.0f + e) * rel / wsum;
            if (!a.body.is_static())
                a.body.velocity.x -= j * wa * -dir * dir; // == j*wa along x
            if (!b.body.is_static())
                b.body.velocity.x += j * wb;
            // Simplify: project to restitution response along x.
            if (!a.body.is_static() && !b.body.is_static()) {
                const f32 avg = (a.body.velocity.x + b.body.velocity.x) * 0.5f;
                a.body.velocity.x = avg;
                b.body.velocity.x = avg;
            }
        }
    } else {
        const f32 dir = (a.body.position.y < b.body.position.y) ? -1.0f : 1.0f;
        if (!a.body.is_static())
            a.body.position.y += dir * oy * (wa / wsum);
        if (!b.body.is_static())
            b.body.position.y -= dir * oy * (wb / wsum);
        const f32 rel = b.body.velocity.y - a.body.velocity.y;
        if (rel * dir > 0.0f) {
            if (!a.body.is_static() && !b.body.is_static()) {
                const f32 avg = (a.body.velocity.y + b.body.velocity.y) * 0.5f;
                a.body.velocity.y = avg;
                b.body.velocity.y = avg;
            } else if (!a.body.is_static()) {
                a.body.velocity.y = b.body.velocity.y + dir * std::abs(rel) * e;
            } else {
                b.body.velocity.y = a.body.velocity.y - dir * std::abs(rel) * e;
            }
        }
    }
}

} // namespace detail

/// Full pre-simulation: returns per-body per-frame positions.
/// frames+1 samples at `fps` over `duration_seconds`.
[[nodiscard]] inline std::vector<std::vector<Vec2>> simulate_positions(
    const PhysicsWorldConfig& cfg, const std::vector<PhysicsBody>& bodies,
    double duration_seconds, double fps,
    std::vector<std::vector<f32>>* out_rotations = nullptr) {
    detail::validate_config(cfg);
    if (bodies.empty())
        throw std::invalid_argument("physics simulation requires >= 1 body");
    if (bodies.size() > cfg.max_bodies)
        throw std::invalid_argument("physics body count exceeds max_bodies");
    if (!std::isfinite(duration_seconds) || duration_seconds <= 0.0 ||
        duration_seconds > 600.0)
        throw std::invalid_argument("physics duration must be in (0, 600] s");
    if (!std::isfinite(fps) || fps < 1.0 || fps > 240.0)
        throw std::invalid_argument("physics bake fps must be in [1, 240]");
    for (const auto& b : bodies)
        detail::validate_body(b);

    std::vector<detail::SimState> state;
    state.reserve(bodies.size());
    for (const auto& b : bodies) {
        detail::SimState s{b, 0.0f};
        s.inv_mass = b.is_static() ? 0.0f : 1.0f / b.mass;
        state.push_back(s);
    }

    const double dt = 1.0 / static_cast<double>(cfg.substep_hz);
    const std::size_t frames =
        static_cast<std::size_t>(std::llround(duration_seconds * fps));
    const std::size_t steps_total = static_cast<std::size_t>(
        std::llround(duration_seconds / dt));
    const std::size_t steps_per_frame =
        std::max<std::size_t>(1, steps_total / std::max<std::size_t>(1, frames));

    std::vector<std::vector<Vec2>> traj(bodies.size());
    std::vector<std::vector<f32>> rot(bodies.size());
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        traj[i].reserve(frames + 1);
        rot[i].reserve(frames + 1);
        traj[i].push_back(state[i].body.position);
        rot[i].push_back(state[i].body.rotation);
    }

    const f32 damp_factor_base =
        std::clamp(cfg.air_damping, 0.0f, 10.0f);
    std::size_t step = 0;
    for (std::size_t f = 0; f < frames; ++f) {
        for (std::size_t k = 0; k < steps_per_frame; ++k, ++step) {
            // Integrate.
            for (auto& s : state) {
                if (s.body.is_static())
                    continue;
                s.body.velocity += cfg.gravity * static_cast<f32>(dt);
                if (damp_factor_base > 0.0f) {
                    const f32 keep =
                        std::max(0.0f, 1.0f - damp_factor_base * static_cast<f32>(dt));
                    s.body.velocity *= keep;
                    s.body.angular_velocity *= keep;
                }
                s.body.position += s.body.velocity * static_cast<f32>(dt);
                s.body.rotation += s.body.angular_velocity * static_cast<f32>(dt);
                if (!std::isfinite(s.body.position.x) ||
                    !std::isfinite(s.body.position.y) ||
                    !std::isfinite(s.body.velocity.x) ||
                    !std::isfinite(s.body.velocity.y))
                    throw std::runtime_error("physics integration went non-finite");
            }
            // Contacts: planes + platforms, then body pairs (2 relaxations).
            for (auto& s : state) {
                detail::resolve_planes(s, cfg);
                for (const auto& p : cfg.platforms)
                    detail::resolve_vs_static_aabb(s, p);
            }
            for (int iter = 0; iter < 2; ++iter)
                for (std::size_t i = 0; i < state.size(); ++i)
                    for (std::size_t j = i + 1; j < state.size(); ++j)
                        detail::resolve_body_pair(state[i], state[j]);
        }
        for (std::size_t i = 0; i < state.size(); ++i) {
            traj[i].push_back(state[i].body.position);
            rot[i].push_back(state[i].body.rotation);
        }
    }
    if (out_rotations)
        *out_rotations = std::move(rot);
    return traj;
}

/// Pre-simulate and bake to AnimationTrack curves (random-access friendly).
/// One key per frame with Linear easing; sample with AnimatedValue or by
/// lerping adjacent keys.
[[nodiscard]] inline std::vector<BakedBodyTrack> bake_physics_simulation(
    const PhysicsWorldConfig& cfg, const std::vector<PhysicsBody>& bodies,
    double duration_seconds, double fps) {
    std::vector<std::vector<f32>> rotations;
    auto traj = simulate_positions(cfg, bodies, duration_seconds, fps, &rotations);
    std::vector<BakedBodyTrack> out;
    out.reserve(bodies.size());
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        BakedBodyTrack t;
        t.start_position = bodies[i].position;
        for (std::size_t f = 0; f < traj[i].size(); ++f) {
            t.position.to(Frame{static_cast<i64>(f)}, traj[i][f], Easing::Linear);
            t.rotation.to(Frame{static_cast<i64>(f)}, rotations[i][f], Easing::Linear);
        }
        out.push_back(std::move(t));
    }
    return out;
}

/// Sample a baked position track at a fractional frame (linear interp).
[[nodiscard]] inline Vec2 sample_baked_position(const BakedBodyTrack& track,
                                               double frame) {
    const auto& keys = track.position.keys();
    if (keys.empty())
        return track.start_position;
    if (frame <= static_cast<double>(keys.front().frame.integral()))
        return keys.front().value;
    if (frame >= static_cast<double>(keys.back().frame.integral()))
        return keys.back().value;
    for (std::size_t i = 1; i < keys.size(); ++i) {
        const double f0 = static_cast<double>(keys[i - 1].frame.integral());
        const double f1 = static_cast<double>(keys[i].frame.integral());
        if (frame >= f0 && frame <= f1) {
            const double t = (f1 > f0) ? (frame - f0) / (f1 - f0) : 0.0;
            const Vec2 a = keys[i - 1].value, b = keys[i].value;
            const float ft = static_cast<float>(std::clamp(t, 0.0, 1.0));
            return Vec2{a.x + (b.x - a.x) * ft, a.y + (b.y - a.y) * ft};
        }
    }
    return keys.back().value;
}

} // namespace chronon3d::physics2d
