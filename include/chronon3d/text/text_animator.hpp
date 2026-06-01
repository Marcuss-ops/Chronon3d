#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/animation/easing.hpp>
#include <chronon3d/animation/animated_value.hpp>
#include <string>
#include <vector>
#include <functional>

namespace chronon3d {

// ── Text Animation Mode ─────────────────────────────────────────────────────

enum class TextAnimMode {
    ByCharacter,  // one unit per character (including spaces)
    ByWord,       // one unit per word
    ByLine,       // one unit per line (split by '\n')
};

// ── Text Animation Configuration ────────────────────────────────────────────

struct TextAnimConfig {
    TextAnimMode mode{TextAnimMode::ByCharacter};
    Frame duration{30};              // animation duration per unit (frames)
    Frame delay_per_unit{3};         // stagger delay between consecutive units
    EasingCurve easing{Easing::OutCubic};

    // ── Which effects to animate ────────────────────────────────────────
    bool animate_opacity{true};      // fade in from 0 → 1
    bool animate_slide{false};       // slide in from an offset
    Vec3 slide_from{0.0f, 40.0f, 0.0f};
    bool animate_scale{false};       // scale from a smaller size
    Vec3 scale_from{0.7f, 0.7f, 1.0f};
    bool animate_tracking{false};    // animate letter-spacing (tracking)
    f32  tracking_from{30.0f};       // initial extra pixel gap between units
    bool animate_blur{false};        // animate gaussian blur reveal
    f32  blur_from{20.0f};           // initial blur radius in pixels
};

// ── TextAnimator — builds staggered per-unit text layers ────────────────────

class TextAnimator {
public:
    TextAnimator() = default;

    // ── Text content ────────────────────────────────────────────────────
    TextAnimator& text(std::string content) {
        m_text = std::move(content);
        return *this;
    }

    // ── Typography ──────────────────────────────────────────────────────
    TextAnimator& font_size(f32 size)       { m_font_size = size; return *this; }
    TextAnimator& font_path(std::string p)  { m_font_path = std::move(p); return *this; }
    TextAnimator& font_weight(int w)        { m_font_weight = w; return *this; }
    TextAnimator& color(Color c)            { m_color = c; return *this; }
    TextAnimator& align(TextAlign a)        { m_align = a; return *this; }
    TextAnimator& line_height(f32 ratio)    { m_line_height = ratio; return *this; }
    TextAnimator& tracking(f32 px)          { m_tracking = px; return *this; }  // base (non-animated)

    // ── Animation config ────────────────────────────────────────────────
    TextAnimator& config(const TextAnimConfig& cfg) {
        m_config = cfg;
        return *this;
    }

    // ── Build layers into a SceneBuilder ────────────────────────────────
    // Creates one layer per unit (character / word / line) with staggered
    // keyframe animation on AnimatedTransform.
    void build(SceneBuilder& scene, std::string_view layer_name_prefix) const;

    // ── Split the text into units based on mode ─────────────────────────
    [[nodiscard]] std::vector<std::string> split_units() const;

    // ── Approximate width of a unit at the configured font size ─────────
    [[nodiscard]] f32 measure_unit_width(const std::string& unit) const;

private:
    std::string m_text;
    TextAnimConfig m_config;

    f32          m_font_size{72.0f};
    std::string  m_font_path{"assets/fonts/Inter-Bold.ttf"};
    int          m_font_weight{800};
    Color        m_color{1.0f, 1.0f, 1.0f, 1.0f};
    TextAlign    m_align{TextAlign::Center};
    f32          m_line_height{1.2f};
    f32          m_tracking{0.0f};

    static constexpr f32 kCharWidthRatio = 0.58f;  // approximate ratio for sans-serif

    // Per-unit animation delay (frames)
    [[nodiscard]] Frame unit_delay(size_t index, size_t total) const;
};

// ── Inline helpers ──────────────────────────────────────────────────────────

inline f32 TextAnimator::measure_unit_width(const std::string& unit) const {
    f32 w = 0.0f;
    for (char c : unit) {
        w += m_font_size * kCharWidthRatio + m_tracking;
    }
    return w;
}

inline std::vector<std::string> TextAnimator::split_units() const {
    std::vector<std::string> units;

    switch (m_config.mode) {
        case TextAnimMode::ByCharacter: {
            for (char c : m_text) {
                units.push_back(std::string(1, c));
            }
            break;
        }
        case TextAnimMode::ByWord: {
            std::string current;
            for (char c : m_text) {
                if (c == ' ' || c == '\n') {
                    if (!current.empty()) {
                        units.push_back(current);
                        current.clear();
                    }
                } else {
                    current.push_back(c);
                }
            }
            if (!current.empty()) {
                units.push_back(current);
            }
            break;
        }
        case TextAnimMode::ByLine: {
            std::string current;
            for (char c : m_text) {
                if (c == '\n') {
                    units.push_back(current);
                    current.clear();
                } else {
                    current.push_back(c);
                }
            }
            if (!current.empty()) {
                units.push_back(current);
            }
            break;
        }
    }

    return units;
}

inline Frame TextAnimator::unit_delay(size_t index, size_t total) const {
    if (total <= 1) return Frame{0};
    // Distribute delays evenly across units
    f32 t = static_cast<f32>(index) / static_cast<f32>(total - 1);
    f32 eased = m_config.easing.apply(t);
    f32 max_delay = static_cast<f32>(static_cast<Frame>(total - 1) * m_config.delay_per_unit);
    return Frame{static_cast<Frame>(static_cast<f32>(eased * max_delay))};
}

// ── Build implementation ────────────────────────────────────────────────────

inline void TextAnimator::build(SceneBuilder& scene, std::string_view layer_name_prefix) const {
    if (m_text.empty()) return;

    auto units = split_units();
    if (units.empty()) return;

    // Compute total width for centering
    f32 total_width = 0.0f;
    for (const auto& unit : units) {
        total_width += measure_unit_width(unit);
    }

    // Per-unit tracking animation: the starting position offset for tracking
    f32 tracking_start_total = m_config.animate_tracking
        ? m_config.tracking_from * static_cast<f32>(units.size() - 1)
        : 0.0f;

    f32 cursor_x = 0.0f;
    if (m_align == TextAlign::Center) {
        cursor_x = -total_width * 0.5f;
    } else if (m_align == TextAlign::Right) {
        cursor_x = -total_width;
    }

    for (size_t i = 0; i < units.size(); ++i) {
        const auto& unit = units[i];
        if (unit.empty()) continue;

        f32 unit_w = measure_unit_width(unit);
        f32 center_x = cursor_x + unit_w * 0.5f;
        Frame delay = unit_delay(i, units.size());

        std::string layer_name = std::string(layer_name_prefix) + "_" + std::to_string(i);

        scene.layer(layer_name, [&](LayerBuilder& lb) {
            // Base position
            lb.position({center_x, 0.0f, 0.0f});

            // Create the text node
            TextParams tp;
            tp.text = unit;
            tp.font_size = m_font_size;
            tp.font_path = m_font_path;
            tp.font_weight = m_font_weight;
            tp.color = m_color;
            tp.align = m_align;
            tp.line_height = m_line_height;
            tp.tracking = m_tracking;
            tp.size = {unit_w + 10.0f, m_font_size * 1.4f};  // box per unit
            lb.text("text_node", tp);

            // ── Apply staggered animation keyframes ────────────────────
            // Three-keyframe pattern: Hold at initial, start transition at delay,
            // complete at delay + duration.
            Frame start_frame = delay;
            Frame end_frame   = delay + m_config.duration;

            if (m_config.animate_opacity) {
                auto& op = lb.opacity_anim();
                op.key(Frame{0},           0.0f,          EasingCurve{Easing::Hold});
                op.key(start_frame,        0.0f,          m_config.easing);
                op.key(end_frame,          1.0f,          EasingCurve{Easing::Linear});
            }

            if (m_config.animate_scale) {
                auto& sc = lb.scale_anim();
                sc.key(Frame{0},           m_config.scale_from, EasingCurve{Easing::Hold});
                sc.key(start_frame,        m_config.scale_from, m_config.easing);
                sc.key(end_frame,          Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::Linear});
            }

            // ── Position animation: handle slide, tracking, or both ────
            if (m_config.animate_slide || m_config.animate_tracking) {
                Vec3 start_pos = {center_x, 0.0f, 0.0f};
                Vec3 target_pos = {center_x, 0.0f, 0.0f};

                if (m_config.animate_slide) {
                    start_pos += m_config.slide_from;
                }

                if (m_config.animate_tracking) {
                    f32 tracking_offset = m_config.tracking_from * static_cast<f32>(i);
                    start_pos.x -= tracking_offset;
                }

                auto& pos = lb.position_anim();
                pos.key(Frame{0},          start_pos,     EasingCurve{Easing::Hold});
                pos.key(start_frame,       start_pos,     m_config.easing);
                pos.key(end_frame,         target_pos,    EasingCurve{Easing::Linear});
                lb.position(target_pos);
            }

            if (m_config.animate_blur) {
                lb.blur(m_config.blur_from);
            }
        });

        cursor_x += unit_w;
    }
}

} // namespace chronon3d
