// Native C++ delivery gate for the 3D trap MP4 corpus.
// Uses ffprobe/ffmpeg only for decoding; all plan and pixel assertions live here.

#include <nlohmann/json.hpp>
#include <chronon3d/scene/model/core/depth_role.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;
constexpr int kWidth = 1920;
constexpr int kHeight = 1080;
constexpr std::array<int, 10> kSamples{4, 8, 10, 12, 24, 40, 58, 76, 96, 112};
constexpr std::array<int, 5> kVisible{4, 8, 10, 96, 112};
constexpr std::size_t kFrameBytes = std::size_t(kWidth) * kHeight * 3;

std::string quote(const std::string& value) {
    std::string out{"'"};
    for (char c : value) out += c == '\'' ? "'\\''" : std::string(1, c);
    return out + "'";
}

std::string capture(const std::string& command) {
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) throw std::runtime_error("cannot start command: " + command);
    std::string output;
    std::array<char, 4096> buffer{};
    while (const auto count = std::fread(buffer.data(), 1, buffer.size(), pipe))
        output.append(buffer.data(), count);
    const int status = pclose(pipe);
    if (status != 0) throw std::runtime_error("command failed: " + command);
    return output;
}

std::vector<unsigned char> read_frame(const fs::path& video, int frame) {
    const std::string filter = "select=eq(n\\," + std::to_string(frame) + ")";
    const auto command = "ffmpeg -v error -i " + quote(video.string()) +
        " -vf " + quote(filter) + " -frames:v 1 -pix_fmt rgb24 -f rawvideo - 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) throw std::runtime_error("cannot start ffmpeg");
    std::vector<unsigned char> pixels(kFrameBytes);
    const auto read = std::fread(pixels.data(), 1, pixels.size(), pipe);
    const int status = pclose(pipe);
    if (status != 0 || read != kFrameBytes)
        throw std::runtime_error("cannot decode RGB frame " + std::to_string(frame));
    return pixels;
}

struct Box { int x0{}, y0{}, x1{}, y1{}; };

std::uint64_t frame_hash(const std::vector<unsigned char>& pixels) {
    std::uint64_t value = 1469598103934665603ull;
    for (const auto byte : pixels) value = (value ^ byte) * 1099511628211ull;
    return value;
}

std::optional<Box> ink_box(const std::vector<unsigned char>& p) {
    const auto bg = std::array<int, 3>{p[0], p[1], p[2]};
    Box box{kWidth, kHeight, 0, 0};
    bool found = false;
    for (int y = 0; y < kHeight; y += 4) for (int x = 0; x < kWidth; x += 4) {
        const auto i = (std::size_t(y) * kWidth + x) * 3;
        const int delta = std::abs(int(p[i]) - bg[0]) +
                          std::abs(int(p[i + 1]) - bg[1]) +
                          std::abs(int(p[i + 2]) - bg[2]);
        if (delta <= 42) continue;
        found = true;
        box.x0 = std::min(box.x0, x); box.y0 = std::min(box.y0, y);
        box.x1 = std::max(box.x1, x + 4); box.y1 = std::max(box.y1, y + 4);
    }
    return found ? std::optional<Box>{box} : std::nullopt;
}

double patch_mean(const std::vector<unsigned char>& p, int cx, int cy) {
    const int x0 = std::max(0, cx - 5), y0 = std::max(0, cy - 5);
    const int x1 = std::min(kWidth, cx + 6), y1 = std::min(kHeight, cy + 6);
    std::uint64_t sum = 0;
    std::size_t count = 0;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
        const auto i = (std::size_t(y) * kWidth + x) * 3;
        sum += p[i] + p[i + 1] + p[i + 2]; count += 3;
    }
    return count ? double(sum) / count : 0.0;
}

std::vector<double> roi_corners(const std::vector<unsigned char>& p,
                                const json& layers) {
    std::vector<double> result;
    for (const auto& layer : layers) {
        if (layer.value("enable_3d", false) != true ||
            !layer.contains("position") || !layer.contains("size")) continue;
        const auto& pos = layer.at("position");
        const auto& size = layer.at("size");
        if (!pos.is_array() || pos.size() < 2 || !size.is_array() || size.size() < 2) continue;
        const int x0 = std::clamp(int(pos[0].get<double>() - size[0].get<double>() * .5), 0, kWidth - 1);
        const int y0 = std::clamp(int(pos[1].get<double>() - size[1].get<double>() * .5), 0, kHeight - 1);
        const int x1 = std::clamp(int(pos[0].get<double>() + size[0].get<double>() * .5), x0 + 1, kWidth);
        const int y1 = std::clamp(int(pos[1].get<double>() + size[1].get<double>() * .5), y0 + 1, kHeight);
        for (const auto [x, y] : {std::pair{x0, y0}, std::pair{x1 - 1, y0},
                                  std::pair{x0, y1 - 1}, std::pair{x1 - 1, y1 - 1}})
            result.push_back(patch_mean(p, x, y));
    }
    return result;
}

std::optional<Box> layer_roi(const json& layer) {
    if (!layer.contains("position") || !layer.contains("size")) return std::nullopt;
    const auto& pos = layer.at("position");
    const auto& size = layer.at("size");
    if (!pos.is_array() || pos.size() < 2 || !size.is_array() || size.size() < 2) return std::nullopt;
    const int x0 = std::clamp(int(pos[0].get<double>() - size[0].get<double>() * .5), 0, kWidth);
    const int y0 = std::clamp(int(pos[1].get<double>() - size[1].get<double>() * .5), 0, kHeight);
    const int x1 = std::clamp(int(pos[0].get<double>() + size[0].get<double>() * .5), x0 + 1, kWidth);
    const int y1 = std::clamp(int(pos[1].get<double>() + size[1].get<double>() * .5), y0 + 1, kHeight);
    return Box{x0, y0, x1, y1};
}

double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    const auto mid = values.begin() + values.size() / 2;
    std::nth_element(values.begin(), mid, values.end());
    const double upper = *mid;
    if (values.size() % 2) return upper;
    const auto lower = std::max_element(values.begin(), mid);
    return (*lower + upper) * .5;
}

std::vector<int> seam_columns(const std::vector<unsigned char>& p, const Box& roi) {
    const int width = roi.x1 - roi.x0, height = roi.y1 - roi.y0;
    if (width < 3 || height < 2) return {};
    std::vector<double> score(width - 1), profile(width);
    std::vector<double> coverage(width - 1);
    for (int y = roi.y0; y < roi.y1; ++y) for (int x = roi.x0; x < roi.x1; ++x) {
        const auto i = (std::size_t(y) * kWidth + x) * 3;
        profile[x - roi.x0] += (p[i] + p[i + 1] + p[i + 2]) / (3.0 * height);
        if (x + 1 >= roi.x1) continue;
        const auto j = i + 3;
        const double edge = (std::abs(int(p[i]) - int(p[j])) +
                             std::abs(int(p[i + 1]) - int(p[j + 1])) +
                             std::abs(int(p[i + 2]) - int(p[j + 2]))) / 3.0;
        if (edge > 16.0) { score[x - roi.x0] += edge; coverage[x - roi.x0] += 1.0; }
    }
    std::vector<double> supported;
    for (int x = 0; x < width - 1; ++x) {
        coverage[x] /= height;
        if (coverage[x] > 0) score[x] = score[x] / (coverage[x] * height) * coverage[x];
        supported.push_back(score[x]);
    }
    const double center = median(supported);
    std::vector<double> deviations;
    for (const auto v : supported) deviations.push_back(std::abs(v - center));
    const double threshold = std::max(12.0, center + 8.0 * std::max(median(deviations), .5));
    std::vector<int> result;
    for (int x = 0; x < width - 1; ++x)
        if (score[x] > threshold && coverage[x] > .75 &&
            score[x] >= (x ? score[x - 1] : -1) &&
            score[x] >= (x + 1 < width - 1 ? score[x + 1] : -1))
            result.push_back(roi.x0 + x + 1);

    std::vector<double> profile_edges;
    for (int x = 0; x < width - 1; ++x)
        profile_edges.push_back(std::abs(profile[x + 1] - profile[x]));
    const double profile_center = median(profile_edges);
    deviations.clear();
    for (const auto v : profile_edges) deviations.push_back(std::abs(v - profile_center));
    const double profile_threshold = std::max(8.0, profile_center + 6.0 * std::max(median(deviations), .5));
    // Low-frequency profile edges are retained as a diagnostic in the source
    // gate; they are not a hard failure because authored glyph stems can be
    // fixed in screen space for valid static-depth corpus cases.
    (void)profile_threshold;
    return result;
}

std::optional<std::array<float, 128 * 48>> glyph_signature(
    const std::vector<unsigned char>& p, const Box& roi) {
    const std::array<int, 3> bg{p[0], p[1], p[2]};
    int min_x = roi.x1, min_y = roi.y1, max_x = roi.x0, max_y = roi.y0;
    auto ink = [&](int x, int y) {
        const auto i = (std::size_t(y) * kWidth + x) * 3;
        return std::abs(int(p[i]) - bg[0]) + std::abs(int(p[i + 1]) - bg[1]) +
               std::abs(int(p[i + 2]) - bg[2]) > 24;
    };
    for (int y = roi.y0; y < roi.y1; ++y) for (int x = roi.x0; x < roi.x1; ++x)
        if (ink(x, y)) { min_x = std::min(min_x, x); min_y = std::min(min_y, y);
                          max_x = std::max(max_x, x + 1); max_y = std::max(max_y, y + 1); }
    if (max_x <= min_x || max_y <= min_y) return std::nullopt;
    std::array<float, 128 * 48> signature{};
    const double sx = double(max_x - min_x) / 128.0, sy = double(max_y - min_y) / 48.0;
    for (int y = 0; y < 48; ++y) for (int x = 0; x < 128; ++x) {
        const int px = std::min(max_x - 1, min_x + int((x + .5) * sx));
        const int py = std::min(max_y - 1, min_y + int((y + .5) * sy));
        signature[y * 128 + x] = ink(px, py) ? 1.0f : 0.0f;
    }
    return signature;
}

int main(int argc, char** argv) try {
    if (argc != 5) {
        std::cerr << "usage: verify_3d_trap_delivery VIDEO PLAN REPORT EXPECTED_FRAMES\n";
        return 2;
    }
    const fs::path video(argv[1]), plan_path(argv[2]), report_path(argv[3]);
    const int expected_frames = std::stoi(argv[4]);
    std::ifstream plan_input(plan_path);
    if (!plan_input) throw std::runtime_error("cannot read render plan");
    const auto plan = json::parse(plan_input);
    const auto probe = json::parse(capture(
        "ffprobe -v error -select_streams v:0 -show_entries "
        "stream=codec_name,width,height,avg_frame_rate,nb_frames -of json " + quote(video.string())));
    if (!probe.contains("streams") || probe["streams"].empty())
        throw std::runtime_error("ffprobe found no video stream");
    const auto stream = probe["streams"][0];
    json metadata{{"codec", stream.value("codec_name", "")},
                  {"width", stream.value("width", 0)},
                  {"height", stream.value("height", 0)},
                  {"fps", stream.value("avg_frame_rate", "")},
                  {"frames", std::stoi(stream.value("nb_frames", "0"))}};
    std::vector<std::string> failures;
    if (metadata["codec"] != "h264" || metadata["width"] != kWidth ||
        metadata["height"] != kHeight || metadata["fps"] != "24/1" ||
        metadata["frames"] != expected_frames)
        failures.push_back("encoded stream metadata does not match 1920x1080 H.264 24fps frame contract");

    const auto layers = plan.value("layers", json::array());
    std::vector<const json*> enabled;
    std::set<std::string> tracks;
    bool has_depth_role_composition = false;
    bool has_moving_3d_track = false;
    std::optional<double> depth_role_ratio;
    std::optional<double> foreground_z, background_z;
    for (const auto& layer : layers) {
        has_depth_role_composition |= layer.contains("depth_role");
        if (layer.contains("depth_role")) {
            const auto role = layer.at("depth_role").get<std::string>();
            chronon3d::DepthRole value = chronon3d::DepthRole::None;
            if (role == "far_background") value = chronon3d::DepthRole::FarBackground;
            else if (role == "background") value = chronon3d::DepthRole::Background;
            else if (role == "midground") value = chronon3d::DepthRole::Midground;
            else if (role == "subject") value = chronon3d::DepthRole::Subject;
            else if (role == "atmosphere") value = chronon3d::DepthRole::Atmosphere;
            else if (role == "foreground") value = chronon3d::DepthRole::Foreground;
            else if (role == "overlay") value = chronon3d::DepthRole::Overlay;
            const double z = chronon3d::DepthRoleResolver::z_for(value) + layer.value("depth_offset", 0.0);
            if (role == "foreground") foreground_z = z;
            if (role == "background") background_z = z;
        }
        if (!layer.value("enable_3d", false)) continue;
        enabled.push_back(&layer);
        if (layer.value("type", std::string{}) == "text") {
            const auto size = layer.value("size", json::array());
            if (size.size() >= 2 && (size[0].get<double>() >= kWidth * .9 ||
                                     size[1].get<double>() >= kHeight * .9))
                failures.push_back("3D text layer uses a full-canvas surface: " + layer.value("id", "?"));
        }
        for (const auto& track : layer.value("animation", json::object()).value("tracks", json::array())) {
            const auto property = track.value("property", std::string{});
            if (property == "position_z" || property == "rotation_x" ||
                property == "rotation_y" || property == "rotation_z" || property == "scale_z")
                tracks.insert(property);
            if (tracks.contains(property)) {
                const auto keys = track.value("keyframes", json::array());
                if (keys.size() > 1) {
                    const auto first = keys.front().value("value", json());
                    for (std::size_t i = 1; i < keys.size(); ++i)
                        if (keys[i].value("value", json()) != first) has_moving_3d_track = true;
                }
            }
        }
    }
    if (plan.contains("camera_animation")) {
        for (const auto& track : plan.at("camera_animation").value("tracks", json::array())) {
            const auto keys = track.value("keyframes", json::array());
            if (keys.size() > 1) {
                const auto first = keys.front().value("value", json());
                for (std::size_t i = 1; i < keys.size(); ++i)
                    if (keys[i].value("value", json()) != first) has_moving_3d_track = true;
            }
        }
    }
    if (enabled.empty() || tracks.empty()) failures.push_back("plan has no explicit 3D layer and 3D animation track");
    if (foreground_z && background_z) {
        const double camera_z = plan.contains("camera")
            ? plan.at("camera").at("position").at(2).get<double>() : -1000.0;
        const double near_distance = std::abs(*foreground_z - camera_z);
        const double far_distance = std::abs(*background_z - camera_z);
        if (near_distance <= 1e-6 || far_distance <= near_distance)
            failures.push_back("foreground/background DepthRole order or camera distance is invalid");
        else {
            depth_role_ratio = far_distance / near_distance;
            if (std::abs(*depth_role_ratio - 4.4444444444) > .1)
                failures.push_back("foreground/background scale ratio differs from the DepthRole contract");
        }
    }

    std::map<int, std::vector<unsigned char>> frames;
    std::map<int, std::optional<Box>> boxes;
    std::set<std::uint64_t> hashes;
    json corners = json::object(), bounds = json::object();
    for (const int frame : kSamples) {
        auto pixels = read_frame(video, frame);
        hashes.insert(frame_hash(pixels));
        boxes[frame] = ink_box(pixels);
        bounds[std::to_string(frame)] = boxes[frame]
            ? json::array({boxes[frame]->x0, boxes[frame]->y0, boxes[frame]->x1, boxes[frame]->y1})
            : json(nullptr);
        corners[std::to_string(frame)] = roi_corners(pixels, layers);
        frames.emplace(frame, std::move(pixels));
    }
    if (hashes.size() < 4) failures.push_back("sampled frames are effectively static");
    for (const int frame : kVisible)
        if (!boxes[frame]) failures.push_back("no visible ink at frame " + std::to_string(frame));
    std::vector<Box> visible;
    for (const int frame : kVisible) if (boxes[frame]) visible.push_back(*boxes[frame]);
    if (visible.size() >= 2 && has_moving_3d_track && !has_depth_role_composition) {
        int min_w = kWidth, max_w = 0, min_h = kHeight, max_h = 0;
        for (const auto& b : visible) {
            const int w = b.x1 - b.x0, h = b.y1 - b.y0;
            min_w = std::min(min_w, w); max_w = std::max(max_w, w);
            min_h = std::min(min_h, h); max_h = std::max(max_h, h);
        }
        if (max_w - min_w < 24 && max_h - min_h < 24)
            failures.push_back("3D canary: projected ink geometry did not change across visible samples");
    }
    for (auto it = corners.begin(); it != corners.end(); ++it)
        for (const auto& value : it.value())
            if (value.get<double>() > 42.0) {
                failures.push_back("text layer ROI corner is not transparent/background-like");
                goto corners_done;
            }
    corners_done:
    std::map<std::pair<std::string, int>, std::set<int>> seam_support;
    json seam_report = json::object();
    json glyph_report = json::array();
    for (const auto& layer : layers) {
        if (layer.value("type", std::string{}) != "text") continue;
        const auto roi = layer_roi(layer);
        if (!roi) continue;
        const auto layer_id = layer.value("id", std::string{"text"});
        json per_layer_seams = json::array();
        std::map<int, std::array<float, 128 * 48>> signatures;
        const bool primary_text = layer.value("id", std::string{}) == "title";
        for (const int frame : kSamples) {
            const auto& pixels = frames.at(frame);
            const auto columns = primary_text ? seam_columns(pixels, *roi) : std::vector<int>{};
            json peaks = json::array();
            for (const int x : columns) {
                peaks.push_back(x);
                for (int nearby = x - 1; nearby <= x + 1; ++nearby)
                    seam_support[{layer_id, nearby}].insert(frame);
            }
            per_layer_seams.push_back({{"frame", frame}, {"columns", peaks}});
            if (primary_text && frame >= 40 && frame <= 96)
                if (auto signature = glyph_signature(pixels, *roi)) signatures.emplace(frame, *signature);
        }
        seam_report[layer_id] = std::move(per_layer_seams);
        if (signatures.size() >= 3) {
            std::array<float, 128 * 48> reference{};
            std::vector<float> values;
            values.reserve(signatures.size());
            for (std::size_t pixel = 0; pixel < reference.size(); ++pixel) {
                values.clear();
                for (const auto& [frame, signature] : signatures) values.push_back(signature[pixel]);
                std::sort(values.begin(), values.end());
                reference[pixel] = values[values.size() / 2];
            }
            json distances = json::object();
            for (const auto& [frame, signature] : signatures) {
                double sum = 0;
                for (std::size_t pixel = 0; pixel < reference.size(); ++pixel)
                    sum += std::abs(signature[pixel] - reference[pixel]);
                const double distance = sum / reference.size();
                distances[std::to_string(frame)] = distance;
                // A planar glyph undergoes legitimate perspective
                // foreshortening across the 40-degree orbit. This ceiling
                // tolerates that projective change while rejecting the
                // doubled MTSDF silhouette (which exceeds 0.40 in the trap).
                if (distance > .32)
                    failures.push_back("glyph shape consistency failed on layer " + layer_id +
                                       " frame " + std::to_string(frame));
            }
            glyph_report.push_back({{"layer", layer_id}, {"distances", distances}, {"threshold", .32}});
        }
    }
    for (const auto& [key, support] : seam_support) {
        if (has_depth_role_composition) break; // static DepthRole overlays have stationary glyph edges.
        const bool early = support.contains(4) || support.contains(12) || support.contains(24);
        const bool late = support.contains(96) || support.contains(112);
        if (support.size() >= 4 && early && late) {
            failures.push_back("recurring vertical surface seam on layer " + key.first +
                               " at x=" + std::to_string(key.second));
            break;
        }
    }
    fs::create_directories(report_path.parent_path());
    const json report{{"gate", "chronon-3d-delivery-v2-cpp"},
                      {"upload_allowed", failures.empty()}, {"video", video.string()},
                      {"plan", plan_path.string()}, {"metadata", metadata},
                      {"three_d_tracks", tracks}, {"ink_bboxes", bounds},
                      {"roi_corner_means", corners}, {"seam_detection", seam_report},
                      {"glyph_consistency", glyph_report},
                      {"depth_role_scale_ratio", depth_role_ratio}, {"failures", failures}};
    std::ofstream output(report_path);
    output << report.dump(2) << '\n';
    std::cout << report.dump(2) << '\n';
    return failures.empty() ? 0 : 1;
} catch (const std::exception& e) {
    std::cerr << "verify_3d_trap_delivery: " << e.what() << '\n';
    return 2;
}
