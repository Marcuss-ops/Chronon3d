#include "text_audit_types.hpp"

#include <cstdio>
#include <sstream>
#include <iomanip>
#include <string>

namespace chronon3d::cli {

namespace {

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

} // anonymous namespace

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

} // namespace chronon3d::cli
