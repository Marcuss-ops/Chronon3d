#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <vector>
#include <string>
#include <algorithm>

namespace chronon3d {

struct RichTextRun {
    std::string text;
    Color color{1, 1, 1, 1};
    f32 font_size{72.0f};
    std::string font_path{"assets/fonts/Inter-Bold.ttf"};
    f32 tracking{0.0f};
    bool is_star{false};
    f32 star_inner_radius{12.0f};
    f32 star_outer_radius{42.0f};
    i32 star_points{8};
};

class RichTextLine {
public:
    RichTextLine& run(std::string text, Color color, f32 size = 72.0f, std::string font = "assets/fonts/Inter-Bold.ttf") {
        m_runs.push_back({
            .text = std::move(text),
            .color = color,
            .font_size = size,
            .font_path = std::move(font),
            .tracking = 0.0f,
            .is_star = false
        });
        return *this;
    }

    RichTextLine& space(f32 px) {
        m_runs.push_back({
            .text = " ",
            .color = Color::white(),
            .font_size = 72.0f,
            .font_path = "assets/fonts/Inter-Bold.ttf",
            .tracking = px,
            .is_star = false
        });
        return *this;
    }

    RichTextLine& star(Color color, f32 outer_radius = 42.0f, f32 inner_radius = 12.0f, i32 points = 8) {
        m_runs.push_back({
            .text = "",
            .color = color,
            .font_size = 0.0f,
            .font_path = "",
            .tracking = 0.0f,
            .is_star = true,
            .star_inner_radius = inner_radius,
            .star_outer_radius = outer_radius,
            .star_points = points
        });
        return *this;
    }

    [[nodiscard]] const std::vector<RichTextRun>& runs() const { return m_runs; }

private:
    std::vector<RichTextRun> m_runs;
};

inline void draw_rich_text(LayerBuilder& l, const RichTextLine& rtl, Vec3 start_pos, FontEngine* engine = nullptr) {
    FontEngine* fe = engine ? engine : &shared_font_engine();
    f32 current_x = start_pos.x;
    i32 index = 0;

    for (const auto& run : rtl.runs()) {
        std::string run_name = "run_" + std::to_string(index++);
        if (run.is_star) {
            l.star(run_name, {
                .center = {current_x + run.star_outer_radius, start_pos.y},
                .points = run.star_points,
                .inner_radius = run.star_inner_radius,
                .outer_radius = run.star_outer_radius,
                .color = run.color
            });
            current_x += run.star_outer_radius * 2.0f;
        } else {
            f32 run_width = 0.0f;
            if (run.text != " ") {
                run_width = fe->measure_text(run.text, FontSpec{run.font_path, "Inter", 900}, run.font_size);
                l.text(run_name, {
                    .text = run.text,
                    .size = {run_width + 10.0f, run.font_size * 2.0f},
                    .pos = {current_x + run_width * 0.5f, start_pos.y, start_pos.z},
                    .font_path = run.font_path,
                    .font_size = run.font_size,
                    .color = run.color,
                    .align = TextAlign::Center,
                    .vertical_align = VerticalAlign::Middle,
                    .tracking = run.tracking,
                    .wrap = TextWrap::None
                });
                current_x += run_width;
            } else {
                current_x += run.tracking;
            }
        }
    }
}

} // namespace chronon3d
