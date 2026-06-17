#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_framing_solver.hpp
//
// CameraFramingSolver V2 — replaces the legacy fit_camera_to_layers() with
// multi-target, safe-area, dead-zone, hysteresis, and strategy-based solving.
//
// Supports:
//   - Real axis-aligned bounding boxes with per-target weights
//   - Safe area margins (left/right/top/bottom, normalized)
//   - Rule of thirds placement
//   - Dolly / aim / zoom strategies
//   - Dead zone + hysteresis for smooth transitions
//   - Multi-target weighted centroid for group framing
//   - Look-ahead via FramingSession velocity state
//   - Convergence report
// ==============================================================================
#include <chronon3d/math/camera_2_5d_projection.hpp>  // Camera2_5D
#include <chronon3d/math/glm_types.hpp>                // Vec3, Vec2
#include <chronon3d/scene/camera/camera_projection.hpp> // Viewport

#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d::camera_v1 {

// =========================================================================
// Bounding box in world space.
// =========================================================================
struct FramingBBox {
    Vec3  min{0,0,0};
    Vec3  max{0,0,0};
    float weight{1.0f};   // importance for multi-target centroid
};

// =========================================================================
// Safe area — normalized margins from viewport edges.
// =========================================================================
struct FramingSafeArea {
    float left{0.12f};
    float right{0.12f};
    float top{0.12f};
    float bottom{0.12f};
};

// =========================================================================
// Framing strategy.
// =========================================================================
enum class FramingStrategy : std::uint8_t {
    FitAll        = 0,  // dolly to fit all targets within safe area
    FitHighest    = 1,  // fit only the highest-weight target
    RuleOfThirds  = 2,  // place centroid at rule-of-thirds intersection
    DollyOnly     = 3,  // only change distance, preserve aim
    AimOnly       = 4,  // only change aim point, preserve distance
    DollyAndAim   = 5,  // adjust both dolly and aim together
};

// =========================================================================
// Dead zone + hysteresis — prevents micro-jitter between frames.
// =========================================================================
struct FramingDeadZone {
    float dolly_dead_zone{0.02f};     // normalized — ignore dolly < 2% change
    float aim_dead_zone_deg{2.0f};    // degrees — ignore aim < 2° change
    float hysteresis{0.8f};           // EMA factor: 1.0 = instant, 0.0 = frozen
};

// =========================================================================
// Request
// =========================================================================
struct CameraFramingRequest {
    std::vector<FramingBBox> targets;
    FramingSafeArea          safe_area;
    FramingStrategy          strategy{FramingStrategy::FitAll};
    FramingDeadZone          dead_zone;
    Viewport                 viewport{1920, 1080};

    float min_zoom{1.0f};
    float max_zoom{10000.0f};
    float min_distance{10.0f};
    float max_distance{50000.0f};
};

// =========================================================================
// Convergence report — how close to solution.
// =========================================================================
struct FramingConvergence {
    bool  converged{false};
    float dolly_error{0.0f};
    float aim_error_deg{0.0f};
    int   iterations{0};
};

// =========================================================================
// Result
// =========================================================================
struct CameraFramingResult {
    Camera2_5D          camera;
    FramingConvergence  convergence;
    bool                ok{true};
    std::string         diagnostic;
};

// =========================================================================
// Session — state that persists across frames for hysteresis.
// =========================================================================
struct FramingSession {
    Camera2_5D previous_camera{};
    Vec3       previous_aim_target{0,0,0};
    float      smoothed_dolly{0.0f};
    bool       has_previous{false};

    void reset() { *this = {}; }
};

// =========================================================================
// CameraFramingSolver
// =========================================================================
class CameraFramingSolver {
public:
    CameraFramingSolver() = default;

    /// Solve framing for a request. Returns the framed camera.
    CameraFramingResult solve(const CameraFramingRequest& req,
                               const Camera2_5D& base_camera,
                               FramingSession& session) const;

private:
    /// Weighted centroid of all targets.
    Vec3 compute_centroid(const std::vector<FramingBBox>& targets) const;

    /// Project a bounding box to screen-space [min, max] in pixels.
    void project_bbox(const FramingBBox& bbox, const Camera2_5D& cam,
                      Viewport vp, Vec2& out_min, Vec2& out_max) const;

    /// How much to dolly (negative = dolly in, positive = dolly out).
    float compute_dolly(const CameraFramingRequest& req,
                         const Camera2_5D& cam) const;

    /// Target aim point in world space.
    Vec3 compute_aim_point(const CameraFramingRequest& req,
                            const Camera2_5D& cam) const;

    /// Apply dead zone + hysteresis to smooth the transition.
    Camera2_5D apply_dead_zone(const Camera2_5D& target,
                                const Camera2_5D& current,
                                const FramingDeadZone& dz,
                                FramingSession& session) const;

    /// Rule-of-thirds placement: which intersection (0=top-left .. 4=center).
    Vec2 rule_of_thirds_target(const FramingBBox& bbox,
                                const Camera2_5D& cam, Viewport vp,
                                int row, int col) const;
};

} // namespace chronon3d::camera_v1
