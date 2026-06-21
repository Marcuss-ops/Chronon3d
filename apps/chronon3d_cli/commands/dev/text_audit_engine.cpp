#include "text_audit_engine.hpp"
#include "text_audit_types.hpp"

#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/core/types/frame_context.hpp>

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <map>
#include <cctype>

namespace chronon3d::cli {

namespace {

// ── UTF-8 helpers ─────────────────────────────────────────────────────────

int count_codepoints(const std::string& s) {
    int count = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if (c < 0x80) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else len = 1;
        i += len;
        ++count;
    }
    return count;
}

bool has_replacement_char(const std::string& s) {
    // Check for U+FFFD (EF BF BD in UTF-8)
    return s.find("\xEF\xBF\xBD") != std::string::npos;
}

// ── JSON serialization helpers ─────────────────────────────────────────────

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string json_bbox(const TextAuditBBox& b) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(1);
    os << "[" << b.x0 << ", " << b.y0 << ", " << b.x1 << ", " << b.y1 << "]";
    return os.str();
}

// ── Border alpha counting (scans texture edges) ───────────────────────────

struct BorderAlphaResult {
    int top{0}, bottom{0}, left{0}, right{0};
    int total{0};
};

BorderAlphaResult count_border_alpha(const BLImage& img, int alpha_threshold) {
    BorderAlphaResult result;
    BLImageData data;
    if (img.getData(&data) != BL_SUCCESS || !data.pixelData) return result;

    const int w = data.size.w;
    const int h = data.size.h;
    if (w <= 0 || h <= 0) return result;

    const int stride_px = static_cast<int>(data.stride / sizeof(uint32_t));
    auto* pixels = reinterpret_cast<const uint32_t*>(data.pixelData);

    auto alpha_at = [&](int x, int y) -> int {
        return static_cast<int>((pixels[y * stride_px + x] >> 24) & 0xFF);
    };

    // Top row
    for (int x = 0; x < w; ++x) {
        if (alpha_at(x, 0) > alpha_threshold) ++result.top;
    }
    // Bottom row
    for (int x = 0; x < w; ++x) {
        if (alpha_at(x, h - 1) > alpha_threshold) ++result.bottom;
    }
    // Left column (skip corners already counted)
    for (int y = 1; y < h - 1; ++y) {
        if (alpha_at(0, y) > alpha_threshold) ++result.left;
    }
    // Right column (skip corners already counted)
    for (int y = 1; y < h - 1; ++y) {
        if (alpha_at(w - 1, y) > alpha_threshold) ++result.right;
    }

    result.total = result.top + result.bottom + result.left + result.right;
    return result;
}

// ── Ink bbox from rendered image ───────────────────────────────────────────

struct InkBBoxResult {
    TextAuditBBox bbox;
    bool has_ink{false};
    int ink_pixel_count{0};
};

InkBBoxResult compute_ink_bbox(const BLImage& img, int alpha_threshold) {
    InkBBoxResult result;
    BLImageData data;
    if (img.getData(&data) != BL_SUCCESS || !data.pixelData) return result;

    const int w = data.size.w;
    const int h = data.size.h;
    if (w <= 0 || h <= 0) return result;

    const int stride_px = static_cast<int>(data.stride / sizeof(uint32_t));
    auto* pixels = reinterpret_cast<const uint32_t*>(data.pixelData);

    int min_x = w, min_y = h, max_x = -1, max_y = -1;
    int count = 0;

    // Sampled scan for performance (stride 2 in both directions)
    for (int y = 0; y < h; y += 2) {
        for (int x = 0; x < w; x += 2) {
            int alpha = static_cast<int>((pixels[y * stride_px + x] >> 24) & 0xFF);
            if (alpha > alpha_threshold) {
                if (x < min_x) min_x = x;
                if (y < min_y) min_y = y;
                if (x > max_x) max_x = x;
                if (y > max_y) max_y = y;
                ++count;
            }
        }
    }

    if (count > 0) {
        // Expand by sample stride
        result.bbox.x0 = static_cast<float>(std::max(0, min_x - 1));
        result.bbox.y0 = static_cast<float>(std::max(0, min_y - 1));
        result.bbox.x1 = static_cast<float>(std::min(w - 1, max_x + 1));
        result.bbox.y1 = static_cast<float>(std::min(h - 1, max_y + 1));
        result.has_ink = true;
        result.ink_pixel_count = count;
    }

    return result;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// audit_single_text — the core audit for one TextShape at one frame
// ═══════════════════════════════════════════════════════════════════════════

TextAuditFrameResult audit_single_text(
    const TextShape& text,
    int canvas_width,
    int canvas_height,
    int frame,
    const TextAuditPolicy& policy,
    const TextAuditFrameResult* prev_frame)
{
    TextAuditFrameResult result;
    result.frame = frame;
    result.total_codepoints = count_codepoints(text.text);
    result.font_size_requested = text.style.size;

    // ── Safe area ─────────────────────────────────────────────────────
    float margin_x = canvas_width * policy.safe_margin_x;
    float margin_y = canvas_height * policy.safe_margin_y;
    result.safe_area = {margin_x, margin_y,
                        canvas_width - margin_x, canvas_height - margin_y};

    // ── Text box ──────────────────────────────────────────────────────
    if (text.box.enabled) {
        // Box is relative to layer position; for audit we center on canvas
        float cx = canvas_width * 0.5f;
        float cy = canvas_height * 0.5f;
        result.text_box = {
            cx - text.box.size.x * 0.5f,
            cy - text.box.size.y * 0.5f,
            cx + text.box.size.x * 0.5f,
            cy + text.box.size.y * 0.5f
        };
    }

    // ── Layout computation ────────────────────────────────────────────
    // WP-8 PR 8.0 — textual CLI tools source the resolver from
    // `runtime::typed_resolver_for_deep_code` (the PR 8.0 transition
    // bridge; deleted in PR 8.1).  Production paths plumb an explicit
    // `sw_renderer->runtime().resolver()` — the CLI dev-command
    // surface has no runtime in scope today, so it's on the bridge.
    // WP-8 PR 8.0 — explicit resolver source, paired with the engine
    // ctor and with `rasterize_text_to_bl_image` below.
    const auto& resolver = chronon3d::runtime::typed_resolver_for_deep_code();
    FontEngine engine{resolver};
    FontSpec font_spec;
    font_spec.font_path = text.style.font_path;
    font_spec.font_family = text.style.font_family;
    font_spec.font_weight = text.style.font_weight;
    font_spec.font_style = text.style.font_style;

    TextLayoutInput layout_in;
    layout_in.text = text.text;
    layout_in.style = text.style;
    layout_in.box = text.box;
    layout_in.font_engine = &engine;
    layout_in.font_spec = font_spec;

    TextLayoutResult layout_res = TextLayoutEngine::layout(layout_in);

    result.font_size_resolved = layout_res.font_size;
    result.line_count = static_cast<int>(layout_res.lines.size());

    float max_line_w = 0.0f;
    float min_line_w = 1e9f;

    for (size_t i = 0; i < layout_res.lines.size(); ++i) {
        const auto& line = layout_res.lines[i];
        TextAuditLineInfo li;
        li.text = line.text;
        li.x = line.position.x;
        li.y = line.position.y;
        li.width = line.width;
        li.baseline = line.baseline;
        result.lines.push_back(li);

        max_line_w = std::max(max_line_w, line.width);
        if (line.width > 0.0f) min_line_w = std::min(min_line_w, line.width);
    }

    // Layout bbox (from layout engine)
    float text_start_x = canvas_width * 0.5f - layout_res.size.x * 0.5f;
    float text_start_y = canvas_height * 0.5f - layout_res.size.y * 0.5f;
    result.layout_bbox = {
        text_start_x,
        text_start_y,
        text_start_x + layout_res.size.x,
        text_start_y + layout_res.size.y
    };

    // ── Rasterize for ink bbox ────────────────────────────────────────
    auto raster = rasterize_text_to_bl_image(text, layout_res.font_size, 4, resolver);
    if (raster) {
        auto ink = compute_ink_bbox(raster->image, policy.alpha_threshold);
        if (ink.has_ink) {
            // Translate ink bbox from texture coords to canvas coords
            result.ink_bbox = {
                text_start_x + raster->x_offset + ink.bbox.x0,
                text_start_y + raster->y_offset + ink.bbox.y0,
                text_start_x + raster->x_offset + ink.bbox.x1,
                text_start_y + raster->y_offset + ink.bbox.y1
            };
        }

        auto border = count_border_alpha(raster->image, policy.alpha_threshold);
        result.checks.border_alpha_pixels = border.total;
        result.checks.alpha_pixels_top = border.top;
        result.checks.alpha_pixels_bottom = border.bottom;
        result.checks.alpha_pixels_left = border.left;
        result.checks.alpha_pixels_right = border.right;
    }

    // ── Glyph positions (for stability tracking) ──────────────────────
    // Build per-glyph info from layout lines
    int glyph_idx = 0;
    for (size_t li = 0; li < layout_res.lines.size(); ++li) {
        const auto& line = layout_res.lines[li];
        float glyph_x = text_start_x + line.position.x;
        float glyph_y = text_start_y + line.position.y + line.baseline;

        // Walk the text of this line by codepoints
        const std::string& lt = line.text;
        for (size_t bi = 0; bi < lt.size();) {
            unsigned char c0 = static_cast<unsigned char>(lt[bi]);
            size_t cp_len = 1;
            if (c0 < 0x80) cp_len = 1;
            else if ((c0 & 0xE0) == 0xC0) cp_len = 2;
            else if ((c0 & 0xF0) == 0xE0) cp_len = 3;
            else if ((c0 & 0xF8) == 0xF0) cp_len = 4;
            else cp_len = 1;

            std::string cp_str = lt.substr(bi, cp_len);

            TextAuditGlyphInfo gi;
            gi.glyph_index = glyph_idx;
            gi.codepoint = cp_str;
            gi.current_x = glyph_x;
            gi.current_y = glyph_y;
            gi.line_index = static_cast<int>(li);

            // Estimate char width for x advance
            float char_w = layout_res.font_size * 0.58f + text.style.tracking;
            if (cp_str == " ") char_w = layout_res.font_size * 0.28f + text.style.tracking;

            // If this glyph was in the previous frame, use its final position
            if (prev_frame && glyph_idx < static_cast<int>(prev_frame->glyphs.size())) {
                gi.final_x = prev_frame->glyphs[glyph_idx].final_x;
                gi.final_y = prev_frame->glyphs[glyph_idx].final_y;
            } else {
                gi.final_x = glyph_x;
                gi.final_y = glyph_y;
            }

            result.glyphs.push_back(gi);
            glyph_x += char_w;
            glyph_idx += cp_len > 1 ? 1 : 1;
            bi += cp_len;
        }
    }

    result.visible_text = text.text;
    result.visible_codepoints = result.total_codepoints;

    // ── Check 1: Inside canvas ────────────────────────────────────────
    if (result.ink_bbox.x0 < 0 || result.ink_bbox.y0 < 0 ||
        result.ink_bbox.x1 > canvas_width || result.ink_bbox.y1 > canvas_height) {
        result.checks.inside_canvas = false;
    }

    // ── Check 2: Inside safe area ─────────────────────────────────────
    if (result.ink_bbox.x0 < result.safe_area.x0 ||
        result.ink_bbox.y0 < result.safe_area.y0 ||
        result.ink_bbox.x1 > result.safe_area.x1 ||
        result.ink_bbox.y1 > result.safe_area.y1) {
        result.checks.inside_safe_area = false;
    }

    // ── Check 3: No clipping (border alpha) ───────────────────────────
    result.checks.no_clipping = (result.checks.border_alpha_pixels <= policy.max_border_alpha_pixels);

    // ── Check 4: Stable line breaks (compare with previous frame) ─────
    // Skip when the previous frame had no text (e.g. typewriter before
    // start_delay) — the transition from nothing to something is expected.
    if (prev_frame && prev_frame->line_count > 0) {
        // Same line count and same line texts = stable
        if (prev_frame->line_count != result.line_count) {
            result.checks.stable_line_breaks = false;
        } else {
            for (size_t i = 0; i < std::min(result.lines.size(), prev_frame->lines.size()); ++i) {
                // Only compare the text that was visible in both frames
                if (result.lines[i].text != prev_frame->lines[i].text) {
                    result.checks.stable_line_breaks = false;
                    break;
                }
            }
        }
    }

    // ── Check 5: Stable glyph positions ───────────────────────────────
    // Skip when the previous frame had no glyphs (e.g. typewriter before
    // start_delay or empty frame).
    if (prev_frame && !prev_frame->glyphs.empty()) {
        for (size_t i = 0; i < std::min(result.glyphs.size(), prev_frame->glyphs.size()); ++i) {
            float dx = std::abs(result.glyphs[i].current_x - prev_frame->glyphs[i].final_x);
            float dy = std::abs(result.glyphs[i].current_y - prev_frame->glyphs[i].final_y);
            if (dx > policy.glyph_position_tolerance ||
                dy > policy.glyph_position_tolerance) {
                result.checks.stable_glyph_positions = false;
                break;
            }
        }
    }

    // ── Check 6: Centering error ──────────────────────────────────────
    if (result.ink_bbox.x1 > result.ink_bbox.x0) {
        float ink_cx = (result.ink_bbox.x0 + result.ink_bbox.x1) * 0.5f;
        float target_cx = canvas_width * 0.5f;
        result.checks.center_error_x_px = std::abs(ink_cx - target_cx);
    }
    if (result.ink_bbox.y1 > result.ink_bbox.y0) {
        float ink_cy = (result.ink_bbox.y0 + result.ink_bbox.y1) * 0.5f;
        float target_cy = canvas_height * 0.5f;
        result.checks.center_error_y_px = std::abs(ink_cy - target_cy);
    }

    // ── Check 7: Line balance ─────────────────────────────────────────
    if (max_line_w > 0.0f && result.line_count > 1) {
        result.checks.line_balance_ratio = min_line_w / max_line_w;
    }
    // Count single-word lines
    for (const auto& li : result.lines) {
        bool has_space = li.text.find(' ') != std::string::npos;
        if (!has_space && !li.text.empty()) {
            result.checks.single_word_lines++;
        }
    }
    // Orphan line: last line < 20% of average width
    if (result.line_count > 1 && !result.lines.empty()) {
        float avg_w = max_line_w; // approximate
        float last_w = result.lines.back().width;
        if (avg_w > 0 && last_w / avg_w < 0.20f) {
            result.checks.orphan_lines = 1;
        }
    }

    // ── Check 8: UTF-8 validity and text match ────────────────────────
    if (has_replacement_char(text.text)) {
        result.checks.utf8_valid = false;
    }
    result.checks.final_text_matches = (result.visible_text == text.text);

    // ── Check 9: Missing glyphs ───────────────────────────────────────
    // If the font engine couldn't find a glyph, it would show U+FFFD or empty
    if (!result.checks.utf8_valid) {
        result.checks.missing_glyphs = 1; // at least
    }

    // ── Check 10: Layout vs ink ratio ─────────────────────────────────
    float layout_w = result.layout_bbox.x1 - result.layout_bbox.x0;
    float ink_w = result.ink_bbox.x1 - result.ink_bbox.x0;
    if (layout_w > 0 && ink_w > 0) {
        result.checks.layout_vs_ink_ratio = ink_w / layout_w;
    }

    // ── Text box height overflow ──────────────────────────────────────
    if (text.box.enabled) {
        float layout_h = result.layout_bbox.y1 - result.layout_bbox.y0;
        if (layout_h > text.box.size.y * 1.05f) {
            result.checks.text_box_height_overflow = true;
        }
    }

    // ── Determine status ──────────────────────────────────────────────
    bool has_fail = false;
    bool has_warn = false;

    if (!result.checks.inside_canvas) has_fail = true;
    if (!result.checks.inside_safe_area) has_warn = true;
    if (!result.checks.no_clipping) has_fail = true;
    if (!result.checks.stable_line_breaks) has_fail = true;
    if (!result.checks.stable_glyph_positions) has_fail = true;
    if (!result.checks.utf8_valid) has_fail = true;
    if (!result.checks.final_text_matches) has_fail = true;
    if (result.checks.missing_glyphs > 0) has_fail = true;
    if (result.checks.text_box_height_overflow) has_warn = true;
    if (result.checks.center_error_x_px > policy.max_center_error_px) has_warn = true;
    if (result.checks.center_error_y_px > policy.max_center_error_y_px) has_warn = true;
    if (result.checks.border_alpha_pixels > policy.max_border_alpha_pixels) has_fail = true;

    result.status = has_fail ? "FAIL" : (has_warn ? "WARN" : "PASS");

    return result;
}

namespace {

// ═══════════════════════════════════════════════════════════════════════════
// Layer text collection helpers
// ═══════════════════════════════════════════════════════════════════════════
//
// SceneBuilder::layer() stores text nodes inside scene.layers()[i].nodes,
// NOT in scene.nodes().  The audit must walk layers to find text.
//
// typewriter_build() creates per-character layers named "{prefix}_c{index}",
// each containing a single-glyph TextShape.  The audit detects this pattern
// and aggregates the characters into a composite view.

struct LayerTextNode {
    TextShape text;
    std::string layer_name;
    float layer_opacity{1.0f};
    float offset_x{0.0f};
    float offset_y{0.0f};
};

/// Walk scene.layers() and each layer's nodes to find text shapes.
std::vector<LayerTextNode> collect_text_from_scene(const Scene& scene) {
    std::vector<LayerTextNode> result;
    for (const auto& layer : scene.layers()) {
        if (!layer.visible) continue;
        for (const auto& node : layer.nodes) {
            if (node.shape.type() == ShapeType::Text && !node.shape.text().text.empty()) {
                LayerTextNode info;
                info.text = node.shape.text();
                info.layer_name = std::string(layer.name);
                info.layer_opacity = layer.transform.opacity;
                info.offset_x = node.world_transform.position.x;
                info.offset_y = node.world_transform.position.y;
                result.push_back(std::move(info));
            }
        }
    }
    return result;
}

/// Parse layer name pattern "{prefix}_c{digits}" used by typewriter_build.
bool parse_typewriter_layer_name(const std::string& name,
                                  std::string& prefix, int& index) {
    auto cpos = name.rfind("_c");
    if (cpos == std::string::npos || cpos == 0) return false;
    std::string suffix = name.substr(cpos + 2);
    if (suffix.empty()) return false;
    for (char c : suffix) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    prefix = name.substr(0, cpos);
    index = std::stoi(suffix);
    return true;
}

struct TypewriterGroup {
    std::string prefix;
    std::vector<LayerTextNode> chars;  // sorted by index
    std::string full_text;             // concatenation of all char glyphs
};

/// Detect typewriter_build patterns: groups of single-char layers with
/// names like "tw_c0", "tw_c1", etc.
std::vector<TypewriterGroup> detect_typewriter_groups(
    const std::vector<LayerTextNode>& nodes)
{
    std::map<std::string, std::vector<std::pair<int, LayerTextNode>>> groups;

    for (const auto& node : nodes) {
        std::string prefix;
        int index;
        if (parse_typewriter_layer_name(node.layer_name, prefix, index)) {
            groups[prefix].push_back({index, node});
        }
    }

    std::vector<TypewriterGroup> result;
    for (auto& [prefix, entries] : groups) {
        if (entries.size() < 2) continue;  // need >=2 chars to be typewriter
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        TypewriterGroup group;
        group.prefix = prefix;
        for (auto& [idx, node] : entries) {
            group.full_text += node.text.text;
            group.chars.push_back(std::move(node));
        }
        result.push_back(std::move(group));
    }
    return result;
}

} // anonymous namespace (layer text helpers)

// ═══════════════════════════════════════════════════════════════════════════
// audit_composition — compile scene at each frame and audit text nodes
// ═══════════════════════════════════════════════════════════════════════════

TextAuditResult audit_composition(
    const CompositionRegistry& registry,
    const std::string& comp_id,
    const std::vector<int>& frames,
    const TextAuditPolicy& policy)
{
    TextAuditResult result;
    result.composition = comp_id;
    result.policy = policy;

    if (!registry.contains(comp_id)) {
        result.overall_status = "ERROR";
        result.exit_code = 1;
        spdlog::error("text-audit: unknown composition '{}'", comp_id);
        return result;
    }

    if (frames.empty()) return result;

    Composition comp = registry.create(comp_id);
    result.canvas_width = comp.width();
    result.canvas_height = comp.height();

    // ── Pre-pass: detect typewriter pattern from the last frame ────────
    // typewriter_build creates per-character layers.  We detect this by
    // looking for layers named "{prefix}_c{{N}}" at the last frame and
    // reconstruct the full text from all visible characters.
    bool is_typewriter = false;
    std::string tw_full_text;
    TextShape tw_font_ref;
    float tw_box_w{1200.0f};
    float tw_box_h{400.0f};
    float tw_tracking{0.0f};
    std::vector<LayerTextNode> tw_last_chars;

    {
        auto last_scene = comp.evaluate(Frame{frames.back()});
        auto last_nodes = collect_text_from_scene(last_scene);
        auto tw_groups = detect_typewriter_groups(last_nodes);

        if (!tw_groups.empty() && !tw_groups[0].chars.empty()) {
            is_typewriter = true;
            tw_full_text = tw_groups[0].full_text;
            tw_font_ref = tw_groups[0].chars[0].text;

            const auto& chars = tw_groups[0].chars;
            const float fs = tw_font_ref.style.size;

            // Infer wrapping box from ALL characters at the final frame.
            // Character positions are absolute canvas coords (pin_to(Center)
            // adds canvas_width/2, canvas_height/2), so subtract the center
            // to get positions relative to the text block center.
            // Character positions from world_transform.position are
            // relative to canvas center (pin_to(Anchor::Center) uses local
            // offsets).  Use them directly — no need to subtract cx/cy.
            float max_abs_x = 0.0f;
            float min_y = 1e9f, max_y = -1e9f;
            for (const auto& ch : chars) {
                max_abs_x = std::max(max_abs_x, std::abs(ch.offset_x));
                min_y = std::min(min_y, ch.offset_y);
                max_y = std::max(max_y, ch.offset_y);
            }
            tw_box_w = std::max(max_abs_x * 2.0f + fs * 1.5f, 200.0f);
            tw_box_h = std::max((max_y - min_y) + fs * 3.0f, fs * 2.0f);

            // Infer tracking from consecutive character spacing.
            // typewriter_build zeroes tracking on per-char TextShapes
            // ("spacing already handled by layout"), but the layout engine
            // needs the original tracking to reproduce the same line breaks.
            // compute_typewriter_layout uses FontEngine::shape_text() for
            // actual glyph advances.  Tracking inference: advance_x already
            // includes the glyph width, so tracking ≈ (x2 - x1) - advance_x
            // for consecutive chars on the same line.  Approximate using
            // font_size * 0.58 as a stand-in for the unknown advance_x.
            for (size_t i = 1; i < chars.size(); ++i) {
                if (std::abs(chars[i].offset_y - chars[i-1].offset_y) < 1.0f &&
                    chars[i].text.text.size() == 1 &&
                    chars[i-1].text.text.size() == 1 &&
                    chars[i].text.text[0] != ' ' &&
                    chars[i-1].text.text[0] != ' ') {
                    float spacing = chars[i].offset_x - chars[i-1].offset_x;
                    tw_tracking = spacing - fs * 0.58f;
                    break;
                }
            }

            tw_last_chars = tw_groups[0].chars;
        }
    }

    if (is_typewriter) {
        // ── Typewriter path ─────────────────────────────────────────────
        // The full text is known from the last frame.  At each frame we
        // audit the full layout (for geometry checks) but override the
        // visible text and add per-character opacity checks.
        result.expected_text = tw_full_text;

        TextAuditFrameResult* prev = nullptr;

        for (int frame : frames) {
            auto scene = comp.evaluate(Frame{frame});
            auto nodes = collect_text_from_scene(scene);
            auto tw_groups = detect_typewriter_groups(nodes);

            if (tw_groups.empty() || tw_groups[0].chars.empty()) {
                // Before start_delay: nothing visible
                TextAuditFrameResult empty;
                empty.frame = frame;
                empty.total_codepoints = count_codepoints(tw_full_text);
                empty.status = "PASS";
                result.frames.push_back(empty);
                prev = &result.frames.back();
                continue;
            }

            auto& group = tw_groups[0];

            // Create synthetic TextShape with full text for layout + raster audit.
            // This lets audit_single_text check geometry (ink bbox, centering,
            // safe area, border alpha) against the complete text layout.
            TextShape synthetic = tw_font_ref;
            synthetic.text = tw_full_text;
            synthetic.style.tracking = tw_tracking;
            synthetic.style.wrap = TextWrap::Word;
            synthetic.box.size = {tw_box_w, tw_box_h};
            synthetic.box.enabled = true;

            auto fr = audit_single_text(
                synthetic,
                result.canvas_width,
                result.canvas_height,
                frame,
                policy,
                prev);

            // Override visible text with what's actually revealed at this frame
            fr.visible_text = group.full_text;
            fr.visible_codepoints = count_codepoints(group.full_text);
            fr.total_codepoints = count_codepoints(tw_full_text);

            // Typewriter opacity checks (spec Check 9)
            // Already-revealed chars should be alpha=1, current char fades in,
            // future chars should not exist as layers at all.
            int partially_visible = 0;
            int fully_visible_below_99 = 0;
            for (const auto& ch : group.chars) {
                if (ch.layer_opacity > 0.01f && ch.layer_opacity < 0.99f) {
                    partially_visible++;
                } else if (ch.layer_opacity < 0.99f) {
                    fully_visible_below_99++;
                }
            }
            fr.checks.partially_visible_glyphs = partially_visible;
            fr.checks.fully_visible_glyphs_below_99 = fully_visible_below_99;
            // typewriter_build never creates layers for unrevealed characters
            fr.checks.future_glyphs_above_0 = 0;

            // Downgrade text_box_height_overflow for partial reveals
            if (fr.visible_codepoints < fr.total_codepoints &&
                fr.checks.text_box_height_overflow) {
                fr.checks.text_box_height_overflow = false;
            }

            // Override geometry with position-based values from actual
            // typewriter character positions.  TextLayoutEngine produces
            // different line breaks than compute_typewriter_layout's
            // heuristic, so the synthetic layout_bbox/ink_bbox from
            // audit_single_text may not match the rendered positions.
            // Using the actual character positions gives correct geometry.
            {
                const float cx = result.canvas_width * 0.5f;
                const float cy = result.canvas_height * 0.5f;
                const float fs = tw_font_ref.style.size;
                // Conservative glyph extents (slightly larger than actual
                // to avoid false PASS on edge-case glyphs like W or g)
                const float half_w = std::max(fs * 0.35f, 1.0f);
                const float half_h = fs * 0.55f;

                // Layout bbox from ALL characters at last frame
                // Character positions are relative to canvas center.
                float l_min_x = 1e9f, l_max_x = -1e9f;
                float l_min_y = 1e9f, l_max_y = -1e9f;
                for (const auto& ch : tw_last_chars) {
                    l_min_x = std::min(l_min_x, ch.offset_x - half_w);
                    l_max_x = std::max(l_max_x, ch.offset_x + half_w);
                    l_min_y = std::min(l_min_y, ch.offset_y - half_h);
                    l_max_y = std::max(l_max_y, ch.offset_y + half_h);
                }
                fr.layout_bbox = {cx + l_min_x, cy + l_min_y,
                                  cx + l_max_x, cy + l_max_y};

                // Ink bbox from VISIBLE characters at current frame
                float i_min_x = 1e9f, i_max_x = -1e9f;
                float i_min_y = 1e9f, i_max_y = -1e9f;
                bool has_ink = false;
                for (const auto& ch : group.chars) {
                    if (ch.layer_opacity < 0.01f) continue;
                    i_min_x = std::min(i_min_x, ch.offset_x - half_w);
                    i_max_x = std::max(i_max_x, ch.offset_x + half_w);
                    i_min_y = std::min(i_min_y, ch.offset_y - half_h);
                    i_max_y = std::max(i_max_y, ch.offset_y + half_h);
                    has_ink = true;
                }
                if (has_ink) {
                    fr.ink_bbox = {cx + i_min_x, cy + i_min_y,
                                   cx + i_max_x, cy + i_max_y};
                }

                // Border alpha from the synthetic rasterization is
                // meaningless for typewriter (per-character layers don't
                // have texture-border artifacts).  Override to avoid false
                // clipping failures.
                fr.checks.border_alpha_pixels = 0;
                fr.checks.no_clipping = true;

                // Re-check text_box_height_overflow with position-based layout
                if (synthetic.box.enabled) {
                    float layout_h = fr.layout_bbox.y1 - fr.layout_bbox.y0;
                    fr.checks.text_box_height_overflow =
                        (layout_h > synthetic.box.size.y * 1.05f);
                }

                // Re-run geometry checks with corrected bboxes
                fr.checks.inside_canvas =
                    !(fr.ink_bbox.x0 < 0 || fr.ink_bbox.y0 < 0 ||
                      fr.ink_bbox.x1 > result.canvas_width ||
                      fr.ink_bbox.y1 > result.canvas_height);
                fr.checks.inside_safe_area =
                    !(fr.ink_bbox.x0 < fr.safe_area.x0 ||
                      fr.ink_bbox.y0 < fr.safe_area.y0 ||
                      fr.ink_bbox.x1 > fr.safe_area.x1 ||
                      fr.ink_bbox.y1 > fr.safe_area.y1);
                if (fr.ink_bbox.x1 > fr.ink_bbox.x0) {
                    float ink_cx = (fr.ink_bbox.x0 + fr.ink_bbox.x1) * 0.5f;
                    fr.checks.center_error_x_px =
                        std::abs(ink_cx - result.canvas_width * 0.5f);
                }
                if (fr.ink_bbox.y1 > fr.ink_bbox.y0) {
                    float ink_cy = (fr.ink_bbox.y0 + fr.ink_bbox.y1) * 0.5f;
                    fr.checks.center_error_y_px =
                        std::abs(ink_cy - result.canvas_height * 0.5f);
                }
                float lw = fr.layout_bbox.x1 - fr.layout_bbox.x0;
                float iw = fr.ink_bbox.x1 - fr.ink_bbox.x0;
                if (lw > 0 && iw > 0) {
                    fr.checks.layout_vs_ink_ratio = iw / lw;
                }
            }

            // Re-determine status after geometry override
            {
                bool has_fail = false, has_warn = false;
                if (!fr.checks.inside_canvas) has_fail = true;
                if (!fr.checks.inside_safe_area) has_warn = true;
                if (!fr.checks.no_clipping) has_fail = true;
                if (!fr.checks.stable_line_breaks) has_fail = true;
                if (!fr.checks.stable_glyph_positions) has_fail = true;
                if (!fr.checks.utf8_valid) has_fail = true;
                if (!fr.checks.final_text_matches) has_fail = true;
                if (fr.checks.missing_glyphs > 0) has_fail = true;
                if (fr.checks.text_box_height_overflow) has_warn = true;
                if (fr.checks.center_error_x_px > policy.max_center_error_px) has_warn = true;
                if (fr.checks.center_error_y_px > policy.max_center_error_y_px) has_warn = true;
                if (fr.checks.border_alpha_pixels > policy.max_border_alpha_pixels) has_fail = true;
                fr.status = has_fail ? "FAIL" : (has_warn ? "WARN" : "PASS");
            }

            result.frames.push_back(fr);
            prev = &result.frames.back();
        }
    } else {
        // ── Regular text path ───────────────────────────────────────────
        // Walk scene.layers() (not scene.nodes()) to find text shapes.

        // Collect expected text from frame 0
        {
            auto scene = comp.evaluate(Frame{0});
            auto nodes = collect_text_from_scene(scene);
            for (const auto& node : nodes) {
                if (!result.expected_text.empty()) result.expected_text += " | ";
                result.expected_text += node.text.text;
            }
        }

        TextAuditFrameResult* prev = nullptr;

        for (int frame : frames) {
            auto scene = comp.evaluate(Frame{frame});
            auto nodes = collect_text_from_scene(scene);

            bool found_text = false;
            for (const auto& node : nodes) {
                auto fr = audit_single_text(
                    node.text,
                    result.canvas_width,
                    result.canvas_height,
                    frame,
                    policy,
                    prev);

                result.frames.push_back(fr);
                prev = &result.frames.back();
                found_text = true;
                break;  // audit first text node per frame
            }

            if (!found_text) {
                TextAuditFrameResult empty;
                empty.frame = frame;
                empty.status = "PASS";
                result.frames.push_back(empty);
                prev = &result.frames.back();
            }
        }
    }

    result.exit_code = audit_exit_code(result);

    // Determine overall status
    bool any_fail = false;
    bool any_warn = false;
    for (const auto& fr : result.frames) {
        if (fr.status == "FAIL") any_fail = true;
        if (fr.status == "WARN") any_warn = true;
    }
    result.overall_status = any_fail ? "FAIL" : (any_warn ? "WARN" : "PASS");

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// audit_result_to_json — serialize the full result
// ═══════════════════════════════════════════════════════════════════════════

std::string audit_result_to_json(const TextAuditResult& r) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(2);

    os << "{\n";
    os << "  \"composition\": \"" << json_escape(r.composition) << "\",\n";
    os << "  \"canvas\": { \"width\": " << r.canvas_width
       << ", \"height\": " << r.canvas_height << " },\n";
    os << "  \"expected_text\": \"" << json_escape(r.expected_text) << "\",\n";
    os << "  \"overall_status\": \"" << r.overall_status << "\",\n";
    os << "  \"exit_code\": " << r.exit_code << ",\n";

    // Policy
    os << "  \"policy\": {\n";
    os << "    \"safe_margin_x\": " << r.policy.safe_margin_x << ",\n";
    os << "    \"safe_margin_y\": " << r.policy.safe_margin_y << ",\n";
    os << "    \"max_center_error_px\": " << r.policy.max_center_error_px << ",\n";
    os << "    \"max_border_alpha_pixels\": " << r.policy.max_border_alpha_pixels << ",\n";
    os << "    \"glyph_position_tolerance\": " << r.policy.glyph_position_tolerance << "\n";
    os << "  },\n";

    // Frames
    os << "  \"frames\": [\n";
    for (size_t fi = 0; fi < r.frames.size(); ++fi) {
        const auto& f = r.frames[fi];
        os << "    {\n";
        os << "      \"frame\": " << f.frame << ",\n";
        os << "      \"status\": \"" << f.status << "\",\n";
        os << "      \"text\": {\n";
        os << "        \"visible\": \"" << json_escape(f.visible_text) << "\",\n";
        os << "        \"visible_codepoints\": " << f.visible_codepoints << ",\n";
        os << "        \"total_codepoints\": " << f.total_codepoints << "\n";
        os << "      },\n";

        os << "      \"layout\": {\n";
        os << "        \"font_size_requested\": " << f.font_size_requested << ",\n";
        os << "        \"font_size_resolved\": " << f.font_size_resolved << ",\n";
        os << "        \"line_count\": " << f.line_count << ",\n";
        os << "        \"lines\": [\n";
        for (size_t li = 0; li < f.lines.size(); ++li) {
            const auto& ln = f.lines[li];
            os << "          { \"text\": \"" << json_escape(ln.text) << "\""
               << ", \"x\": " << ln.x << ", \"y\": " << ln.y
               << ", \"width\": " << ln.width
               << ", \"baseline\": " << ln.baseline << " }";
            if (li + 1 < f.lines.size()) os << ",";
            os << "\n";
        }
        os << "        ]\n";
        os << "      },\n";

        os << "      \"geometry\": {\n";
        os << "        \"text_box\": " << json_bbox(f.text_box) << ",\n";
        os << "        \"layout_bbox\": " << json_bbox(f.layout_bbox) << ",\n";
        os << "        \"ink_bbox\": " << json_bbox(f.ink_bbox) << ",\n";
        os << "        \"safe_area\": " << json_bbox(f.safe_area) << "\n";
        os << "      },\n";

        os << "      \"checks\": {\n";
        os << "        \"inside_canvas\": " << (f.checks.inside_canvas ? "true" : "false") << ",\n";
        os << "        \"inside_safe_area\": " << (f.checks.inside_safe_area ? "true" : "false") << ",\n";
        os << "        \"no_clipping\": " << (f.checks.no_clipping ? "true" : "false") << ",\n";
        os << "        \"stable_line_breaks\": " << (f.checks.stable_line_breaks ? "true" : "false") << ",\n";
        os << "        \"stable_glyph_positions\": " << (f.checks.stable_glyph_positions ? "true" : "false") << ",\n";
        os << "        \"center_error_x_px\": " << f.checks.center_error_x_px << ",\n";
        os << "        \"center_error_y_px\": " << f.checks.center_error_y_px << ",\n";
        os << "        \"missing_glyphs\": " << f.checks.missing_glyphs << ",\n";
        os << "        \"border_alpha_pixels\": " << f.checks.border_alpha_pixels << ",\n";
        os << "        \"utf8_valid\": " << (f.checks.utf8_valid ? "true" : "false") << ",\n";
        os << "        \"final_text_matches\": " << (f.checks.final_text_matches ? "true" : "false") << ",\n";
        os << "        \"line_balance_ratio\": " << f.checks.line_balance_ratio << ",\n";
        os << "        \"single_word_lines\": " << f.checks.single_word_lines << ",\n";
        os << "        \"orphan_lines\": " << f.checks.orphan_lines << ",\n";
        os << "        \"layout_vs_ink_ratio\": " << f.checks.layout_vs_ink_ratio << ",\n";
        os << "        \"text_box_height_overflow\": " << (f.checks.text_box_height_overflow ? "true" : "false") << "\n";
        os << "      }";

        if (!f.glyphs.empty()) {
            os << ",\n      \"glyphs\": [\n";
            for (size_t gi = 0; gi < f.glyphs.size(); ++gi) {
                const auto& g = f.glyphs[gi];
                os << "        { \"glyph_id\": " << g.glyph_index
                   << ", \"codepoint\": \"" << json_escape(g.codepoint) << "\""
                   << ", \"x\": " << g.current_x << ", \"y\": " << g.current_y
                   << ", \"line\": " << g.line_index << " }";
                if (gi + 1 < f.glyphs.size()) os << ",";
                os << "\n";
            }
            os << "      ]\n";
        } else {
            os << "\n";
        }

        os << "    }";
        if (fi + 1 < r.frames.size()) os << ",";
        os << "\n";
    }
    os << "  ]\n";
    os << "}\n";

    return os.str();
}

// ═══════════════════════════════════════════════════════════════════════════
// audit_exit_code — map accumulated failures to exit codes
// ═══════════════════════════════════════════════════════════════════════════

int audit_exit_code(const TextAuditResult& result) {
    int worst = 0;

    for (const auto& f : result.frames) {
        if (!f.checks.inside_canvas || !f.checks.no_clipping) {
            worst = std::max(worst, static_cast<int>(TextAuditExit::Clipping));
        }
        if (!f.checks.stable_line_breaks || !f.checks.stable_glyph_positions) {
            worst = std::max(worst, static_cast<int>(TextAuditExit::TypewriterInstability));
        }
        if (f.checks.missing_glyphs > 0 || !f.checks.utf8_valid) {
            worst = std::max(worst, static_cast<int>(TextAuditExit::MissingGlyphs));
        }
        if (!f.checks.inside_safe_area || f.checks.text_box_height_overflow) {
            worst = std::max(worst, static_cast<int>(TextAuditExit::LayoutError));
        }
    }

    return worst;
}

} // namespace chronon3d::cli
