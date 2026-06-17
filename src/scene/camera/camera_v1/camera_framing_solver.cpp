// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_framing_solver.cpp
//
// CameraFramingSolver V2 — multi-target framing with dolly/aim strategies.
//
// Algorithm overview:
//   1. Compute weighted centroid of all target bounding boxes.
//   2. Project each bbox to screen space and measure how far it is from
//      the safe area. Compute required dolly (zoom in/out).
//   3. Compute aim point (centroid projected to screen, mapped to
//      rule-of-thirds if requested).
//   4. Apply dead zone + hysteresis via EMA smoothing on dolly/aim.
//   5. Return the framed camera with convergence report.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_framing_solver.hpp>

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <string>

namespace chronon3d::camera_v1 {

// =========================================================================
// Utility — project a 3D point to screen space.
// =========================================================================

namespace {

inline Vec2 project_point(Vec3 world_pos, const Camera2_5D& cam, Viewport vp) {
    // Simple perspective projection: dolly along Z.
    float fov_scale = std::tan(glm::radians(cam.fov_deg) * 0.5f);
    Vec3 local = world_pos - cam.position;

    // Forward = -Z in Chronon3D left-handed coords.
    float depth = -local.z;
    if (std::abs(depth) < 1e-6f) depth = 1e-6f;

    float sx = (local.x / (depth * fov_scale)) * (vp.width  * 0.5f) + (vp.width  * 0.5f);
    float sy = (local.y / (depth * fov_scale)) * (vp.height * 0.5f) + (vp.height * 0.5f);
    return {sx, sy};
}

} // namespace

// =========================================================================
// CameraFramingSolver
// =========================================================================

Vec3 CameraFramingSolver::compute_centroid(
        const std::vector<FramingBBox>& targets) const {
    if (targets.empty()) return {0,0,0};

    Vec3 sum{0,0,0};
    float total_weight = 0.0f;
    for (auto& t : targets) {
        Vec3 center = {(t.min.x + t.max.x) * 0.5f,
                       (t.min.y + t.max.y) * 0.5f,
                       (t.min.z + t.max.z) * 0.5f};
        float w = std::max(0.0f, t.weight);
        sum += center * w;
        total_weight += w;
    }
    return (total_weight > 0.0f) ? (sum / total_weight) : sum;
}

void CameraFramingSolver::project_bbox(const FramingBBox& bbox,
                                        const Camera2_5D& cam,
                                        Viewport vp,
                                        Vec2& out_min, Vec2& out_max) const {
    // Project 8 corners of the bounding box.
    std::array<Vec3, 8> corners = {{
        {bbox.min.x, bbox.min.y, bbox.min.z},
        {bbox.max.x, bbox.min.y, bbox.min.z},
        {bbox.min.x, bbox.max.y, bbox.min.z},
        {bbox.min.x, bbox.min.y, bbox.max.z},
        {bbox.max.x, bbox.max.y, bbox.min.z},
        {bbox.max.x, bbox.min.y, bbox.max.z},
        {bbox.min.x, bbox.max.y, bbox.max.z},
        {bbox.max.x, bbox.max.y, bbox.max.z},
    }};

    out_min = {1e9f, 1e9f};
    out_max = {-1e9f, -1e9f};
    for (auto& c : corners) {
        Vec2 sp = project_point(c, cam, vp);
        out_min.x = std::min(out_min.x, sp.x);
        out_min.y = std::min(out_min.y, sp.y);
        out_max.x = std::max(out_max.x, sp.x);
        out_max.y = std::max(out_max.y, sp.y);
    }
}

float CameraFramingSolver::compute_dolly(const CameraFramingRequest& req,
                                          const Camera2_5D& cam) const {
    float worst_overflow = 0.0f;  // positive = need to dolly out

    for (auto& target : req.targets) {
        Vec2 smin, smax;
        project_bbox(target, cam, req.viewport, smin, smax);

        float safe_l = req.viewport.width  * req.safe_area.left;
        float safe_r = req.viewport.width  * (1.0f - req.safe_area.right);
        float safe_t = req.viewport.height * req.safe_area.top;
        float safe_b = req.viewport.height * (1.0f - req.safe_area.bottom);

        // Overflow beyond safe area (negative = inside safe area).
        float overflow_l = safe_l - smin.x;
        float overflow_r = smax.x - safe_r;
        float overflow_t = safe_t - smin.y;
        float overflow_b = smax.y - safe_b;

        // Max overflow in any direction determines how much to dolly.
        float overflow = std::max({overflow_l, overflow_r, overflow_t, overflow_b, 0.0f});

        // Weighted by target importance.
        worst_overflow = std::max(worst_overflow, overflow * std::max(0.0f, target.weight));
    }

    // Convert screen-space overflow to world-space dolly amount.
    if (worst_overflow > 0.0f) {
        float fov_scale = std::tan(glm::radians(cam.fov_deg) * 0.5f);
        float depth = cam.point_of_interest_enabled
            ? glm::length(cam.point_of_interest - cam.position)
            : 1000.0f;
        if (depth < 1.0f) depth = 1.0f;
        // Screen-to-world: dolly = overflow * depth / (vp_width * 0.5f) * fov_scale
        return worst_overflow * depth * fov_scale / (req.viewport.width * 0.5f);
    }

    // Check if we can dolly in (target is too small in viewport).
    float max_inside = 0.0f;
    for (auto& target : req.targets) {
        Vec2 smin, smax;
        project_bbox(target, cam, req.viewport, smin, smax);

        float safe_l = req.viewport.width  * req.safe_area.left;
        float safe_r = req.viewport.width  * (1.0f - req.safe_area.right);
        float safe_t = req.viewport.height * req.safe_area.top;
        float safe_b = req.viewport.height * (1.0f - req.safe_area.bottom);

        // How far inside each edge (positive = inside).
        float inside_l = smin.x - (safe_l + 30.0f);
        float inside_r = (safe_r - 30.0f) - smax.x;
        float inside_t = smin.y - (safe_t + 30.0f);
        float inside_b = (safe_b - 30.0f) - smax.y;

        float inside = std::min({inside_l, inside_r, inside_t, inside_b});
        max_inside = std::min(max_inside, inside);  // most negative = farthest inside
    }

    // If ALL targets are well inside, we can dolly in.
    if (max_inside > 10.0f) {
        float fov_scale = std::tan(glm::radians(cam.fov_deg) * 0.5f);
        float depth = cam.point_of_interest_enabled
            ? glm::length(cam.point_of_interest - cam.position)
            : 1000.0f;
        if (depth < 1.0f) depth = 1.0f;
        return -(max_inside * 0.5f) * depth * fov_scale / (req.viewport.width * 0.5f);
    }

    return 0.0f;
}

Vec3 CameraFramingSolver::compute_aim_point(const CameraFramingRequest& req,
                                             const Camera2_5D& cam) const {
    Vec3 centroid = compute_centroid(req.targets);

    if (req.strategy == FramingStrategy::RuleOfThirds && !req.targets.empty()) {
        // Place centroid at rule-of-thirds intersection.
        // Default: bottom-right intersection (row=1, col=1).
        Vec2 target_screen = rule_of_thirds_target(req.targets[0], cam, req.viewport, 1, 1);

        // Convert screen target to a world-space aim point near the centroid.
        float depth = cam.point_of_interest_enabled
            ? glm::length(cam.point_of_interest - cam.position)
            : 1000.0f;
        if (depth < 1.0f) depth = 1.0f;
        float fov_scale = std::tan(glm::radians(cam.fov_deg) * 0.5f);
        float world_x = (target_screen.x - req.viewport.width  * 0.5f) * (depth * fov_scale) / (req.viewport.width  * 0.5f);
        float world_y = (target_screen.y - req.viewport.height * 0.5f) * (depth * fov_scale) / (req.viewport.height * 0.5f);

        return centroid + Vec3{world_x, world_y, 0.0f};
    }

    return centroid;
}

Vec2 CameraFramingSolver::rule_of_thirds_target(const FramingBBox& bbox,
                                                  const Camera2_5D&,
                                                  Viewport vp,
                                                  int row, int col) const {
    // 0=top, 1=middle, 2=bottom; 0=left, 1=center, 2=right.
    float x = vp.width  * (1.0f / 3.0f + static_cast<float>(col) / 3.0f);
    float y = vp.height * (1.0f / 3.0f + static_cast<float>(row) / 3.0f);
    return {x, y};
}

Camera2_5D CameraFramingSolver::apply_dead_zone(
        const Camera2_5D& target, const Camera2_5D& current,
        const FramingDeadZone& dz, FramingSession& session) const {
    Camera2_5D result = target;

    float dolly_delta = glm::length(target.position - current.position);
    float dolly_norm = glm::length(current.position) > 1e-6f
        ? dolly_delta / glm::length(current.position) : 0.0f;

    // Dead zone: don't move if below threshold.
    if (dolly_norm < dz.dolly_dead_zone) {
        result.position = current.position;
    }

    // Aim dead zone.
    if (target.point_of_interest_enabled && current.point_of_interest_enabled) {
        float aim_delta = glm::length(target.point_of_interest - current.point_of_interest);
        if (aim_delta < dz.aim_dead_zone_deg * 10.0f) {  // rough px→world mapping
            result.point_of_interest = current.point_of_interest;
        }
    }

    // Hysteresis via EMA smoothing.
    if (session.has_previous) {
        float a = std::clamp(dz.hysteresis, 0.0f, 1.0f);
        session.smoothed_dolly = session.smoothed_dolly
            + a * (dolly_delta - session.smoothed_dolly);
        result.position = current.position
            + glm::normalize(target.position - current.position + Vec3{0,0,1e-6f})
            * session.smoothed_dolly;
    }

    session.previous_camera = result;
    session.has_previous = true;
    return result;
}

CameraFramingResult CameraFramingSolver::solve(
        const CameraFramingRequest& req,
        const Camera2_5D& base_camera,
        FramingSession& session) const {
    CameraFramingResult result;
    result.camera = base_camera;

    if (req.targets.empty()) {
        result.ok = true;
        result.diagnostic = "no targets — returning base camera";
        return result;
    }

    constexpr int kMaxIters = 8;
    Camera2_5D current = base_camera;

    for (int iter = 0; iter < kMaxIters; ++iter) {
        // Compute dolly amount.
        float dolly = compute_dolly(req, current);

        // Compute aim point.
        Vec3 aim = compute_aim_point(req, current);

        // Apply strategy.
        bool dolly_enabled = false, aim_enabled = false;
        switch (req.strategy) {
        case FramingStrategy::FitAll:
        case FramingStrategy::FitHighest:
            dolly_enabled = aim_enabled = true;
            break;
        case FramingStrategy::DollyOnly:
            dolly_enabled = true;
            break;
        case FramingStrategy::AimOnly:
            aim_enabled = true;
            break;
        case FramingStrategy::DollyAndAim:
        case FramingStrategy::RuleOfThirds:
            dolly_enabled = aim_enabled = true;
            break;
        }

        // Apply dolly.
        if (dolly_enabled && std::abs(dolly) > 1e-3f) {
            Vec3 forward = current.point_of_interest_enabled
                ? glm::normalize(current.position - current.point_of_interest)
                : Vec3{0, 0, 1};
            current.position = current.position + forward * dolly;
            current.position.z = std::clamp(current.position.z,
                                            req.min_distance, req.max_distance);
        }

        // Apply aim.
        if (aim_enabled) {
            current.point_of_interest = aim;
            current.point_of_interest_enabled = true;
        }

        // Convergence check.
        if (std::abs(dolly) < 1e-2f) {
            result.convergence.converged = true;
            result.convergence.iterations = iter + 1;
            break;
        }
        result.convergence.iterations = iter + 1;
    }

    // Dead zone + hysteresis.
    result.camera = apply_dead_zone(current, session.previous_camera.has_previous
        ? session.previous_camera : current, req.dead_zone, session);

    result.convergence.dolly_error = glm::length(result.camera.position - base_camera.position);
    result.ok = true;
    return result;
}

} // namespace chronon3d::camera_v1
