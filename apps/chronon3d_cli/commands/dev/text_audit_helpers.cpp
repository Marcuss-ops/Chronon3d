#include "text_audit_helpers.hpp"

#include <blend2d/image.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cctype>

namespace chronon3d::cli {

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
    return s.find("\xEF\xBF\xBD") != std::string::npos;
}

// ── JSON serialization helpers ────────────────────────────────────────────

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\"':  out += "\\\""; break;
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

// ── Border alpha counting ────────────────────────────────────────────────

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

    for (int x = 0; x < w; ++x) {
        if (alpha_at(x, 0) > alpha_threshold) ++result.top;
    }
    for (int x = 0; x < w; ++x) {
        if (alpha_at(x, h - 1) > alpha_threshold) ++result.bottom;
    }
    for (int y = 1; y < h - 1; ++y) {
        if (alpha_at(0, y) > alpha_threshold) ++result.left;
    }
    for (int y = 1; y < h - 1; ++y) {
        if (alpha_at(w - 1, y) > alpha_threshold) ++result.right;
    }

    result.total = result.top + result.bottom + result.left + result.right;
    return result;
}

// ── Ink bbox from rendered image ──────────────────────────────────────────

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
        result.bbox.x0 = static_cast<float>(std::max(0, min_x - 1));
        result.bbox.y0 = static_cast<float>(std::max(0, min_y - 1));
        result.bbox.x1 = static_cast<float>(std::min(w - 1, max_x + 1));
        result.bbox.y1 = static_cast<float>(std::min(h - 1, max_y + 1));
        result.has_ink = true;
        result.ink_pixel_count = count;
    }

    return result;
}

} // namespace chronon3d::cli
