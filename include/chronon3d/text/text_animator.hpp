#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <set>
#include <string>
#include <unordered_map>
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
            // Add tracking per code point (not per byte) to match layout engine behaviour
            return run->width + m_tracking * static_cast<f32>(
                std::max(detail::grapheme_cluster_count(unit), size_t{1}) - 1);
        }
    }
    // Fallback to approximate character-width heuristic (UTF-8 code-point count)
    f32 w = 0.0f;
    const size_t cluster_count = detail::grapheme_cluster_count(unit);
    const f32 effective_clusters = static_cast<f32>(cluster_count > 0 ? cluster_count - 1 : 0);
    w += m_font_size * kCharWidthRatio * static_cast<f32>(cluster_count);
    w += m_tracking * effective_clusters;
    return w;
}

inline std::vector<std::string> TextAnimator::split_units() const {
    std::vector<std::string> units;

    switch (m_config.mode) {
        case TextAnimMode::ByCharacter: {
            // Grapheme-cluster iteration so combining marks (e.g. e + accent),
            // ZWJ emoji sequences, and emoji modifiers stay attached to their
            // base character and are never split mid-reveal.
            // Implements UAX #29 GB11 for emoji ZWJ sequences.
            // O(n) single-pass: accumulate code points into a cluster and
            // only flush when we encounter the START of the NEXT cluster.
            size_t cluster_start = 0;
            size_t i = 0;
            size_t ri_toggle = 0;

            // GB11 state: see grapheme_cluster_count() in text_layout_engine.hpp
            enum class GB11State { Normal, ExtPict, ExtPictZWJ };
            GB11State gb11 = GB11State::Normal;

            while (i < m_text.size()) {
                // Decode one code point
                const unsigned char lead = static_cast<unsigned char>(m_text[i]);
                const size_t len = detail::utf8_seq_len(lead);
                if (i + len > m_text.size()) { ++i; continue; }

                char32_t cp;
                switch (len) {
                    case 1: cp = lead; break;
                    case 2: cp = ((lead & 0x1F) << 6) | (static_cast<unsigned char>(m_text[i+1]) & 0x3F); break;
                    case 3: cp = ((lead & 0x0F) << 12) | ((static_cast<unsigned char>(m_text[i+1]) & 0x3F) << 6) | (static_cast<unsigned char>(m_text[i+2]) & 0x3F); break;
                    case 4: cp = ((lead & 0x07) << 18) | ((static_cast<unsigned char>(m_text[i+1]) & 0x3F) << 12) | ((static_cast<unsigned char>(m_text[i+2]) & 0x3F) << 6) | (static_cast<unsigned char>(m_text[i+3]) & 0x3F); break;
                    default: cp = 0xFFFD; break;
                }

                const bool is_ext = detail::is_grapheme_extend(cp);
                const bool is_ri  = detail::is_regional_indicator(cp);
                const bool is_ep  = detail::is_extended_pictographic(cp);
                const bool is_zwj = (cp == 0x200D);

                // Determine if this code point starts a new cluster.
                // Grapheme_Extend characters and the second RI of a flag
                // continue the current cluster, as do ZWJ-joined emoji
                // (GB11: Extended_Pictographic Extend* ZWJ × Extended_Pictographic).
                bool starts_new_cluster = !is_ext && !(is_ri && ri_toggle == 1);

                // GB11: ZWJ after Extended_Pictographic transitions to ExtPictZWJ.
                if (is_zwj && gb11 == GB11State::ExtPict) {
                    gb11 = GB11State::ExtPictZWJ;
                    starts_new_cluster = false;
                }

                // GB11: Extended_Pictographic after ExtPictZWJ continues the cluster.
                // Exclude Regional Indicators — they use separate GB12/GB13 rules
                // and must not be absorbed into a ZWJ chain.
                if (is_ep && !is_ri && gb11 == GB11State::ExtPictZWJ) {
                    gb11 = GB11State::ExtPict;
                    starts_new_cluster = false;
                }

                if (starts_new_cluster) {
                    // Flush the PREVIOUS cluster (if any)
                    if (i > cluster_start) {
                        units.push_back(
                            m_text.substr(cluster_start, i - cluster_start));
                    }
                    cluster_start = i;
                }

                // Update RI toggle AFTER the cluster-boundary decision.
                if (is_ri) ri_toggle ^= 1;
                else if (!is_ext) ri_toggle = 0;

                // Update GB11 state for non-extend, non-ZWJ-sequenced characters
                if (!is_ext && !(is_ep && gb11 == GB11State::ExtPictZWJ)) {
                    gb11 = is_ep ? GB11State::ExtPict : GB11State::Normal;
                }

                i += len;
            }
            // Flush the final cluster
            if (i > cluster_start) {
                units.push_back(m_text.substr(cluster_start, i - cluster_start));
            }
            break;
        }
        case TextAnimMode::ByWord: {
            std::string current;
            for (size_t i = 0; i < m_text.size();) {
                size_t len = detail::utf8_seq_len(static_cast<unsigned char>(m_text[i]));
                if (len == 1 && (m_text[i] == ' ' || m_text[i] == '\n')) {
                    if (!current.empty()) {
                        units.push_back(current);
                        current.clear();
                    }
                } else {
                    current.append(m_text, i, len);
                }
                i += len;
            }
            if (!current.empty()) {
                units.push_back(current);
            }
            break;
        }
        case TextAnimMode::ByLine: {
            std::string current;
            for (size_t i = 0; i < m_text.size();) {
                size_t len = detail::utf8_seq_len(static_cast<unsigned char>(m_text[i]));
                if (len == 1 && m_text[i] == '\n') {
                    units.push_back(current);
                    current.clear();
                } else {
                    current.append(m_text, i, len);
                }
                i += len;
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

    // ── Canonical PlacedGlyphRun: resolves positions and builds
    // cluster info with byte offsets and advances. ────────────────
    // Tracking is 0 here because the TextAnimator has its own
    // animated tracking in m_tracking (applied separately).
    auto placed = resolve_placed_glyph_run(*run, 0.0f, m_text);

    // ── Map each cluster to a GlyphUnit ──────────────────────────
    units.reserve(placed.clusters.size());
    for (const auto& cl : placed.clusters) {
        if (cl.byte_len == 0) continue;

        GlyphUnit gu;
        gu.text = m_text.substr(cl.byte_offset, cl.byte_len);
        // Use the resolved x position of the first glyph in this cluster.
        if (cl.start_glyph < placed.glyphs.size()) {
            gu.x = placed.glyphs[cl.start_glyph].x;
        }
        // Use the CLUSTER's raw_advance (not just the first glyph's advance_x).
        // This correctly accounts for multi-glyph clusters (ligatures,
        // combining marks) where the cluster's total advance spans all
        // its glyphs, not just the first one.
        gu.advance_x = cl.raw_advance;

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
        for (size_t i = 0; i < glyphs.size(); ++i) {
            total_width += glyphs[i].advance_x;
            // Apply tracking between consecutive glyphs (not after the last).
            // The resolver was called with tracking=0, so we must add the
            // tracking gap ourselves — tp.tracking has no effect on single-
            // cluster layers (tracking * (1-1) = 0).
            if (i + 1 < glyphs.size()) {
                total_width += m_tracking;
            }
        }

        f32 scene_offset_x = 0.0f;
        if (m_align == TextAlign::Center) {
            scene_offset_x = -total_width * 0.5f;
        } else if (m_align == TextAlign::Right) {
            scene_offset_x = -total_width;
        }

        f32 cumulative_tracking = 0.0f;
        for (size_t i = 0; i < glyphs.size(); ++i) {
            const auto& gu = glyphs[i];
            if (gu.text.empty()) continue;

            // Adjust the glyph's position by cumulative tracking so far.
            // gu.x comes from the resolver (tracking=0), so we add the
            // tracking gap from each preceding inter-glyph boundary.
            f32 adjusted_x = gu.x + cumulative_tracking;
            f32 center_x = scene_offset_x + adjusted_x + gu.advance_x * 0.5f;

            // Record tracking for the NEXT boundary (after this glyph).
            cumulative_tracking += m_tracking;

            Frame delay = unit_delay(i, glyphs.size());

            std::string layer_name = std::string(layer_name_prefix) + "_" + std::to_string(i);

            scene.layer(layer_name, [&](LayerBuilder& lb) {
                if (m_font_engine) {
                    lb.font_engine(m_font_engine);
                }

                lb.position({center_x, 0.0f, 0.0f});

                TextSpec tp;
                tp.content.value = gu.text;
                tp.font.font_size = m_font_size;
                tp.font.font_path = m_font_path;
                tp.font.font_weight = m_font_weight;
                tp.appearance.color = m_color;
                tp.layout.align = m_align;
                tp.layout.line_height = m_line_height;
                tp.layout.tracking = m_tracking;  // no effect on single-cluster layers (kept for API consistency)
                tp.layout.box = {gu.advance_x + 10.0f, m_font_size * 1.4f};
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

            TextSpec tp;
            tp.content.value = unit;
            tp.font.font_size = m_font_size;
            tp.font.font_path = m_font_path;
            tp.font.font_weight = m_font_weight;
            tp.appearance.color = m_color;
            tp.layout.align = m_align;
            tp.layout.line_height = m_line_height;
            tp.layout.tracking = m_tracking;
            tp.layout.box = {unit_w + 10.0f, m_font_size * 1.4f};
            lb.text("text_node", tp);

            apply_animation(lb, i, center_x, delay);
        });

        cursor_x += unit_w;
    }
}

} // namespace chronon3d
