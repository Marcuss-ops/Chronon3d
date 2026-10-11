#pragma once

// ============================================================================
// puppet_deform.hpp — Puppet-pin control-point deformation + 2-bone IK.
//
// AE parity: After Effects' Puppet Tool bends characters / flags /
// illustrations with pins whose neighborhood follows rigidly and falls off
// with distance, plus skeleton joints with IK. Chronon already has MeshWarp
// (raster grid) and path ops (Twist/Pucker/ZigZag/Wiggle) but no pin-driven
// vector deformation. This header adds it WITHOUT touching those engines:
//
//   - apply_puppet_deform(PathShape, PuppetDeformParams): deforms every
//     path vertex (MoveTo/LineTo/Quadratic/Cubic control points) through a
//     deterministic inverse-distance-weighted (IDW) translation field
//     built from pins. Empty pin set = identity. Usable as an offline bake
//     before lowering, or per-frame with animated pin positions baked into
//     AnimationTrack<Vec2> by the caller (same headless pattern as physics).
//   - solve_two_bone_ik(...): analytic 2-joint IK for puppet skeletons.
//
// Determinism: pure functions, no clock/RNG; finite validation; throws
// std::invalid_argument on abuse. Close commands are preserved untouched.
//
// Model: displacement(p) = sum_i w_i * d_i / sum_i w_i, with
//   w_i = strength_i * falloff(dist(p, rest_i) / radius_i),
//   falloff(t) = (1 - smoothstep(0,1,t))^power for radius-limited pins,
//   or 1/(d^power + eps) for infinite-radius pins (radius <= 0).
// Vertices within 1e-4 of a pin rest snap exactly to that pin's deformed
// position (rigid pin, no smearing).
// ============================================================================

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/path.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace chronon3d::puppet {

struct PuppetPin {
    Vec2 rest{0.0f, 0.0f};     // bind pose
    Vec2 deformed{0.0f, 0.0f}; // current pose
    f32 radius{0.0f};          // <= 0 => infinite influence; > 0 => smooth cutoff
    f32 strength{1.0f};        // in [0, 1]
};

struct PuppetDeformParams {
    std::vector<PuppetPin> pins{};
    f32 falloff_power{2.0f}; // in [0.5, 8]
    f32 snap_epsilon{1e-4f}; // rigid-pin snap distance
};

struct TwoBoneIkSolution {
    Vec2 elbow{0.0f};
    Vec2 end{0.0f};
    bool clamped{false}; // true when the target was out of reach
    f32 root_angle{0.0f};
    f32 elbow_angle{0.0f};
};

namespace detail {

[[nodiscard]] inline bool finite_vec(Vec2 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y);
}

inline void validate_params(const PuppetDeformParams& params) {
    if (params.pins.size() > 256)
        throw std::invalid_argument("puppet pin count exceeds the 256 pin limit");
    if (!std::isfinite(params.falloff_power) || params.falloff_power < 0.5f ||
        params.falloff_power > 8.0f)
        throw std::invalid_argument("puppet falloff_power must be in [0.5, 8]");
    if (!std::isfinite(params.snap_epsilon) || params.snap_epsilon < 0.0f ||
        params.snap_epsilon > 10.0f)
        throw std::invalid_argument("puppet snap_epsilon must be finite in [0, 10]");
    for (const auto& pin : params.pins) {
        if (!finite_vec(pin.rest) || !finite_vec(pin.deformed))
            throw std::invalid_argument("puppet pin positions must be finite");
        if (!std::isfinite(pin.radius) || pin.radius < 0.0f || pin.radius > 1e7f)
            throw std::invalid_argument("puppet pin radius must be in [0, 1e7]");
        if (!std::isfinite(pin.strength) || pin.strength < 0.0f ||
            pin.strength > 1.0f)
            throw std::invalid_argument("puppet pin strength must be in [0, 1]");
    }
}

[[nodiscard]] inline float smoothstep01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Weight of one pin at query point p.
[[nodiscard]] inline double pin_weight(const PuppetPin& pin, Vec2 p,
                                      double power) {
    if (pin.strength <= 0.0f)
        return 0.0;
    const double dx = static_cast<double>(p.x) - static_cast<double>(pin.rest.x);
    const double dy = static_cast<double>(p.y) - static_cast<double>(pin.rest.y);
    const double dist = std::sqrt(dx * dx + dy * dy);
    if (pin.radius > 0.0f) {
        const double t = dist / static_cast<double>(pin.radius);
        if (t >= 1.0)
            return 0.0;
        const float s = smoothstep01(static_cast<float>(t));
        const double base = 1.0 - static_cast<double>(s);
        return static_cast<double>(pin.strength) * std::pow(base, power);
    }
    constexpr double kEps = 1e-6;
    return static_cast<double>(pin.strength) / (std::pow(dist + kEps, power) + 1e-12);
}

[[nodiscard]] inline Vec2 deform_point(Vec2 p, const PuppetDeformParams& params) {
    // Rigid snap: vertices at a pin bind pose follow it exactly.
    for (const auto& pin : params.pins) {
        if (pin.strength <= 0.0f)
            continue;
        const float d = glm::length(p - pin.rest);
        if (d <= params.snap_epsilon)
            return pin.deformed;
    }
    double wsum = 0.0;
    double ox = 0.0, oy = 0.0;
    const double power = static_cast<double>(params.falloff_power);
    for (const auto& pin : params.pins) {
        const double w = pin_weight(pin, p, power);
        if (!(w > 0.0))
            continue;
        wsum += w;
        ox += w * (static_cast<double>(pin.deformed.x) - static_cast<double>(pin.rest.x));
        oy += w * (static_cast<double>(pin.deformed.y) - static_cast<double>(pin.rest.y));
    }
    if (!(wsum > 1e-12))
        return p;
    return Vec2{static_cast<float>(static_cast<double>(p.x) + ox / wsum),
                static_cast<float>(static_cast<double>(p.y) + oy / wsum)};
}

} // namespace detail

[[nodiscard]] inline std::optional<std::string> validate_puppet_params(
    const PuppetDeformParams& params) noexcept {
    try {
        detail::validate_params(params);
    } catch (const std::invalid_argument& e) {
        return e.what();
    }
    return std::nullopt;
}

/// Deform every vertex of a path through the pin field. Command types and
/// contour structure are preserved; Close commands pass through.
[[nodiscard]] inline PathShape apply_puppet_deform(
    const PathShape& source, const PuppetDeformParams& params) {
    detail::validate_params(params);
    if (params.pins.empty())
        return source;
    for (const auto& cmd : source.commands) {
        if (cmd.type == PathCommandType::Close)
            continue;
        if (!detail::finite_vec(cmd.p0) || !detail::finite_vec(cmd.p1) ||
            !detail::finite_vec(cmd.p2))
            throw std::invalid_argument("puppet source coordinates must be finite");
    }
    PathShape out = source;
    for (auto& cmd : out.commands) {
        if (cmd.type == PathCommandType::Close)
            continue;
        cmd.p0 = detail::deform_point(cmd.p0, params);
        if (cmd.type == PathCommandType::QuadraticTo ||
            cmd.type == PathCommandType::CubicTo)
            cmd.p1 = detail::deform_point(cmd.p1, params);
        if (cmd.type == PathCommandType::CubicTo)
            cmd.p2 = detail::deform_point(cmd.p2, params);
        if (!detail::finite_vec(cmd.p0) || !detail::finite_vec(cmd.p1) ||
            !detail::finite_vec(cmd.p2))
            throw std::runtime_error("puppet deformation produced non-finite output");
    }
    return out;
}

/// Analytic two-bone IK in the XY plane.
///
/// root: shoulder/hip. len1/len2: bone lengths (> 0). target: desired
/// end-effector. bend_hint: preferred elbow side (e.g. {0,-1} bends down in
/// screen space). Out-of-reach targets clamp along the root->target ray and
/// set clamped=true. All inputs must be finite.
[[nodiscard]] inline TwoBoneIkSolution solve_two_bone_ik(
    Vec2 root, f32 len1, f32 len2, Vec2 target, Vec2 bend_hint = {0.0f, -1.0f}) {
    if (!detail::finite_vec(root) || !detail::finite_vec(target) ||
        !detail::finite_vec(bend_hint))
        throw std::invalid_argument("IK inputs must be finite");
    if (!std::isfinite(len1) || !std::isfinite(len2) || len1 <= 0.0f ||
        len2 <= 0.0f || len1 > 1e7f || len2 > 1e7f)
        throw std::invalid_argument("IK bone lengths must be finite and > 0");
    Vec2 to_target = target - root;
    double dist = static_cast<double>(glm::length(to_target));
    const double l1 = len1, l2 = len2;
    TwoBoneIkSolution sol;
    bool clamped = false;
    Vec2 dir;
    if (dist <= 1e-9) {
        // Target on the root: fold along +x with the hint as bend axis.
        dir = {1.0f, 0.0f};
        dist = 0.0;
        clamped = true;
    } else {
        dir = to_target / static_cast<float>(dist);
    }
    double clamped_dist = dist;
    if (dist > l1 + l2) {
        clamped_dist = l1 + l2;
        clamped = true;
    } else if (dist < std::abs(l1 - l2) + 1e-9) {
        clamped_dist = std::abs(l1 - l2) + 1e-9;
        clamped = true;
    }
    // Law of cosines: angle at root between target ray and bone 1.
    const double cos_a =
        std::clamp((l1 * l1 + clamped_dist * clamped_dist - l2 * l2) /
                       (2.0 * l1 * clamped_dist),
                   -1.0, 1.0);
    const double angle = std::acos(cos_a);
    // Frame: u = dir, v = perpendicular. Bend sign from the hint.
    Vec2 perp{-dir.y, dir.x};
    const double hint_side =
        static_cast<double>(perp.x) * static_cast<double>(bend_hint.x) +
        static_cast<double>(perp.y) * static_cast<double>(bend_hint.y);
    const double side = (hint_side >= 0.0) ? 1.0 : -1.0;
    const Vec2 elbow = root + dir * static_cast<float>(l1 * std::cos(angle)) +
                       perp * static_cast<float>(side * l1 * std::sin(angle));
    const Vec2 end = clamped ? root + dir * static_cast<float>(clamped_dist) : target;
    sol.elbow = elbow;
    sol.end = end;
    sol.clamped = clamped;
    sol.root_angle = std::atan2(static_cast<double>(elbow.y - root.y),
                                static_cast<double>(elbow.x - root.x));
    sol.elbow_angle =
        std::atan2(static_cast<double>(end.y - elbow.y),
                   static_cast<double>(end.x - elbow.x)) -
        sol.root_angle;
    return sol;
}

} // namespace chronon3d::puppet
