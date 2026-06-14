#include <chronon3d/backends/text/bidi_segmenter.hpp>

#include <fribidi/fribidi.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d {

// ── UTF-8 → UTF-32 conversion (code-point iterator) ─────────────────────

namespace {

/// Decode the next UTF-8 code point at `offset`, advance `offset` past it.
/// Returns U+FFFD on invalid sequences.
char32_t utf8_to_cp(std::string_view sv, size_t& offset) {
    if (offset >= sv.size()) return 0;
    const unsigned char lead = static_cast<unsigned char>(sv[offset]);
    if (lead < 0x80) {
        ++offset;
        return lead;
    }
    size_t len = 1;
    if ((lead & 0xE0) == 0xC0) len = 2;
    else if ((lead & 0xF0) == 0xE0) len = 3;
    else if ((lead & 0xF8) == 0xF0) len = 4;
    else { ++offset; return 0xFFFD; }

    if (offset + len > sv.size()) { ++offset; return 0xFFFD; }

    char32_t cp;
    switch (len) {
        case 2: cp = ((lead & 0x1F) << 6)  | (static_cast<unsigned char>(sv[offset+1]) & 0x3F); break;
        case 3: cp = ((lead & 0x0F) << 12) | ((static_cast<unsigned char>(sv[offset+1]) & 0x3F) << 6)  | (static_cast<unsigned char>(sv[offset+2]) & 0x3F); break;
        case 4: cp = ((lead & 0x07) << 18) | ((static_cast<unsigned char>(sv[offset+1]) & 0x3F) << 12) | ((static_cast<unsigned char>(sv[offset+2]) & 0x3F) << 6)  | (static_cast<unsigned char>(sv[offset+3]) & 0x3F); break;
        default: cp = 0xFFFD; break;
    }
    offset += len;
    return cp;
}

} // anonymous namespace

// ── segment_bidi_runs ─────────────────────────────────────────────────────

std::vector<BidiRun> segment_bidi_runs(std::string_view text, int base_dir) {
    std::vector<BidiRun> runs;
    if (text.empty()) return runs;

    // ── 1. Convert UTF-8 → UTF-32 ────────────────────────────────────
    // First pass: count code points.
    size_t cp_count = 0;
    for (size_t i = 0; i < text.size();) {
        utf8_to_cp(text, i); // advance i
        ++cp_count;
    }
    if (cp_count == 0) return runs;

    // Allocate and fill FriBidiChar array + byte-offset map.
    std::vector<FriBidiChar> fribidi_str(cp_count);
    std::vector<size_t> byte_offsets(cp_count); // byte offset of each cp
    FriBidiStrIndex len = static_cast<FriBidiStrIndex>(cp_count);

    {
        size_t bi = 0;
        for (size_t i = 0; i < text.size();) {
            byte_offsets[bi] = i;
            fribidi_str[bi] = static_cast<FriBidiChar>(utf8_to_cp(text, i));
            ++bi;
        }
    }

    // ── 2. Get bidi types and embedding levels ────────────────────────
    std::vector<FriBidiCharType> bidi_types(len);
    fribidi_get_bidi_types(fribidi_str.data(), len, bidi_types.data());

    std::vector<FriBidiLevel> embedding_levels(len, 0);
    FriBidiParType pbase_dir = static_cast<FriBidiParType>(base_dir);

    // If base_dir is 0 (auto), set to FRIBIDI_PAR_ON to trigger auto-detection.
    if (pbase_dir == 0) {
        pbase_dir = FRIBIDI_PAR_ON;
    }

    FriBidiLevel max_level = fribidi_get_par_embedding_levels_ex(
        bidi_types.data(),
        nullptr, // bracket_types — not needed, no pair matching
        len,
        &pbase_dir,
        embedding_levels.data()
    );

    if (max_level == 0) {
        // Error — return single run with auto-detect from text
        TextDirection fallback_dir = TextDirection::LTR;
        // Check the first strong character for RTL
        for (size_t i = 0; i < len; ++i) {
            if (FRIBIDI_IS_STRONG(bidi_types[i])) {
                if (FRIBIDI_IS_RTL(bidi_types[i])) {
                    fallback_dir = TextDirection::RTL;
                }
                break;
            }
        }
        runs.push_back({std::string(text), fallback_dir, 0});
        return runs;
    }

    // ── 3. Group characters with the same level parity into runs ─────
    // Characters with even embedding level → LTR, odd → RTL.
    // We build runs in LOGICAL order (the order they appear in the input).
    // Each run is tagged with its resolved direction.

    // Group consecutive code points with same embedding level parity.
    // Characters with even level → LTR, odd → RTL.
    size_t run_start = 0;
    while (run_start < cp_count) {
        const bool is_rtl = (embedding_levels[run_start] % 2) == 1;
        size_t run_end = run_start + 1;

        while (run_end < cp_count) {
            const bool next_rtl = (embedding_levels[run_end] % 2) == 1;
            if (next_rtl != is_rtl) break;
            ++run_end;
        }

        // Build the substring for this run.
        size_t byte_start = byte_offsets[run_start];
        size_t byte_end = (run_end < cp_count)
            ? byte_offsets[run_end]
            : text.size();

        BidiRun run;
        run.text = std::string(text.substr(byte_start, byte_end - byte_start));
        run.direction = is_rtl ? TextDirection::RTL : TextDirection::LTR;
        run.byte_offset = byte_start;
        runs.push_back(std::move(run));

        run_start = run_end;
    }

    return runs;
}

} // namespace chronon3d
