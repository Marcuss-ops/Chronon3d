// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_framing_solver.cpp
//
// CameraFramingSolver V2 — multi-target framing with dolly/aim strategies.
//
// Uses canonical project_world_to_screen() (no duplicate projection math).
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_framing_solver.hpp>
#include <chronon3d/scene/camera/camera_projection.hpp>  // project_world_to_screen

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace chronon3d::camera_v1 {

// =========================================================================
// Angular deviation helper (degrees) between two direction vectors viewed
// from the camera origin.  Returns 0 if either direction is degenerate.
// =========================================================================
static float angular_deviation_deg(const Vec3& a, const Vec3& b) {
    const float la = glm::length(a);
    const float lb = glm::length(b);
    if (la < 1e-3f || lb < 1e-3f) return 0.0f;
    const float cos_a = std::clamp(glm::dot(a, b) / (la * lb), -1.0f, 1.0f);
    return glm::degrees(glm::acos(cos_a));
}

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
    // Project 8 corners using canonical project_world_to_screen.
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
    bool any_projected = false;
    for (auto& c : corners) {
        auto sp = project_world_to_screen(c, cam, vp);
        // Framing hard-point fix: skip behind-camera corners. Without this,
        // their inverted screen positions overflow the bounding box and spuriously
        // force a dolly-out even when the bbox is fully visible in front of the
        // camera.
        if (sp.behind_camera) continue;
        out_min.x = std::min(out_min.x, sp.position.x);
        out_min.y = std::min(out_min.y, sp.position.y);
        out_max.x = std::max(out_max.x, sp.position.x);
        out_max.y = std::max(out_max.y, sp.position.y);
        any_projected = true;
    }
    // If every corner ended up behind the camera, leave the bbox at a sentinel
    // that downstream code will see as empty (out_min > out_max).
    if (!any_projected) {
        out_min = {1e9f, 1e9f};
        out_max = {-1e9f, -1e9f};
    }
}

// Select targets for the active strategy.
static std::vector<FramingBBox> select_targets(const CameraFramingRequest& req) {
    if (req.strategy == FramingStrategy::FitHighest && !req.targets.empty()) {
        // FitHighest: only the highest-weight target.
        auto best = req.targets[0];
        for (auto& t : req.targets) {
            if (t.weight > best.weight) best = t;
        }
        return {best};
    }
    return req.targets;
}

float CameraFramingSolver::compute_dolly(const CameraFramingRequest& req,
                                          const Camera2_5D& cam) const {
    auto active = select_targets(req);
    if (active.empty()) return 0.0f;

    float worst_overflow = 0.0f;

    for (auto& target : active) {
        Vec2 smin, smax;
        project_bbox(target, cam, req.viewport, smin, smax);

        float safe_l = req.viewport.width  * req.safe_area.left;
        float safe_r = req.viewport.width  * (1.0f - req.safe_area.right);
        float safe_t = req.viewport.height * req.safe_area.top;
        float safe_b = req.viewport.height * (1.0f - req.safe_area.bottom);

        float overflow_l = safe_l - smin.x;
        float overflow_r = smax.x - safe_r;
        float overflow_t = safe_t - smin.y;
        float overflow_b = smax.y - safe_b;

        float overflow = std::max({overflow_l, overflow_r, overflow_t, overflow_b, 0.0f});
        worst_overflow = std::max(worst_overflow, overflow * std::max(0.0f, target.weight));
    }

    if (worst_overflow > 0.0f) {
        float fov_scale = std::tan(glm::radians(cam.fov_deg) * 0.5f);
        float depth = cam.point_of_interest_enabled
            ? glm::length(cam.point_of_interest - cam.position)
            : 1000.0f;
        if (depth < 1.0f) depth = 1.0f;
        return worst_overflow * depth * fov_scale / (req.viewport.width * 0.5f);
    }

    // Check if we can dolly in (target too small in viewport).
    float min_inside = std::numeric_limits<float>::max();
    for (auto& target : active) {
        Vec2 smin, smax;
        project_bbox(target, cam, req.viewport, smin, smax);

        float safe_l = req.viewport.width  * req.safe_area.left;
        float safe_r = req.viewport.width  * (1.0f - req.safe_area.right);
        float safe_t = req.viewport.height * req.safe_area.top;
        float safe_b = req.viewport.height * (1.0f - req.safe_area.bottom);

        float inside_l = smin.x - (safe_l + 30.0f);
        float inside_r = (safe_r - 30.0f) - smax.x;
        float inside_t = smin.y - (safe_t + 30.0f);
        float inside_b = (safe_b - 30.0f) - smax.y;

        float inside = std::min({inside_l, inside_r, inside_t, inside_b});
        min_inside = std::min(min_inside, inside);  // min across all targets
    }

    if (min_inside > 10.0f) {
        float fov_scale = std::tan(glm::radians(cam.fov_deg) * 0.5f);
        float depth = cam.point_of_interest_enabled
            ? glm::length(cam.point_of_interest - cam.position)
            : 1000.0f;
        if (depth < 1.0f) depth = 1.0f;
        return -(min_inside * 0.5f) * depth * fov_scale / (req.viewport.width * 0.5f);
    }

    return 0.0f;
}

Vec3 CameraFramingSolver::compute_aim_point(const CameraFramingRequest& req,
                                             const Camera2_5D& cam,
                                             const FramingSession& session) const {
    auto active = select_targets(req);
    Vec3 centroid = compute_centroid(active);
    Vec3 aim = centroid;

    if (req.strategy == FramingStrategy::RuleOfThirds && !active.empty()) {
        Vec2 target_screen = rule_of_thirds_target(active[0], cam, req.viewport, 1, 1);

        float depth = cam.point_of_interest_enabled
            ? glm::length(cam.point_of_interest - cam.position)
            : 1000.0f;
        if (depth < 1.0f) depth = 1.0f;
        float fov_scale = std::tan(glm::radians(cam.fov_deg) * 0.5f);
        float world_x = (target_screen.x - req.viewport.width  * 0.5f) * (depth * fov_scale) / (req.viewport.width  * 0.5f);
        float world_y = (target_screen.y - req.viewport.height * 0.5f) * (depth * fov_scale) / (req.viewport.height * 0.5f);

        aim = centroid + Vec3{world_x, world_y, 0.0f};
    }

    // Framing hard-point fix: aim_error_deg tolerance gate.  std::max guards
    // against negative thresholds (treated the same as 0/disabled), preserving
    // the "0 = disabled" sentinel contract.    angular_deviation_deg returns
    // a non-negative value, so when tol == 0 the < tol comparison is always
    // false and the gate never fires regardless of has_value().
    const float tol = std::max(0.0f, req.aim_error_deg);
    if (tol > 0.0f && session.previous_aim_target.has_value()) {
        const Vec3 prev_aim   = *session.previous_aim_target;
        const Vec3 old_dir    = prev_aim - cam.position;
        const Vec3 fresh_dir  = aim    - cam.position;
        if (angular_deviation_deg(old_dir, fresh_dir) < tol) {
            aim = prev_aim;  // micro-adjustment suppressed — keep prior aim
        }
    }

    return aim;
}

Vec2 CameraFramingSolver::rule_of_thirds_target(const FramingBBox&,
                                                  const Camera2_5D&,
                                                  Viewport vp,
                                                  int row, int col) const {
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

    if (dolly_norm < dz.dolly_dead_zone) {
        result.position = current.position;
    }

    if (target.point_of_interest_enabled && current.point_of_interest_enabled) {
        float aim_delta = glm::length(target.point_of_interest - current.point_of_interest);
        if (aim_delta < dz.aim_dead_zone_deg * 10.0f) {
            result.point_of_interest = current.point_of_interest;
        }
    }

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
    // Mirror the previous aim into session state so the compute_aim_point
    // tolerance gate has a baseline on the next frame.  std::optional makes
    // "no previous" explicit, sidestepping the (0,0,0)-truthy-sentinel hazard.
    if (target.point_of_interest_enabled) {
        session.previous_aim_target = result.point_of_interest;
    }
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
        float dolly = compute_dolly(req, current);
        Vec3 aim = compute_aim_point(req, current, session);

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

        // Apply dolly (move along forward axis by dolly amount).
        if (dolly_enabled && std::abs(dolly) > 1e-3f) {
            Vec3 target_pt = current.point_of_interest_enabled
                ? current.point_of_interest
                : Vec3{current.position.x, current.position.y, current.position.z - 1000.0f};
            Vec3 forward = glm::normalize(current.position - target_pt);

            Vec3 new_pos = current.position + forward * dolly;

            // Clamp distance to target, not world-Z coordinate.
            float new_dist = glm::length(new_pos - target_pt);
            if (new_dist < req.min_distance || new_dist > req.max_distance) {
                float clamped = std::clamp(new_dist, req.min_distance, req.max_distance);
                new_pos = target_pt + forward * clamped;
            }
            current.position = new_pos;
        }

        if (aim_enabled) {
            current.point_of_interest = aim;
            current.point_of_interest_enabled = true;
        }

        if (std::abs(dolly) < 1e-2f) {
            result.convergence.converged = true;
            result.convergence.iterations = iter + 1;
            break;
        }
        result.convergence.iterations = iter + 1;
    }

    // Framing hard-point fix: clamp zoom to request bounds (min_zoom, max_zoom).
    // Previously these were accepted in CameraFramingRequest but never applied,
    // leaving users with one fewer knob than the API implied.
    current.zoom = std::clamp(current.zoom, req.min_zoom, req.max_zoom);

    // Dead zone + hysteresis.
    Camera2_5D prev = session.has_previous ? session.previous_camera : current;
    result.camera = apply_dead_zone(current, prev, req.dead_zone, session);

    // Report the residual convergence error for both dolly and aim so callers
    // can decide when to stop iterating / log diagnostics. aim_error_deg is
    // measured at the camera origin between the framed aim and the base
    // camera's prior aim (or final aim if no prior was set).
    result.convergence.dolly_error = glm::length(result.camera.position - base_camera.position);
    if (result.camera.point_of_interest_enabled) {
        Vec3 final_dir = result.camera.point_of_interest - result.camera.position;
        Vec3 base_dir  = base_camera.point_of_interest_enabled
            ? (base_camera.point_of_interest - base_camera.position)
            : final_dir;
        result.convergence.aim_error_deg = angular_deviation_deg(final_dir, base_dir);
    }
    result.ok = true;
    return result;
}

} // namespace chronon3d::camera_v1
