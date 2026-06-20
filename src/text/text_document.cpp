// ═══════════════════════════════════════════════════════════════════════════
// text_document.cpp — TextDocument implementation (validation, splitting, hashing)
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_document.hpp>
#include <xxhash.h>

#include <algorithm>
#include <string_view>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// UTF-8 validation helper
// ═══════════════════════════════════════════════════════════════════════════

namespace {

/// Validate UTF-8, including overlong-encoding and surrogate-pair rejection.
/// Not an exhaustive grapheme-cluster check, but catches truncated sequences,
/// overlong encodings, invalid continuation bytes, and surrogates.
[[nodiscard]] bool is_valid_utf8(std::string_view sv) {
    for (size_t i = 0; i < sv.size();) {
        unsigned char c = static_cast<unsigned char>(sv[i]);
        if (c < 0x80) {
            ++i;
            continue;
        }

        // Count leading 1-bits to determine sequence length.
        int len = 0;
        uint32_t cp = 0;
        if ((c & 0xE0) == 0xC0) {        // 110xxxxx → 2-byte sequence
            len = 2;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {  // 1110xxxx → 3-byte
            len = 3;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {  // 11110xxx → 4-byte
            len = 4;
            cp = c & 0x07u;
        } else {
            return false;  // invalid leading byte (10xxxxxx or 11111xxx)
        }

        // Truncated sequence.
        if (i + static_cast<size_t>(len) > sv.size()) return false;

        // Validate continuation bytes and decode.
        for (int j = 1; j < len; ++j) {
            unsigned char cont = static_cast<unsigned char>(sv[i + j]);
            if ((cont & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cont & 0x3Fu);
        }

        // ── Overlong / invalid range checks ────────────────────────────
        if (len == 2) {
            if (cp < 0x80) return false;       // overlong (e.g. 0xC0 0xAF)
        } else if (len == 3) {
            if (cp < 0x800) return false;      // overlong
            // Surrogate range (U+D800 – U+DFFF) is invalid in UTF-8.
            if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        } else if (len == 4) {
            if (cp < 0x10000) return false;    // overlong
            if (cp > 0x10FFFF) return false;   // beyond Unicode maximum
        }

        i += static_cast<size_t>(len);
    }
    return true;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// TextDocument::validate
// ═══════════════════════════════════════════════════════════════════════════

bool TextDocument::validate() const {
    // ── UTF-8 validity ──────────────────────────────────────────────────
    if (!utf8.empty() && !is_valid_utf8(utf8)) {
        return false;
    }

    const size_t len = utf8.size();

    // ── Spans ───────────────────────────────────────────────────────────
    size_t prev_end = 0;
    for (size_t i = 0; i < spans.size(); ++i) {
        const auto& s = spans[i];

        // Bounds
        if (s.byte_start > len || s.byte_end > len) return false;
        if (s.byte_start >= s.byte_end) return false;

        // Sorted + non-overlapping
        if (s.byte_start < prev_end) return false;  // overlap or out-of-order
        prev_end = s.byte_end;
    }

    // ── Paragraphs ──────────────────────────────────────────────────────
    if (paragraphs.empty()) {
        // Empty paragraphs vector is valid — split_paragraphs() will fill it.
        return true;
    }

    prev_end = 0;
    for (size_t i = 0; i < paragraphs.size(); ++i) {
        const auto& p = paragraphs[i];

        // Bounds
        if (p.byte_start > len || p.byte_end > len) return false;
        if (p.byte_start >= p.byte_end) return false;

        // Sorted + non-overlapping
        if (p.byte_start < prev_end) return false;
        prev_end = p.byte_end;
    }

    // Must cover the entire document.
    if (prev_end != len) return false;

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// TextDocument::split_paragraphs
// ═══════════════════════════════════════════════════════════════════════════

size_t TextDocument::split_paragraphs(const ParagraphStyle& default_style) {
    // If paragraphs are already populated, validate but don't overwrite.
    if (!paragraphs.empty()) {
        if (validate()) {
            return paragraphs.size();
        }
        paragraphs.clear();
    }

    if (utf8.empty()) {
        // Empty document → single empty paragraph.
        paragraphs.push_back(ParagraphRange{
            .byte_start = 0,
            .byte_end = 0,
            .style = default_style,
        });
        return 1;
    }

    paragraphs.clear();
    size_t para_start = 0;
    const size_t len = utf8.size();

    for (size_t i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        bool is_break = false;
        bool is_u2029 = false;

        if (c == '\n') {
            is_break = true;
        } else if (c == 0xE2 && i + 2 < len) {
            // U+2029 PARAGRAPH SEPARATOR: E2 80 A9
            unsigned char c1 = static_cast<unsigned char>(utf8[i + 1]);
            unsigned char c2 = static_cast<unsigned char>(utf8[i + 2]);
            if (c1 == 0x80 && c2 == 0xA9) {
                is_break = true;
                is_u2029 = true;
            }
        }

        if (is_break) {
            // Emit the paragraph up to (but not including) the break.
            if (i > para_start) {
                paragraphs.push_back(ParagraphRange{
                    .byte_start = para_start,
                    .byte_end = i,
                    .style = default_style,
                });
            } else {
                // Consecutive breaks → empty paragraph.
                paragraphs.push_back(ParagraphRange{
                    .byte_start = i,
                    .byte_end = i,
                    .style = default_style,
                });
            }

            if (is_u2029) {
                // U+2029 is a 3-byte sequence.  Consume it fully and move on.
                i += 2;        // loop will increment the last byte
                para_start = i + 1;
            } else {
                // '\n': collapse consecutive newlines into a single break.
                // \n\n means one paragraph boundary, not two.
                size_t next = i + 1;
                while (next < len && utf8[next] == '\n') {
                    ++next;
                }
                i = next - 1;   // loop increment will move to `next`
                para_start = next;
            }
        }
    }

    // Final paragraph (from last break to end).
    if (para_start < len) {
        paragraphs.push_back(ParagraphRange{
            .byte_start = para_start,
            .byte_end = len,
            .style = default_style,
        });
    } else if (len > 0 && para_start == len) {
        // Document ended with a break — final empty paragraph.
        paragraphs.push_back(ParagraphRange{
            .byte_start = len,
            .byte_end = len,
            .style = default_style,
        });
    }

    return paragraphs.size();
}

// ═══════════════════════════════════════════════════════════════════════════
// Hashing
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// Small XXH64-based hash combiners (matching the codebase convention from
// render_graph_hashing.hpp).

[[nodiscard]] u64 hcombine(u64 seed, u64 value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

[[nodiscard]] u64 hbytes(const void* data, size_t size) {
    return XXH64(data, size, 0);
}

[[nodiscard]] u64 hstring(std::string_view sv) {
    return XXH64(sv.data(), sv.size(), 0);
}

template <typename T>
[[nodiscard]] u64 hval(const T& v) {
    return hbytes(&v, sizeof(v));
}

// ── hash_font_spec ─────────────────────────────────────────────────────

[[nodiscard]] u64 hash_font_spec(const FontSpec& fs) {
    u64 seed = hstring(fs.font_path);
    seed = hcombine(seed, hstring(fs.font_family));
    seed = hcombine(seed, hval(fs.font_weight));
    seed = hcombine(seed, hstring(fs.font_style));
    seed = hcombine(seed, hval(fs.font_size));
    return seed;
}

// ── hash_text_appearance ───────────────────────────────────────────────

[[nodiscard]] u64 hash_text_appearance(const TextAppearanceSpec& a) {
    u64 seed = hbytes(&a.color, sizeof(a.color));
    seed = hcombine(seed, hbytes(&a.paint, sizeof(a.paint)));

    // Shadows
    seed = hcombine(seed, hval(a.shadows.size()));
    for (const auto& s : a.shadows) {
        seed = hcombine(seed, hval(s.enabled));
        seed = hcombine(seed, hbytes(&s.offset, sizeof(s.offset)));
        seed = hcombine(seed, hval(s.blur));
        seed = hcombine(seed, hval(s.opacity));
        seed = hcombine(seed, hbytes(&s.color, sizeof(s.color)));
    }

    // Material (binary hash — matches cache_key pattern)
    seed = hcombine(seed, hbytes(&a.material, sizeof(a.material)));

    // Box style
    seed = hcombine(seed, hbytes(&a.box_style, sizeof(a.box_style)));

    return seed;
}

} // namespace

u64 hash_text_style_span(const TextStyleSpan& span) {
    u64 seed = hval(span.byte_start);
    seed = hcombine(seed, hval(span.byte_end));

    if (span.font)       seed = hcombine(seed, hash_font_spec(*span.font));
    else                 seed = hcombine(seed, 0xFFFFFFFFFFFFFFFFULL);

    if (span.appearance) seed = hcombine(seed, hash_text_appearance(*span.appearance));
    else                 seed = hcombine(seed, 0xFFFFFFFFFFFFFFFFULL);

    if (span.tracking)   seed = hcombine(seed, hval(*span.tracking));
    else                 seed = hcombine(seed, 0xFFFFFFFFFFFFFFFFULL);

    if (span.baseline_shift) seed = hcombine(seed, hval(*span.baseline_shift));
    else                     seed = hcombine(seed, 0xFFFFFFFFFFFFFFFFULL);

    return seed;
}

u64 hash_paragraph_style(const ParagraphStyle& ps) {
    u64 seed = hval(static_cast<u8>(ps.composer));
    seed = hcombine(seed, hval(static_cast<u8>(ps.justification)));

    // Indentation
    seed = hcombine(seed, hval(ps.left_indent));
    seed = hcombine(seed, hval(ps.right_indent));
    seed = hcombine(seed, hval(ps.first_line_indent));

    // Vertical spacing
    seed = hcombine(seed, hval(ps.space_before));
    seed = hcombine(seed, hval(ps.space_after));

    // Spacing limits (pack as 9 floats)
    seed = hcombine(seed, hval(ps.spacing.word_min));
    seed = hcombine(seed, hval(ps.spacing.word_desired));
    seed = hcombine(seed, hval(ps.spacing.word_max));
    seed = hcombine(seed, hval(ps.spacing.letter_min));
    seed = hcombine(seed, hval(ps.spacing.letter_desired));
    seed = hcombine(seed, hval(ps.spacing.letter_max));
    seed = hcombine(seed, hval(ps.spacing.glyph_scale_min));
    seed = hcombine(seed, hval(ps.spacing.glyph_scale_desired));
    seed = hcombine(seed, hval(ps.spacing.glyph_scale_max));

    // Hanging punctuation
    seed = hcombine(seed, hval(ps.hanging_punctuation));
    seed = hcombine(seed, hval(ps.hanging_limit));

    // Hyphenation
    seed = hcombine(seed, hval(ps.hyphenation));
    seed = hcombine(seed, hval(ps.minimum_word_length));
    seed = hcombine(seed, hval(ps.minimum_prefix));
    seed = hcombine(seed, hval(ps.minimum_suffix));

    // Widow/orphan
    seed = hcombine(seed, hval(ps.widow_lines));
    seed = hcombine(seed, hval(ps.orphan_lines));

    // Direction
    seed = hcombine(seed, hval(static_cast<u8>(ps.direction)));

    // Spacing collapse policy
    seed = hcombine(seed, hval(static_cast<u8>(ps.spacing_collapse)));

    return seed;
}

u64 hash_paragraph_range(const ParagraphRange& pr) {
    u64 seed = hval(pr.byte_start);
    seed = hcombine(seed, hval(pr.byte_end));
    seed = hcombine(seed, hash_paragraph_style(pr.style));
    return seed;
}

u64 hash_text_document(const TextDocument& doc) {
    u64 seed = hstring(doc.utf8);

    // Defaults: hash font, layout, appearance, position.
    // hbytes() on the full layout struct includes the new paragraph field
    // since ParagraphStyle is now a member of TextLayoutSpec.
    seed = hcombine(seed, hash_font_spec(doc.defaults.font));
    seed = hcombine(seed, hash_text_appearance(doc.defaults.appearance));
    seed = hcombine(seed, hbytes(&doc.defaults.layout, sizeof(doc.defaults.layout)));
    seed = hcombine(seed, hbytes(&doc.defaults.position, sizeof(doc.defaults.position)));

    // Spans
    seed = hcombine(seed, hval(doc.spans.size()));
    for (const auto& s : doc.spans) {
        seed = hcombine(seed, hash_text_style_span(s));
    }

    // Paragraphs
    seed = hcombine(seed, hval(doc.paragraphs.size()));
    for (const auto& p : doc.paragraphs) {
        seed = hcombine(seed, hash_paragraph_range(p));
    }

    return seed;
}

} // namespace chronon3d
