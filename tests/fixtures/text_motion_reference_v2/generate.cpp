#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
using J = nlohmann::json;
namespace fs = std::filesystem;
const int W = 1920, H = 1080, FPS = 30;
// SF Pro's Apple license excludes general motion-graphics exports. DM Sans is
// the closest bundled, redistributable family available in this repository.
const std::string FONT = "assets/fonts/DMSans-Regular.ttf";
J kf(std::initializer_list<std::pair<int, double>> p) {
    J a = J::array();
    for (auto [f, v] : p)
        a.push_back({{"frame", f}, {"value", v}});
    return a;
}
J track(std::string prop, std::initializer_list<std::pair<int, double>> p,
        std::string ease = "in_out_cubic") {
    J frames = kf(p);
    if (prop == "blur")
        for (auto &frame : frames)
            frame["value"] = frame["value"].get<double>() * 2.0;
    return {{"property", prop}, {"easing", ease}, {"keyframes", frames}};
}
J selector(std::string unit = "line", std::string order = "forward", bool random = false,
           int seed = 0) {
    J s = {{"unit", unit},
           {"shape", "square"},
           {"order", order},
           {"combine", "replace"},
           {"exclude_spaces", true}};
    if (random) {
        s["randomize_order"] = true;
        s["random_seed"] = seed;
    }
    return s;
}
J anim(std::string id, J sel, J props) {
    return {{"id", id}, {"selectors", J::array({sel})}, {"properties", props}};
}
J text(std::string id, std::string value, int size = 76, std::string color = "#F4F5F7",
       J anims = J::array(), int start = 0, int dur = 60, int y = 270) {
    if (!anims.is_array())
        anims = J::array();
    return {{"id", id},
            {"type", "text"},
            {"text", value},
            {"size", {1800, 560}},
            {"position", {960, y * 2}},
            {"style",
             {{"font", FONT},
              {"font_size", size * 2},
              {"fill", color},
              {"glow", {{"radius", 24}, {"intensity", 0.72}, {"color", "#91CFFF"}}}}},
            {"start_frame", start},
            {"duration_frames", dur},
            {"text_animators", anims}};
}
J opacity_anim(std::string id, int a, int b, int end) {
    return anim(id, selector(),
                J::array({track("opacity", {{0, 0}, {a, 0}, {b, 1}, {end, 1}}, "out_cubic"),
                          track("blur", {{0, 16}, {a, 16}, {b, 0}, {end, 0}}, "out_cubic")}));
}
J reveal(std::string unit, int end, int dur, std::string order = "forward", bool random = false,
         int seed = 0, int exit_frame = -1) {
    J s = selector(unit, order, random, seed);
    s["start"] = {{"keyframes", kf({{0, 0}, {end, 100}, {dur - 1, 100}})}, {"easing", "linear"}};
    s["end"] = {{"keyframes", kf({{0, 100}, {dur - 1, 100}})}, {"easing", "linear"}};
    J a =
        anim("reveal", s,
             J::array({track("opacity", {{0, 0}, {dur - 1, 0}}, "linear"),
                       track("blur",
                             {{0, 16}, {end / 2, 9}, {std::min(dur - 1, end + 5), 0}, {dur - 1, 0}},
                             "out_cubic")}));
    J arr = J::array({a});
    if (exit_frame >= 0) {
        const int erase_end = std::min(dur - 1, exit_frame + 18);
        J erase = selector("grapheme", "forward");
        erase["start"] = {
            {"keyframes", kf({{0, 100}, {exit_frame, 100}, {erase_end, 0}, {dur - 1, 0}})},
            {"easing", "linear"}};
        erase["end"] = {{"keyframes", kf({{0, 100}, {dur - 1, 100}})}, {"easing", "linear"}};
        arr.push_back(anim("erase_from_right", erase,
                           J::array({track("opacity", {{0, 0}, {dur - 1, 0}}, "linear")})));
    }
    return arr;
}
J layer_text(std::string id, std::string value, int size, int dur, J anims = J::array(),
             int start = 0, std::string color = "#F4F5F7", J motion = J::array(), int y = 270) {
    J l = text(id, value, size, color, anims, start, dur, y);
    if (!motion.empty())
        l["animation"] = {{"tracks", motion}};
    return l;
}
struct Case {
    int n, dur, src;
    std::string name;
    J layers;
};
int main(int argc, char **argv) {
    fs::path here = argc > 1 ? argv[1] : "Chronon3d/tests/fixtures/text_motion_reference_v2";
    fs::create_directories(here / "plans");
    std::vector<Case> c;
    c.push_back({1, 60, 0, "motion_bear_presents",
                 J::array({layer_text("motion_bear", "Motion Bear", 118, 60,
                                      reveal("grapheme", 13, 60, "forward", false, 0, 27)),
                           layer_text("presents", "Presents", 76, 60,
                                      J::array({opacity_anim("presents_focus", 29, 39, 59)}), 0,
                                      "#BDA7FF")})});
    c.push_back(
        {2, 96, 60, "text_animations_unfold",
         J::array(
             {layer_text("text_line", "Text", 78, 96,
                         J::array({anim(
                             "text_focus", selector(),
                             J::array({track("opacity", {{0, 0}, {8, 1}, {95, 1}}, "out_cubic"),
                                       track("blur", {{0, 12}, {11, 0}, {95, 0}}, "out_cubic")}))}),
                         0, "#F4F5F7", J::array(), 214),
              layer_text(
                  "animations_line", "Animations", 78, 96, opacity_anim("unfold_focus", 8, 18, 95),
                  0, "#F4F5F7",
                  J::array({track(
                      "scale_y",
                      {{0, .01}, {3, .06}, {13, .55}, {22, 1.06}, {28, 1}, {78, .72}, {95, .72}},
                      "out_cubic")}),
                  326)})});
    c.push_back({3, 72, 156, "animate_your_text_effortlessly",
                 J::array({layer_text("phrase", "Animate your text effortlessly", 63, 72,
                                      reveal("word", 20, 72, "forward", false, 0, 49))})});
    c.push_back(
        {4, 108, 228, "save_time_boost_workflow",
         J::array(
             {layer_text("save", "Save time,", 82, 108,
                         J::array({anim(
                             "save_out", selector(),
                             J::array({track("opacity", {{0, 1}, {12, 1}, {21, 0}, {107, 0}}),
                                       track("blur", {{0, 0}, {12, 0}, {21, 14}, {107, 14}})}))})),
              layer_text(
                  "boost", "boost your", 82, 108,
                  J::array({anim(
                      "boost_in_out", selector(),
                      J::array(
                          {track("opacity", {{0, 0}, {15, 0}, {24, 1}, {42, 1}, {51, 0}, {107, 0}}),
                           track("blur",
                                 {{0, 14}, {15, 14}, {24, 0}, {42, 0}, {51, 14}, {107, 14}})}))}),
                  0, "#BBA3FF"),
              layer_text("workflow", "workflow", 94, 108,
                         J::array({anim(
                             "workflow_in", selector(),
                             J::array({track("opacity", {{0, 0}, {47, 0}, {57, 1}, {107, 1}}),
                                       track("blur", {{0, 14}, {47, 14}, {57, 0}, {107, 0}})}))}),
                         0, "#F4F5F7",
                         J::array({track("scale", {{0, .86}, {56, 1.08}, {66, 1}, {107, 1}},
                                         "out_cubic")}))})});
    c.push_back({5, 72, 336, "supports_any_language",
                 J::array({layer_text("supports", "Supports any language.", 72, 72,
                                      reveal("word", 18, 72, "reverse"))})});
    c.push_back({6, 72, 408, "random_4k_resolution",
                 J::array({layer_text("resolution", "4K Resolution", 76, 72,
                                      reveal("glyph", 22, 72, "random", true, 1947))})});
    c.push_back({7, 72, 480, "random_no_plugins",
                 J::array({layer_text("plugins", "No Plugins", 78, 72,
                                      reveal("glyph", 19, 72, "random", true, 2718, 47))})});
    c.push_back(
        {8, 72, 552, "easily_adjust_duration",
         J::array(
             {layer_text(
                  "easily", "Easily", 92, 72,
                  J::array({anim(
                      "easily_in_out", selector(),
                      J::array({track("opacity", {{0, 0}, {8, 1}, {25, 1}, {34, 0}, {71, 0}}),
                                track("blur", {{0, 14}, {8, 0}, {25, 0}, {34, 14}, {71, 14}})}))})),
              layer_text(
                  "adjust", "adjust duration", 70, 72,
                  J::array({anim(
                      "adjust_in_out", selector(),
                      J::array(
                          {track("opacity", {{0, 0}, {34, 0}, {43, 1}, {59, 1}, {71, 0}}),
                           track("blur", {{0, 14}, {34, 14}, {43, 0}, {59, 0}, {71, 14}})}))}))})});
    c.push_back(
        {9, 96, 624, "video_tutorial_included",
         J::array(
             {layer_text("video", "Video", 82, 96,
                         J::array({anim(
                             "video_out", selector(),
                             J::array({track("opacity", {{0, 1}, {12, 1}, {22, 0}, {95, 0}}),
                                       track("blur", {{0, 0}, {12, 0}, {22, 14}, {95, 14}})}))})),
              layer_text(
                  "tutorial", "Tutorial", 82, 96,
                  J::array({anim(
                      "tutorial_in_out", selector(),
                      J::array(
                          {track("opacity", {{0, 0}, {17, 0}, {27, 1}, {38, 1}, {48, 0}, {95, 0}}),
                           track("blur",
                                 {{0, 14}, {17, 14}, {27, 0}, {38, 0}, {48, 14}, {95, 14}})}))}),
                  0, "#B99FFF"),
              layer_text(
                  "included", "Included", 82, 96,
                  J::array({anim(
                      "included_in_out", selector(),
                      J::array(
                          {track("opacity", {{0, 0}, {44, 0}, {54, 1}, {84, 1}, {95, 0}}),
                           track("blur", {{0, 15}, {44, 15}, {54, 0}, {84, 0}, {95, 12}})}))}))})});
    c.push_back({10, 137, 720, "thank_you_typewriter",
                 J::array({layer_text("thank_you", "Thank you!", 90, 137,
                                      reveal("grapheme", 17, 137, "forward", false, 0, 100))})});
    J manifest = {{"reference", "preview_540p_crf26_lower_quality.mp4"},
                  {"reference_fps", 30},
                  {"reference_frames", 857},
                  {"canvas", {W, H}},
                  {"segments", J::array()},
                  {"note", "Segment ranges follow the visible #01–#10 reference labels."}};
    for (auto &x : c) {
        J stage = {{"id", "stage"},
                   {"type", "image"},
                   {"asset", "tests/fixtures/text_motion_reference_v2/motion_bear_stage.png"},
                   {"fit", "stretch"},
                   {"size", {W, H}},
                   {"position", {0, 0}},
                   {"start_frame", 0},
                   {"duration_frames", x.dur}};
        const std::string label_value =
            "#" + std::string(x.n < 10 ? "0" : "") + std::to_string(x.n);
        J label = text("reference_index", label_value, 18, "#E2EAF4", J::array(), 0, x.dur, 54);
        label["style"].erase("glow");
        J layers = J::array({stage, label});
        for (auto l : x.layers)
            layers.push_back(l);
        J plan = {{"schema", "chronon.render-plan.v2"},
                  {"version", 2},
                  {"job_id", "motion_bear_reference_" + std::to_string(x.n) + "_" + x.name},
                  {"canvas",
                   {{"width", W},
                    {"height", H},
                    {"fps_num", FPS},
                    {"fps_den", 1},
                    {"duration_frames", x.dur}}},
                  {"layers", layers},
                  {"output", {{"path", x.name + ".mp4"}, {"format", "mp4"}, {"codec", "h264"}}}};
        std::ofstream(
            here / "plans" /
            (std::string(x.n < 10 ? "0" : "") + std::to_string(x.n) + "_" + x.name + ".plan.json"))
            << plan.dump(2) << '\n';
        manifest["segments"].push_back({{"number", x.n},
                                        {"name", x.name},
                                        {"frames", x.dur},
                                        {"source_start_frame", x.src},
                                        {"source_end_frame_exclusive", x.src + x.dur}});
    }
    std::ofstream(here / "manifest.json") << manifest.dump(2) << '\n';
    std::ofstream ppm(here / "motion_bear_stage.ppm", std::ios::binary);
    ppm << "P6\n" << W << " " << H << "\n255\n";
    const std::vector<std::pair<double, std::array<int, 3>>> stops = {
        {0.0, {2, 3, 8}}, {350.0, {2, 3, 8}}, {400.0, {1, 6, 14}},
        {440.0, {3, 12, 26}}, {470.0, {3, 19, 39}}, {492.0, {6, 26, 52}},
        {540.0, {6, 29, 58}}};
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const double source_y = y / 2.0;
            auto upper = stops.begin() + 1;
            while (upper != stops.end() && source_y > upper->first)
                ++upper;
            const auto &lo = *(upper - 1);
            const auto &hi = *upper;
            const double t = std::clamp((source_y - lo.first) / (hi.first - lo.first), 0.0, 1.0);
            int rgb_value[3];
            for (int channel = 0; channel < 3; ++channel)
                rgb_value[channel] = int(std::lround(lo.second[channel] +
                                                    (hi.second[channel] - lo.second[channel]) * t));
            // Keep the horizon understated: a 1 px feature at 540p becomes 2 px here.
            if (y >= 986 && y < 988 && x >= 48 && x < W - 48) {
                rgb_value[0] = 13;
                rgb_value[1] = 22;
                rgb_value[2] = 37;
            }
            char rgb[3] = {char(rgb_value[0]), char(rgb_value[1]), char(rgb_value[2])};
            ppm.write(rgb, 3);
        }
    std::cout << "Generated " << c.size() << " plans and C++ stage (1920x1080, 30 fps)\n";
}
