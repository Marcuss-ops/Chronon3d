#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace chronon3d::cli {

// ── Exit codes ─────────────────────────────────────────────────────────────
enum class TextAuditExit : int {
    Pass            = 0,
    LayoutError     = 1,
    Clipping        = 2,
    TypewriterInstability = 3,
    MissingGlyphs   = 4,
    VisualDiff      = 5,
};

// ── Audit Status enum (replaces std::string "PASS"|"FAIL"|"WARN") ──────────
// Use AuditStatus (instead of std::string) for the .status / .overall_status
// fields below. The four explicit states cover the audit vocabulary observed
// in production today (PASS/FAIL/WARN) plus a WarnPartial slot for future
// typewriter-opacity mid-state reporting (see text_audit_engine.cpp).
enum class AuditStatus {
    Pass,
    Fail,
    Warn,
    WarnPartial,
};

// to_string: stable bijective stringification for JSON dump + spdlog reports.
// Unknown enum values fall through to a defensive sentinel (loud, observable,
// never silent) per AGENTS.md fail-loud principle.
inline std::string to_string(AuditStatus s) {
    switch (s) {
        case AuditStatus::Pass: return "PASS";
        case AuditStatus::Fail: return "FAIL";
        case AuditStatus::Warn: return "WARN";
        case AuditStatus::WarnPartial: return "WARN_PARTIAL";
    }
    return "WARN_UNRECOGNIZED_ENUM_VALUE";
}

// from_string: fail-closed — unknown strings collapse to Fail so an
// unintended external state never silently re-classifies as PASS.
inline AuditStatus from_string(const std::string& s) {
    if (s == "PASS") return AuditStatus::Pass;
    if (s == "FAIL") return AuditStatus::Fail;
    if (s == "WARN") return AuditStatus::Warn;
    if (s == "WARN_PARTIAL") return AuditStatus::WarnPartial;
    return AuditStatus::Fail;
}

// ── Policy (configurable thresholds) ───────────────────────────────────────
struct TextAuditPolicy {
    float safe_margin_x{0.05f};         // fraction of canvas width
    float safe_margin_y{0.05f};         // fraction of canvas height
    float max_center_error_px{2.0f};    // max allowed centering error (px)
    float max_center_error_y_px{3.0f};  // max allowed vertical center error
    int   max_border_alpha_pixels{0};   // alpha pixels touching texture border
    int   max_lines{0};                 // 0 = no limit
    float min_line_balance_ratio{0.35f};
    float glyph_position_tolerance{0.01f}; // max glyph drift between frames
    float layout_vs_ink_ratio_min{0.35f};  // suspicious if ink/layout < this
    int   alpha_threshold{8};           // alpha > this counts as "ink"
};

// ── Per-line info ──────────────────────────────────────────────────────────
struct TextAuditLineInfo {
    std::string text;
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float baseline{0.0f};
};

// ── Per-glyph info (for stability tracking) ────────────────────────────────
struct TextAuditGlyphInfo {
    int glyph_index{0};
    std::string codepoint;       // UTF-8 representation
    float final_x{0.0f};
    float final_y{0.0f};
    float current_x{0.0f};
    float current_y{0.0f};
    int   line_index{0};
    float alpha{1.0f};
};

// ── Geometry bounding box ──────────────────────────────────────────────────
struct TextAuditBBox {
    float x0{0.0f}, y0{0.0f}, x1{0.0f}, y1{0.0f};
};

// ── Per-frame checks ───────────────────────────────────────────────────────
struct TextAuditChecks {
    bool  inside_canvas{true};
    bool  inside_safe_area{true};
    bool  no_clipping{true};
    bool  stable_line_breaks{true};
    bool  stable_glyph_positions{true};
    float center_error_x_px{0.0f};
    float center_error_y_px{0.0f};
    int   missing_glyphs{0};
    int   border_alpha_pixels{0};
    int   alpha_pixels_top{0};
    int   alpha_pixels_bottom{0};
    int   alpha_pixels_left{0};
    int   alpha_pixels_right{0};
    int   fully_visible_glyphs_below_99{0}; // opacity check
    int   future_glyphs_above_0{0};
    int   partially_visible_glyphs{0};
    float line_balance_ratio{1.0f};
    int   single_word_lines{0};
    int   orphan_lines{0};
    bool  utf8_valid{true};
    bool  final_text_matches{true};
    float layout_vs_ink_ratio{1.0f};
    bool  text_box_height_overflow{false};
};

// ── Per-frame result ───────────────────────────────────────────────────────
struct TextAuditFrameResult {
    int frame{0};
    std::string visible_text;
    int visible_codepoints{0};
    int total_codepoints{0};
    // Layout
    float font_size_requested{0.0f};
    float font_size_resolved{0.0f};
    int   line_count{0};
    std::vector<TextAuditLineInfo> lines;
    // Geometry
    TextAuditBBox text_box;
    TextAuditBBox layout_bbox;
    TextAuditBBox ink_bbox;
    TextAuditBBox safe_area;
    // Glyphs
    std::vector<TextAuditGlyphInfo> glyphs;
    // Checks
    TextAuditChecks checks;
    // Status
    AuditStatus status; // see AuditStatus enum above (was std::string "PASS"|"FAIL"|"WARN")
};

// ── Full audit result ──────────────────────────────────────────────────────
struct TextAuditResult {
    std::string composition;
    int canvas_width{1920};
    int canvas_height{1080};
    std::string expected_text;
    TextAuditPolicy policy;
    std::vector<TextAuditFrameResult> frames;
    AuditStatus overall_status; // see AuditStatus enum above (was std::string "PASS"|"FAIL"|"WARN")
    int exit_code{0};
};

} // namespace chronon3d::cli
