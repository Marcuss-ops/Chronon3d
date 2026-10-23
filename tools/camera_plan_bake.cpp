#include <nlohmann/json.hpp>

#include <chrononmotion/math/Euler.hpp>
#include <chrononmotion/motion/CameraRig.hpp>
#include <chrononmotion/motion/RenderPlanLowering.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

using Json = nlohmann::ordered_json;
using namespace chrononmotion;
using namespace chrononmotion::motion;

namespace {
Easing easing(const std::string& name) {
    constexpr float third = 1.0f / 3.0f;
    constexpr float twoThirds = 2.0f / 3.0f;
    if (name.empty() || name == "linear")
        return Easing::cubicBezier(third, third, twoThirds, twoThirds);
    if (name == "in_out_cubic") return Easing::linear(); // Expanded into two exact cubic segments below.
    if (name == "in_cubic") return Easing::cubicBezier(third, 0.f, twoThirds, 0.f);
    if (name == "out_cubic") return Easing::cubicBezier(third, 1.f, twoThirds, 1.f);
    if (name == "smoothstep") return Easing::cubicBezier(third, 0.f, twoThirds, 1.f);
    if (name == "out_expo") return Easing::expoOut();
    throw std::runtime_error("unsupported camera easing: " + name);
}

void addTrack(Track<float>& track, const Json& keys, const std::string& curve, float fps) {
    const Easing curveEasing = easing(curve);
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const float time = keys[i].at("frame").get<float>() / fps;
        const float value = keys[i].at("value").get<float>();
        track.add(time, value, curveEasing);
        if (curve == "in_out_cubic" && i + 1 < keys.size()) {
            const float nextTime = keys[i + 1].at("frame").get<float>() / fps;
            const float nextValue = keys[i + 1].at("value").get<float>();
            const float midpoint = (time + nextTime) * 0.5f;
            const float middleValue = (value + nextValue) * 0.5f;
            track.add(time, value, Easing::cubicBezier(1.f/3.f, 0.f, 2.f/3.f, 0.f));
            track.add(midpoint, middleValue, Easing::cubicBezier(1.f/3.f, 1.f, 2.f/3.f, 1.f));
        }
    }
}

void bake(Json& plan) {
    if (!plan.contains("camera_animation")) return;
    if (!plan.contains("camera") || !plan.at("camera").is_object())
        throw std::runtime_error("camera_animation requires a camera object");
    const auto& canvas = plan.at("canvas");
    const float fpsNum = canvas.value("fps_num", 30.f);
    const float fpsDen = canvas.value("fps_den", 1.f);
    const float fps = fpsNum / fpsDen;
    const int frames = canvas.at("duration_frames").get<int>();
    if (frames < 1 || frames > 65536 || !std::isfinite(fps) || fps <= 0.f)
        throw std::runtime_error("invalid canvas timing");

    const Json base = plan.at("camera");
    const auto pos = base.value("position", Json::array({0,0,-1000}));
    const auto rot = base.value("rotation_deg", Json::array({0,0,0}));
    std::unordered_map<std::string, Track<float>> tracks;
    for (const auto& row : plan.at("camera_animation").at("tracks")) {
        const auto prop = row.at("property").get<std::string>();
        const auto curve = row.value("easing", std::string("linear"));
        addTrack(tracks[prop], row.at("keyframes"), curve, fps);
    }
    auto sample = [&](const std::string& prop, int frame, float fallback) {
        const auto it = tracks.find(prop);
        return it == tracks.end() ? fallback : it->second.sample(frame / fps);
    };

    const auto cameraType = base.value("type", std::string("perspective"));
    if (cameraType != "perspective" && cameraType != "orthographic")
        throw std::runtime_error("unsupported camera type: " + cameraType);
    CameraRig rig(1, base.value("fov_deg", 50.f),
                  canvas.at("width").get<float>() / canvas.at("height").get<float>(),
                  base.value("near", 1.f), base.value("far", 10000.f));
    const auto lensShift = base.value("lens_shift", Json::array({0,0}));
    rig.setSensorWidth(base.value("sensor_width_mm", 36.f))
       .setLensShift(Vector2(lensShift[0].get<float>(), lensShift[1].get<float>()));
    const Json dof = base.value("dof", Json::object());
    if (dof.value("enabled", false)) {
        rig.setFocusDistance(dof.value("focus_distance", 0.f))
           .setMaxBlur(dof.value("max_blur", 0.f))
           // Single authored aperture truth is f/N: authored `f_stop` wins,
           // legacy gain-domain `aperture` is bridged through fStopFromBlurGain.
           .setFStop(dof.contains("f_stop") ? dof.at("f_stop").get<float>()
                                            : fStopFromBlurGain(dof.value("aperture", 0.f)));
    }

    // RenderPlan mirrors world Z; CM3D lowering reflects Z and conjugates rotations.
    const float baseX = pos[0].get<float>(), baseY = pos[1].get<float>(), baseZ = -pos[2].get<float>();
    const float rx = rot[0].get<float>(), ry = rot[1].get<float>(), rz = rot[2].get<float>();
    const bool perspective = cameraType == "perspective";
    for (int frame = 0; frame < frames; ++frame) {
        const float x = sample("camera_position_x", frame, baseX);
        const float y = sample("camera_position_y", frame, baseY);
        const float z = -sample("camera_position_z", frame, -baseZ);
        const float ax = sample("camera_rotation_x", frame, rx) * float(M_PI/180.0);
        const float ay = sample("camera_rotation_y", frame, ry) * float(M_PI/180.0);
        const float az = sample("camera_rotation_z", frame, rz) * float(M_PI/180.0);
        const float zoom = sample("camera_zoom", frame, base.value("zoom", 1.f));
        const float fov = sample("camera_fov_deg", frame, base.value("fov_deg", 50.f));
        Quaternion q; q.setFromEuler(Euler(-ax, -ay, az));
        const float time = frame / fps;
        rig.positionTrack().add(time, Vector3(x,y,z));
        rig.orientationTrack().add(time, q);
        const float rigFov = perspective
            ? 2.f * std::atan(std::tan(fov * float(M_PI/360.0)) / zoom) * float(180.0/M_PI)
            : fov;
        rig.fovTrack().add(time, rigFov);
    }

    auto baked = cameraSamplesToJson(bakeResolvedCameraSamples(rig, fps, frames,
        base.value("near", 1.f), base.value("far", 10000.f)));
    Json samples = Json::array();
    for (int frame = 0; frame < frames; ++frame) {
        Json camera = base;
        const Json& resolved = baked.at(frame).at("camera");
        for (const auto* key : {"position", "rotation_deg", "fov_deg", "near", "far",
                                 "sensor_width_mm", "lens_shift"}) {
            if (resolved.contains(key)) camera[key] = resolved.at(key);
        }
        camera["type"] = cameraType;
        camera["zoom"] = perspective ? 1.f : sample("camera_zoom", frame, base.value("zoom", 1.f));
        samples.push_back(Json{{"frame", frame}, {"camera", std::move(camera)}});
    }
    plan["camera"] = samples.front().at("camera");
    plan["camera_samples"] = std::move(samples);
    plan.erase("camera_animation");
}
}

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: chronon3d_camera_plan_bake <plan.json>\n"; return 2; }
    try {
        std::ifstream in(argv[1]);
        if (!in) throw std::runtime_error("cannot open input plan");
        Json plan; in >> plan; bake(plan);
        std::ofstream out(argv[1]);
        if (!out) throw std::runtime_error("cannot open output plan");
        out << plan.dump(2) << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "camera_plan_bake: " << e.what() << '\n';
        return 1;
    }
}
