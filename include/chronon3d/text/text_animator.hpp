#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <string>
#include <vector>
#include <functional>

namespace chronon3d {

// ── Text Animation Mode ─────────────────────────────────────────────────────

enum class TextAnimMode {
    ByCharacter,  // one unit per character (including spaces)
    ByWord,       // one unit per word
    ByLine,       // one unit per line (split by '\n')
    ByGlyph,      // one unit per shaped glyph with exact GlyphRun positions
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

    // ── FontEngine (for precise glyph metrics and shaping) ──────────────
    TextAnimator& font_engine(FontEngine* engine) { m_font_engine = engine; return *this; }

    // ── Animation config ────────────────────────────────────────────────
    TextAnimator& config(const TextAnimConfig& cfg) {
        m_config = cfg;
        return *this;
    }

    // ── Per-glyph unit (used by ByGlyph mode) ───────────────────────────
    struct GlyphUnit {
        std::string text;     // characters belonging to this glyph's cluster
        f32         x{0.0f};  // glyph x position from full-text shaping (pixels)
        f32         advance_x{0.0f}; // glyph advance (pixels)
    };

    // ── Build layers into a SceneBuilder ────────────────────────────────
    // Creates one layer per unit (character / word / line / glyph) with staggered
    // keyframe animation on AnimatedTransform.
    void build(SceneBuilder& scene, std::string_view layer_name_prefix) const;

    // ── Split the text into units based on mode ─────────────────────────
    [[nodiscard]] std::vector<std::string> split_units() const;

    // ── Split the text into shaped glyphs with exact positions ──────────
    [[nodiscard]] std::vector<GlyphUnit> split_glyphs() const;

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
    FontEngine*  m_font_engine{nullptr};

    static constexpr f32 kCharWidthRatio = 0.58f;  // approximate ratio for sans-serif (fallback)

    // Per-unit animation delay (frames)
    [[nodiscard]] Frame unit_delay(size_t index, size_t total) const;

    // Apply staggered animation keyframes to a LayerBuilder (shared by all modes)
    void apply_animation(LayerBuilder& lb, size_t index, f32 center_x, Frame delay) const;
};

// ── Inline helpers ──────────────────────────────────────────────────────────

inline f32 TextAnimator::measure_unit_width(const std::string& unit) const {
    if (m_font_engine && !unit.empty()) {
        FontSpec spec{m_font_path, "", m_font_weight};
        if (auto run = m_font_engine->shape_text(unit, spec, m_font_size)) {
            // Add tracking per character to match layout engine behaviour
            return run->width + m_tracking * static_cast<f32>(unit.size());
        }
    }
    // Fallback to approximate character-width heuristic
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

inline std::vector<TextAnimator::GlyphUnit> TextAnimator::split_glyphs() const {
    std::vector<GlyphUnit> units;
    if (m_text.empty() || !m_font_engine) return units;

    FontSpec spec{m_font_path, "", m_font_weight};
    auto run = m_font_engine->shape_text(m_text, spec, m_font_size);
    if (!run) return units;

    // RTL detection: if glyph clusters are decreasing (e.g. Arabic, Hebrew),
    // fall back silently so callers get correct substring positions.
    // We detect RTL by checking if the first glyph's cluster > last glyph's cluster
    // (clusters are byte offsets into the source text, so for RTL they go
    // from high to low).
    if (!run->glyphs.empty() && run->glyphs.front().cluster > run->glyphs.back().cluster) {
        // RTL text detected. For RTL, clusters DECREASE (high → low byte offsets)
        // because HarfBuzz processes right-to-left. In the glyph array (visual order,
        // left to right), glyph[0] has the highest cluster and glyph[N-1] the lowest.
        //
        // To extract correct substrings:
        //   glyph[i].cluster   = end byte offset of this visual cluster
        //   glyph[i+1].cluster = start byte offset of this visual cluster (lower)
        //
        // NOTE: Mixed-direction text (e.g. "Hello العربية") has non-monotonic clusters
        // and is not handled by this simple RTL/LTR check. For mixed text, splitting
        // is undefined — consider using ByCharacter mode for such cases.
        for (size_t i = 0; i < run->glyphs.size(); ++i) {
            const auto& gp = run->glyphs[i];
            if (!gp.is_cluster_start) continue;

            // Find the end of this cluster in the glyph array
            size_t cluster_end = i + 1;
            while (cluster_end < run->glyphs.size() && !run->glyphs[cluster_end].is_cluster_start) {
                ++cluster_end;
            }

            // For RTL: the next glyph's cluster (lower offset) is the start,
            // this glyph's cluster (higher offset) is the end.
            size_t text_start = (cluster_end < run->glyphs.size())
                ? static_cast<size_t>(run->glyphs[cluster_end].cluster)
                : 0;
            size_t text_end   = static_cast<size_t>(run->glyphs[i].cluster);

            if (text_end <= text_start) continue; // safety guard

            GlyphUnit gu;
            gu.text = m_text.substr(text_start, text_end - text_start);
            gu.x = gp.x;
            gu.advance_x = gp.advance_x;
            units.push_back(std::move(gu));
        }
        return units;
    }

    // LTR path: clusters increase monotonically
    units.reserve(run->glyphs.size());
    for (size_t i = 0; i < run->glyphs.size(); ++i) {
        const auto& gp = run->glyphs[i];
        size_t start_byte = gp.cluster;
        size_t end_byte = m_text.size();

        for (size_t j = i + 1; j < run->glyphs.size(); ++j) {
            if (run->glyphs[j].cluster > start_byte) {
                end_byte = run->glyphs[j].cluster;
                break;
            }
        }

        GlyphUnit gu;
        gu.text = m_text.substr(start_byte, end_byte - start_byte);
        gu.x = gp.x;
        gu.advance_x = gp.advance_x;
        units.push_back(std::move(gu));
    }
    return units;
}

// ── Build implementation ────────────────────────────────────────────────────

inline void TextAnimator::apply_animation(
    LayerBuilder& lb, size_t index, f32 center_x, Frame delay
) const {
    Frame start_frame = delay;
    Frame end_frame   = delay + m_config.duration;

    if (m_config.animate_opacity) {
        auto& op = lb.opacity_anim();
        op.key(Frame{0},    0.0f, EasingCurve{Easing::Hold});
        op.key(start_frame, 0.0f, m_config.easing);
        op.key(end_frame,   1.0f, EasingCurve{Easing::Linear});
    }

    if (m_config.animate_scale) {
        auto& sc = lb.scale_anim();
        sc.key(Frame{0},    m_config.scale_from, EasingCurve{Easing::Hold});
        sc.key(start_frame, m_config.scale_from, m_config.easing);
        sc.key(end_frame,   Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::Linear});
    }

    if (m_config.animate_slide || m_config.animate_tracking) {
        Vec3 start_pos = {center_x, 0.0f, 0.0f};
        Vec3 target_pos = {center_x, 0.0f, 0.0f};

        if (m_config.animate_slide) {
            start_pos += m_config.slide_from;
        }
        if (m_config.animate_tracking) {
            f32 tracking_offset = m_config.tracking_from * static_cast<f32>(index);
            start_pos.x -= tracking_offset;
        }

        auto& pos = lb.position_anim();
        pos.key(Frame{0},   start_pos, EasingCurve{Easing::Hold});
        pos.key(start_frame, start_pos, m_config.easing);
        pos.key(end_frame,   target_pos, EasingCurve{Easing::Linear});
        lb.position(target_pos);
    }

    if (m_config.animate_blur) {
        auto& bl = lb.blur_anim();
        bl.key(Frame{0},   m_config.blur_from, EasingCurve{Easing::Hold});
        bl.key(start_frame, m_config.blur_from, m_config.easing);
        bl.key(end_frame,   0.0f, EasingCurve{Easing::Linear});
    }
}

inline void TextAnimator::build(SceneBuilder& scene, std::string_view layer_name_prefix) const {
    if (m_text.empty()) return;

    // ── ByGlyph mode: use precise GlyphRun positions ─────────────────
    if (m_config.mode == TextAnimMode::ByGlyph) {
        auto glyphs = split_glyphs();
        if (glyphs.empty()) {
            // FontEngine missing or shaping failed — fall back to ByCharacter
            TextAnimConfig fallback_cfg = m_config;
            fallback_cfg.mode = TextAnimMode::ByCharacter;
            TextAnimator fallback = *this;
            fallback.config(fallback_cfg);
            fallback.build(scene, layer_name_prefix);
            return;
        }

        f32 total_width = 0.0f;
        for (const auto& g : glyphs) {
            total_width += g.advance_x;
        }

        f32 scene_offset_x = 0.0f;
        if (m_align == TextAlign::Center) {
            scene_offset_x = -total_width * 0.5f;
        } else if (m_align == TextAlign::Right) {
            scene_offset_x = -total_width;
        }

        for (size_t i = 0; i < glyphs.size(); ++i) {
            const auto& gu = glyphs[i];
            if (gu.text.empty()) continue;

            f32 center_x = scene_offset_x + gu.x + gu.advance_x * 0.5f;
            Frame delay = unit_delay(i, glyphs.size());

            std::string layer_name = std::string(layer_name_prefix) + "_" + std::to_string(i);

            scene.layer(layer_name, [&](LayerBuilder& lb) {
                if (m_font_engine) {
                    lb.font_engine(m_font_engine);
                }

                lb.position({center_x, 0.0f, 0.0f});

                TextParams tp;
                tp.text = gu.text;
                tp.font_size = m_font_size;
                tp.font_path = m_font_path;
                tp.font_weight = m_font_weight;
                tp.color = m_color;
                tp.align = m_align;
                tp.line_height = m_line_height;
                tp.tracking = m_tracking;
                tp.size = {gu.advance_x + 10.0f, m_font_size * 1.4f};
                lb.text("text_node", tp);

                apply_animation(lb, i, center_x, delay);
            });
        }
        return;
    }

    // ── ByCharacter / ByWord / ByLine modes ──────────────────────────
    auto units = split_units();
    if (units.empty()) return;

    // Pre-compute unit widths once to avoid redundant shaping / measurement
    std::vector<f32> unit_widths;
    unit_widths.reserve(units.size());
    f32 total_width = 0.0f;
    for (const auto& unit : units) {
        f32 w = measure_unit_width(unit);
        unit_widths.push_back(w);
        total_width += w;
    }

    f32 cursor_x = 0.0f;
    if (m_align == TextAlign::Center) {
        cursor_x = -total_width * 0.5f;
    } else if (m_align == TextAlign::Right) {
        cursor_x = -total_width;
    }

    for (size_t i = 0; i < units.size(); ++i) {
        const auto& unit = units[i];
        if (unit.empty()) continue;

        f32 unit_w = unit_widths[i];
        f32 center_x = cursor_x + unit_w * 0.5f;
        Frame delay = unit_delay(i, units.size());

        std::string layer_name = std::string(layer_name_prefix) + "_" + std::to_string(i);

        scene.layer(layer_name, [&](LayerBuilder& lb) {
            if (m_font_engine) {
                lb.font_engine(m_font_engine);
            }

            lb.position({center_x, 0.0f, 0.0f});

            TextParams tp;
            tp.text = unit;
            tp.font_size = m_font_size;
            tp.font_path = m_font_path;
            tp.font_weight = m_font_weight;
            tp.color = m_color;
            tp.align = m_align;
            tp.line_height = m_line_height;
            tp.tracking = m_tracking;
            tp.size = {unit_w + 10.0f, m_font_size * 1.4f};
            lb.text("text_node", tp);

            apply_animation(lb, i, center_x, delay);
        });

        cursor_x += unit_w;
    }
}

} // namespace chronon3d
