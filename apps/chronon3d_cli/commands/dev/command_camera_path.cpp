// ==============================================================================
// CLI Command: camera-path
// Export the camera path of a composition as JSON or CSV for debug/validation.
// Samples the baked Camera2_5D at each frame and reports position, rotation,
// zoom, FOV, target, velocity, and path metrics.
// ==============================================================================
#include "../../commands.hpp"
#include "../../utils/job/cli_render_utils.hpp"
#include <chronon3d/scene/model/camera_2_5d.hpp>
#include <chronon3d/scene/model/scene.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cmath>

namespace chronon3d {
namespace cli {

namespace {

// ── Per-frame sample ─────────────────────────────────────────────────────────
struct PathSample {
    int frame{0};
    Vec3 position{0.0f};
    Vec3 rotation{0.0f};    // euler degrees (tilt, yaw, roll)
    f32  zoom{0.0f};
    f32  fov_deg{0.0f};
    Vec3 target{0.0f};      // point of interest (if enabled)
    bool poi_enabled{false};
    f32  velocity{0.0f};    // distance from previous sample per frame
    f32  path_distance{0.0f}; // cumulative arc-length from start
};

// ── Path metrics ─────────────────────────────────────────────────────────────
struct PathMetrics {
    f32 total_length{0.0f};
    f32 max_velocity{0.0f};
    f32 max_acceleration{0.0f};
    f32 max_target_error_px{0.0f};
    int num_samples{0};
};

// ── Compute velocity and path metrics from raw samples ───────────────────────
void compute_metrics(std::vector<PathSample>& samples, PathMetrics& metrics) {
    metrics.num_samples = static_cast<int>(samples.size());
    if (samples.empty()) return;

    metrics.total_length = 0.0f;
    f32 prev_velocity = 0.0f;

    for (size_t i = 0; i < samples.size(); ++i) {
        if (i == 0) {
            samples[i].velocity = 0.0f;
            samples[i].path_distance = 0.0f;
            continue;
        }

        const Vec3 delta = samples[i].position - samples[i - 1].position;
        const f32 dist = glm::length(delta);
        const f32 dt = static_cast<f32>(samples[i].frame - samples[i - 1].frame);
        const f32 vel = (dt > 0.0f) ? dist / dt : 0.0f;

        samples[i].velocity = vel;
        samples[i].path_distance = samples[i - 1].path_distance + dist;

        metrics.total_length += dist;
        metrics.max_velocity = std::max(metrics.max_velocity, vel);

        // Acceleration = velocity change per frame
        if (i >= 2) {
            const f32 accel = std::abs(vel - prev_velocity);
            metrics.max_acceleration = std::max(metrics.max_acceleration, accel);
        }
        prev_velocity = vel;
    }
}

// ── Detect format from file extension ────────────────────────────────────────
std::string detect_format(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "json";
    std::string ext = path.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    if (ext == "csv") return "csv";
    return "json";
}

// ── JSON export ──────────────────────────────────────────────────────────────
bool write_json(const std::string& path,
                const std::string& comp_id,
                int start, int end, int step,
                const std::vector<PathSample>& samples,
                const PathMetrics& metrics) {
    nlohmann::json js;
    js["composition"] = comp_id;
    js["range"] = {{"start", start}, {"end", end}, {"step", step}};
    js["metrics"] = {
        {"num_samples",       metrics.num_samples},
        {"total_length",      metrics.total_length},
        {"max_velocity",      metrics.max_velocity},
        {"max_acceleration",  metrics.max_acceleration},
    };

    auto& arr = js["samples"];
    for (const auto& s : samples) {
        nlohmann::json entry;
        entry["frame"] = s.frame;
        entry["position"] = {s.position.x, s.position.y, s.position.z};
        entry["rotation"] = {s.rotation.x, s.rotation.y, s.rotation.z};
        entry["zoom"]     = s.zoom;
        entry["fov_deg"]  = s.fov_deg;
        if (s.poi_enabled) {
            entry["target"] = {s.target.x, s.target.y, s.target.z};
        }
        entry["velocity"]       = s.velocity;
        entry["path_distance"]  = s.path_distance;
        arr.push_back(std::move(entry));
    }

    std::ofstream out(path);
    if (!out) {
        spdlog::error("[camera-path] Cannot open output file: {}", path);
        return false;
    }
    out << js.dump(2) << "\n";
    return true;
}

// ── CSV export ───────────────────────────────────────────────────────────────
bool write_csv(const std::string& path,
               const std::vector<PathSample>& samples) {
    std::ofstream out(path);
    if (!out) {
        spdlog::error("[camera-path] Cannot open output file: {}", path);
        return false;
    }

    // Header
    out << "frame,"
        << "pos_x,pos_y,pos_z,"
        << "rot_x,rot_y,rot_z,"
        << "zoom,fov_deg,"
        << "target_x,target_y,target_z,"
        << "velocity,path_distance\n";

    for (const auto& s : samples) {
        out << fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{}\n",
            s.frame,
            s.position.x, s.position.y, s.position.z,
            s.rotation.x, s.rotation.y, s.rotation.z,
            s.zoom, s.fov_deg,
            s.poi_enabled ? s.target.x : 0.0f,
            s.poi_enabled ? s.target.y : 0.0f,
            s.poi_enabled ? s.target.z : 0.0f,
            s.velocity, s.path_distance);
    }
    return true;
}

// ── Terminal summary ─────────────────────────────────────────────────────────
void print_summary(const std::string& comp_id,
                   const PathMetrics& metrics,
                   int start, int end, int step) {
    fmt::print("Camera path: {}\n", comp_id);
    fmt::print("  frames:        {} → {} (step {})\n", start, end, step);
    fmt::print("  samples:       {}\n", metrics.num_samples);
    fmt::print("  path length:   {:.2f}\n", metrics.total_length);
    fmt::print("  max velocity:  {:.4f}\n", metrics.max_velocity);
    fmt::print("  max accel:     {:.4f}\n", metrics.max_acceleration);
}

} // namespace

// ── command_camera_path ──────────────────────────────────────────────────────
int command_camera_path(const CompositionRegistry& registry,
                        const CameraPathArgs& args) {
    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) return 1;

    const auto& comp = *resolved.comp;
    const Frame start = args.start;
    const Frame end   = (args.end > 0) ? args.end : Frame{comp.duration() - 1};
    const int step    = std::max(1, args.step);

    if (start > end) {
        spdlog::error("[camera-path] --start ({}) must be <= --end ({})", start, end);
        return 1;
    }

    // ── Sample camera at each frame ─────────────────────────────────────────
    std::vector<PathSample> samples;
    samples.reserve(static_cast<size_t>((end - start) / step) + 1);

    for (Frame f = start; f <= end; f += Frame{step}) {
        Scene scene = comp.evaluate(f);

        const auto& cam = scene.camera_2_5d();
        PathSample s;
        s.frame      = f;
        s.position   = cam.position;
        s.rotation   = cam.rotation;
        s.zoom       = cam.zoom;
        s.fov_deg    = cam.fov_deg;
        s.poi_enabled = cam.point_of_interest_enabled;
        if (cam.point_of_interest_enabled) {
            s.target = cam.point_of_interest;
        }
        samples.push_back(s);
    }

    if (samples.empty()) {
        spdlog::warn("[camera-path] No samples generated (check frame range)");
        return 1;
    }

    // ── Compute metrics ─────────────────────────────────────────────────────
    PathMetrics metrics;
    compute_metrics(samples, metrics);

    // ── Print terminal summary ──────────────────────────────────────────────
    print_summary(args.comp_id, metrics, start, end, step);

    // ── Export if output path specified ──────────────────────────────────────
    if (!args.output.empty()) {
        // Detect format from extension or use explicit --format
        std::string fmt_str = args.format;
        if (fmt_str.empty() || fmt_str == "auto") {
            fmt_str = detect_format(args.output);
        }

        bool ok = false;
        if (fmt_str == "csv") {
            ok = write_csv(args.output, samples);
        } else {
            ok = write_json(args.output, args.comp_id, start, end, step, samples, metrics);
        }

        if (ok) {
            spdlog::info("[camera-path] Exported {} samples to {}", samples.size(), args.output);
        } else {
            return 1;
        }
    } else {
        // Default: print first/last sample positions to stdout
        fmt::print("  start pos:     ({:.1f}, {:.1f}, {:.1f})\n",
                    samples.front().position.x,
                    samples.front().position.y,
                    samples.front().position.z);
        if (samples.size() > 1) {
            fmt::print("  end pos:       ({:.1f}, {:.1f}, {:.1f})\n",
                        samples.back().position.x,
                        samples.back().position.y,
                        samples.back().position.z);
        }
        fmt::print("\nTip: use -o <path.json|csv> to export the full path.\n");
    }

    return 0;
}

} // namespace cli
} // namespace chronon3d
