#pragma once
// ─── text_audit_helpers.hpp ──────────────────────────────────────────────
//
// FASE 2 Step 1 — extracted from text_audit_engine.cpp.
// Low-level helpers: UTF-8 utilities, JSON escape, border alpha counting,
// ink bounding-box computation.
// ─────────────────────────────────────────────────────────────────────────

#include "text_audit_types.hpp"

#include <string>
#include <string_view>

namespace chronon3d::cli {

// ── UTF-8 helpers ─────────────────────────────────────────────────────────

/// Count the number of Unicode codepoints in a UTF-8 string.
int count_codepoints(const std::string& s);

/// Check if the string contains U+FFFD (replacement character).
bool has_replacement_char(const std::string& s);

// ── JSON serialization helpers ────────────────────────────────────────────

/// Escape a string for safe inclusion in JSON double-quoted strings.
std::string json_escape(const std::string& s);

/// Format a TextAuditBBox as a JSON array string (e.g. "[1.0, 2.0, 3.0, 4.0]").
std::string json_bbox(const TextAuditBBox& b);

// ── Border alpha counting ────────────────────────────────────────────────

struct BorderAlphaResult {
    int top{0}, bottom{0}, left{0}, right{0};
    int total{0};
};

/// Scan the four edges of a BLImage for pixels with alpha above threshold.
BorderAlphaResult count_border_alpha(const BLImage& img, int alpha_threshold);

// ── Ink bbox from rendered image ──────────────────────────────────────────

struct InkBBoxResult {
    TextAuditBBox bbox;
    bool has_ink{false};
    int ink_pixel_count{0};
};

/// Find the bounding box of non-transparent pixels in a rendered image.
InkBBoxResult compute_ink_bbox(const BLImage& img, int alpha_threshold);

} // namespace chronon3d::cli
