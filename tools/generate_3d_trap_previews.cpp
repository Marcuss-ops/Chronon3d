#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

struct Case {
    std::string id;
    std::string title;
    std::string caption;
};

const std::vector<Case> kCases{
    {"A1_dolly_position_z", "DOLLY", "Layer z: -600 to 0"},
    {"A2_orbit_rotation_y", "ORBIT Y", "Rotation: -22 to 0 degrees"},
    {"A3_tilt_rotation_x", "TILT X", "Rotation: 15 to 0 degrees"},
    {"A4_roll_rotation_z", "ROLL Z", "Rotation: 0 to 10 degrees"},
    {"A5_z_sign_depth", "Z SIGN", "Negative z near; positive z far"},
    {"B1_depth_role_resolution", "DEPTH ROLE", "Foreground and background scale"},
    {"B2_depth_crossing_sort", "CROSSING", "Layer order swaps at z=0"},
    {"B3_overlap_alpha", "ALPHA OVERLAP", "Two text layers share the same region"},
    {"C1_camera_dolly", "CAMERA DOLLY", "Camera moves; layers stay still"},
    {"C2_camera_pan_tilt_degrees", "CAMERA PAN / TILT", "Angles use degrees"},
    {"C3_parallax", "PARALLAX", "Near layer moves more than far layer"},
    {"D1_typewriter_orbit", "TYPEWRITER ORBIT", "Glyphs appear during rotation"},
    {"D2_multi_font_depth", "MULTI-FONT", "Three font and depth combinations"},
    {"D3_mtsdf_edge_stress", "MTSDF EDGE", "Thin stroke during 40 degree orbit"},
    {"D4_glow_projection", "GLOW / 3D", "Glow follows the projected text"},
    {"D5_tight_surface_parity", "TIGHT / FULL", "Tight surface versus full canvas"},
    {"E1_text_surface_reuse", "SURFACE REUSE", "Warm and cold frames match across depth"},
};

json track(const std::string& property,
           const std::vector<std::pair<int, double>>& keys,
           const std::string& easing = "linear") {
    json frames = json::array();
    for (const auto& [frame, value] : keys)
        frames.push_back({{"frame", frame}, {"value", value}});
    return {{"property", property}, {"easing", easing}, {"keyframes", frames}};
}

json vector_track(const std::string& property,
                  const std::vector<std::pair<int, std::vector<double>>>& keys) {
    json frames = json::array();
    for (const auto& [frame, value] : keys)
        frames.push_back({{"frame", frame}, {"value", value}});
    return {{"property", property}, {"easing", "linear"}, {"keyframes", frames}};
}

json& layer(json& plan, const std::string& id) {
    for (auto& item : plan.at("layers"))
        if (item.value("id", std::string{}) == id) return item;
    throw std::runtime_error("missing layer: " + id);
}

void set_tracks(json& target, json tracks) {
    target["animation"] = {{"tracks", std::move(tracks)}};
}

void set_text(json& target, const std::string& value) {
    target["text"] = value;
}

void show_all_glyphs(json& target) {
    if (!target.contains("text_animators")) return;
    for (auto& animator : target["text_animators"])
        for (auto& selector : animator["selectors"]) {
            if (selector.contains("start"))
                selector["start"]["keyframes"] = json::array({json{{"frame", 0}, {"value", 0}}});
            if (selector.contains("end"))
                selector["end"]["keyframes"] = json::array({json{{"frame", 0}, {"value", 100}}});
        }
}

void replace_track(json& target, const std::string& property,
                   const std::string& new_property,
                   const std::vector<std::pair<int, double>>& keys) {
    auto& tracks = target["animation"]["tracks"];
    for (auto& item : tracks) {
        if (item.value("property", std::string{}) == property) {
            item = track(new_property, keys, item.value("easing", "linear"));
            return;
        }
    }
    tracks.push_back(track(new_property, keys));
}

void set_depth(json& target, const std::vector<std::pair<int, double>>& keys) {
    replace_track(target, "position_z", "position_z", keys);
}

void set_rotation(json& target, const std::string& property,
                  const std::vector<std::pair<int, double>>& keys) {
    replace_track(target, "rotation_y", property, keys);
}

void setup_labels(json& plan, const Case& item) {
    auto& title = layer(plan, "title");
    auto& subtitle = layer(plan, "subtitle");
    set_text(title, "CHRONON");
    set_text(subtitle, item.id.substr(0, 2));
    subtitle["position"][1] = 930;
    // Keep the corpus orthogonal: D4 is the dedicated glow trap. Applying the
    // base-plan glow to every case obscures shape assertions and makes ordinary
    // text look doubled while it is projected.
    title["style"].erase("glow");
    subtitle["style"].erase("glow");
    // Bound projected ink at the closest authored depth so it stays inside
    // its per-layer surface throughout motion instead of clipping at the ROI.
    title["size"] = {1700, 480};
    title["style"]["font_size"] = 110;
    title["rotation"] = {0, 0.1, 0};
    subtitle["rotation"] = {0, 0.1, 0};
    show_all_glyphs(title);
    show_all_glyphs(subtitle);
}

void configure(json& plan, const Case& item) {
    auto& title = layer(plan, "title");
    auto& subtitle = layer(plan, "subtitle");
    const std::vector<std::pair<int, double>> zero{{0, 0.1}, {10, 0.1}, {96, 0.1}, {112, 0.1}, {119, 0.1}};
    if (item.id.starts_with("A1")) {
        set_depth(title, {{0, -600}, {10, 0}, {96, 0}, {112, -600}, {119, -600}});
        replace_track(title, "rotation_y", "rotation_y", zero);
    } else if (item.id.starts_with("E1")) {
        set_text(title, "CHRONON");
        set_depth(title, {{0, -600}, {10, -200}, {48, 0}, {96, 300}, {112, 600}, {119, 600}});
        replace_track(title, "rotation_y", "rotation_y", zero);
    } else if (item.id.starts_with("A2")) {
        set_depth(title, zero);
        set_rotation(title, "rotation_y", {{0, -22}, {10, 0.1}, {96, 0.1}, {112, 22}, {119, 22}});
    } else if (item.id.starts_with("A3")) {
        set_depth(title, zero);
        set_rotation(title, "rotation_x", {{0, 15}, {10, 0}, {96, 0}, {112, -15}, {119, -15}});
    } else if (item.id.starts_with("A4")) {
        set_depth(title, zero);
        set_rotation(title, "rotation_z", {{0, 0}, {8, 10}, {96, 10}, {112, 0}, {119, 0}});
    } else if (item.id.starts_with("A5")) {
        set_depth(title, {{0, -200}, {10, -200}, {96, 200}, {112, 200}, {119, 200}});
        replace_track(title, "rotation_y", "rotation_y", zero);
    } else if (item.id.starts_with("B1")) {
        title["depth_role"] = "foreground";
        title["depth_offset"] = 0;
        title["style"]["font_size"] = 90;
        subtitle["depth_role"] = "background";
        subtitle["depth_offset"] = 0;
        subtitle["style"]["font_size"] = 90;
        // The two role scales differ by 4.44×. Separate their projected
        // bounds so the foreground title cannot occlude the background tag.
        subtitle["position"][1] = 930;
        auto& title_tracks = title["animation"]["tracks"];
        auto& subtitle_tracks = subtitle["animation"]["tracks"];
        title_tracks.erase(std::remove_if(title_tracks.begin(), title_tracks.end(), [](const json& item) {
            return item.value("property", std::string{}) == "position_z";
        }), title_tracks.end());
        subtitle_tracks.erase(std::remove_if(subtitle_tracks.begin(), subtitle_tracks.end(), [](const json& item) {
            return item.value("property", std::string{}) == "position_z";
        }), subtitle_tracks.end());
        replace_track(title, "rotation_y", "rotation_y", zero);
        replace_track(subtitle, "rotation_y", "rotation_y", zero);
    } else if (item.id.starts_with("B2")) {
        set_text(subtitle, "B2");
        set_depth(title, {{0, -300}, {8, 0}, {10, 75}, {96, 300}, {112, 300}, {119, 300}});
        replace_track(title, "rotation_y", "rotation_y", zero);
    } else if (item.id.starts_with("B3")) {
        subtitle["position"] = title["position"];
    title["style"]["fill"] = "#FFFFFF";
    subtitle["style"]["fill"] = "#FF4DB8";
    } else if (item.id.starts_with("C1")) {
        set_depth(title, zero);
        set_depth(subtitle, zero);
        replace_track(title, "rotation_y", "rotation_y", zero);
        replace_track(subtitle, "rotation_y", "rotation_y", zero);
        plan["camera"] = {{"type", "perspective"}, {"position", {0, 0, -1000}},
                           {"rotation_deg", {0, 0, 0}}, {"fov_deg", 50}};
        plan["camera_animation"] = {{"tracks", json::array({
            track("camera_position_z", {{0, -1000}, {10, -1400}, {96, -1400}, {112, -1000}, {119, -1000}})})}};
    } else if (item.id.starts_with("C2")) {
        set_text(title, "NEAR");
        set_text(subtitle, "FAR");
        title["position"] = {650, 520};
        subtitle["position"] = {1270, 520};
        title["size"] = {700, 320};
        subtitle["size"] = {500, 320};
        set_depth(title, {{0, -350}, {10, -350}, {96, -350}, {112, -350}, {119, -350}});
        set_depth(subtitle, {{0, 350}, {10, 350}, {96, 350}, {112, 350}, {119, 350}});
        replace_track(title, "rotation_y", "rotation_y", zero);
        replace_track(subtitle, "rotation_y", "rotation_y", zero);
        plan["camera"] = {{"type", "perspective"}, {"position", {0, 0, -1000}},
                           {"rotation_deg", {0, 0, 0}}, {"fov_deg", 50}};
        plan["camera_animation"] = {{"tracks", json::array({
            track("camera_position_x", {{0, -120}, {10, 0}, {96, 0}, {112, 120}, {119, 120}}),
            track("camera_rotation_y", {{0, -8}, {10, 0}, {96, 0}, {112, 8}, {119, 8}}),
            track("camera_rotation_x", {{0, 6}, {10, 0}, {96, 0}, {112, -6}, {119, -6}})})}};
    } else if (item.id.starts_with("C3")) {
        plan["camera"] = {{"type", "perspective"}, {"position", {0, 0, -1200}},
                           {"rotation_deg", {0, 0, 0}}, {"fov_deg", 50}};
        plan["camera_animation"] = {{"tracks", json::array({
            track("camera_position_z", {{0, -1200}, {10, -1600}, {96, -1600}, {112, -1200}, {119, -1200}})})}};
        set_depth(title, {{0, -500}, {10, -500}, {96, -500}, {112, -500}, {119, -500}});
        set_depth(subtitle, {{0, 500}, {10, 500}, {96, 500}, {112, 500}, {119, 500}});
        replace_track(title, "rotation_y", "rotation_y", zero);
        replace_track(subtitle, "rotation_y", "rotation_y", zero);
    } else if (item.id.starts_with("D1")) {
        set_rotation(title, "rotation_y", {{0, -35}, {10, 0.1}, {96, 0.1}, {112, 35}, {119, 35}});
        json selector = json::object();
        selector["id"] = "glyphs";
        selector["unit"] = "glyph";
        selector["shape"] = "smooth";
        selector["order"] = "forward";
        selector["combine"] = "replace";
        selector["exclude_spaces"] = true;
        selector["start"]["keyframes"] = json::array({json{{"frame", 0}, {"value", 0}}});
        selector["end"]["keyframes"] = json::array({json{{"frame", 0}, {"value", 0}},
                                                        json{{"frame", 10}, {"value", 100}}});
        json opacity = json::object();
        opacity["property"] = "opacity";
        opacity["keyframes"] = json::array({json{{"frame", 0}, {"value", 1}},
                                             json{{"frame", 10}, {"value", 1}}});
        title["text_animators"] = json::array({json{{"id", "trap_typewriter"},
            {"selectors", json::array({selector})}, {"properties", json::array({opacity})}}});
    } else if (item.id.starts_with("D2")) {
        set_depth(title, {{0, -350}, {10, -350}, {96, -350}, {112, -350}, {119, -350}});
        set_depth(subtitle, {{0, 350}, {10, 350}, {96, 350}, {112, 350}, {119, 350}});
        replace_track(title, "rotation_y", "rotation_y", zero);
        replace_track(subtitle, "rotation_y", "rotation_y", zero);
        title["style"]["font"] = "assets/fonts/Inter-Bold.ttf";
        subtitle["style"]["font"] = "assets/fonts/Inter-Regular.ttf";
    } else if (item.id.starts_with("D3")) {
        set_text(title, "MTSDF EDGE");
        title["style"]["stroke"]["width"] = 0.5;
        set_rotation(title, "rotation_y", {{0, -45}, {10, -40}, {96, 40}, {112, 45}, {119, 45}});
    } else if (item.id.starts_with("D4")) {
        title["style"]["glow"] = {{"radius", 8}, {"intensity", 0.18}, {"threshold", 0},
            {"falloff", 1}, {"core_strength", 0.32}, {"aura_strength", 0.16},
            {"bloom_strength", 0.04}, {"high_quality", true}};
        set_rotation(title, "rotation_y", {{0, -25}, {10, 0.1}, {96, 0.1}, {112, 25}, {119, 25}});
    } else if (item.id.starts_with("D5")) {
        title["size"] = {1500, 260};
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: generate_3d_trap_previews <base-plan.json> <output-dir>\n";
        return 2;
    }
    try {
        const fs::path base_path(argv[1]);
        const fs::path output_dir(argv[2]);
        fs::create_directories(output_dir);
        std::ifstream input(base_path);
        if (!input) throw std::runtime_error("cannot read base plan: " + base_path.string());
        const json base = json::parse(input);
        for (const auto& item : kCases) {
            json plan = base;
            plan["job_id"] = "chronon_trap_preview_" + item.id;
            plan["output"]["path"] = item.id + "_preview.mp4";
            plan.erase("camera");
            plan.erase("camera_animation");
            setup_labels(plan, item);
            configure(plan, item);
            std::ofstream output(output_dir / (item.id + ".plan.json"));
            if (!output) throw std::runtime_error("cannot write plan for " + item.id);
            output << plan.dump(2) << '\n';
        }
        std::cout << "generated " << kCases.size() << " C++ preview plans\n";
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
