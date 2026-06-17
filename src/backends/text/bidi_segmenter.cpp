#include <chronon3d/backends/text/bidi_segmenter.hpp>

#ifdef CHRONON3D_HAS_FRIBIDI
#include <fribidi.h>
#endif

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d {

// ── segment_bidi_runs ─────────────────────────────────────────────────────
//
// Bi-directional text segmentation using the Unicode Bidirectional Algorithm
// (UAX #9) as implemented by the GNU FriBidi library.
//
// Algorithm:
//   1. Convert UTF-8 → UTF-32 (FriBidiChar)
//   2. Determine bidi types for each character
//   3. Determine paragraph base direction (auto / LTR / RTL)
//   4. Compute embedding levels for the entire paragraph
//   5. Group consecutive characters with same embedding level into logical runs
//   6. Even levels → LTR, odd levels → RTL

std::vector<BidiRun> segment_bidi_runs(std::string_view text, int base_dir) {
    std::vector<BidiRun> runs;
    if (text.empty()) return runs;

#ifdef CHRONON3D_HAS_FRIBIDI

    // ── Step 1: Convert UTF-8 to UTF-32 ────────────────────────────────
    const FriBidiStrIndex len = static_cast<FriBidiStrIndex>(text.size());
    std::vector<FriBidiChar> logical(len + 1, 0);  // +1 for null terminator
    // fribidi_charset_to_unicode returns the number of UTF-32 characters.
    FriBidiStrIndex utf32_len = fribidi_charset_to_unicode(
        FRIBIDI_CHAR_SET_UTF8,
        text.data(), len,
        logical.data());
    if (utf32_len == 0) return runs;

    // ── Step 2: Get bidi types ─────────────────────────────────────────
    std::vector<FriBidiCharType> bidi_types(utf32_len);
    fribidi_get_bidi_types(logical.data(), utf32_len, bidi_types.data());

    // ── Step 3: Determine paragraph direction ──────────────────────────
    FriBidiParType par_type = FRIBIDI_PAR_ON;  // auto-detect
    if (base_dir == 1) {
        par_type = FRIBIDI_PAR_LTR;
    } else if (base_dir == 2) {
        par_type = FRIBIDI_PAR_RTL;
    } else {
        // Auto-detect: find first strong character
        par_type = fribidi_get_par_direction(bidi_types.data(), utf32_len);
        if (par_type == FRIBIDI_PAR_ON) par_type = FRIBIDI_PAR_LTR;
    }

    // ── Step 4: Compute embedding levels ───────────────────────────────
    std::vector<FriBidiLevel> embedding_levels(utf32_len);
    FriBidiLevel max_level = fribidi_get_par_embedding_levels_ex(
        bidi_types.data(), nullptr, utf32_len,
        &par_type, embedding_levels.data());

    // ── Step 5: Group into directional runs (logical order) ────────────
    // Consecutive characters with the same embedding level form a run.
    // Even level = LTR, odd level = RTL.
    // We need to convert back from UTF-32 indices to UTF-8 byte offsets.
    //
    // Precompute the UTF-8 byte offset for each UTF-32 character.
    std::vector<std::size_t> utf8_offsets(utf32_len);
    std::size_t byte_pos = 0;
    for (FriBidiStrIndex i = 0; i < utf32_len; ++i) {
        utf8_offsets[i] = byte_pos;
        // Advance past this UTF-8 character
        const FriBidiChar cp = logical[i];
        if (cp < 0x80) {
            byte_pos += 1;
        } else if (cp < 0x800) {
            byte_pos += 2;
        } else if (cp < 0x10000) {
            byte_pos += 3;
        } else {
            byte_pos += 4;
        }
    }

    // Build runs from consecutive same-level characters.
    FriBidiStrIndex run_start = 0;
    FriBidiLevel current_level = embedding_levels[0];

    for (FriBidiStrIndex i = 1; i <= utf32_len; ++i) {
        const bool level_changed = (i == utf32_len) ||
            (embedding_levels[i] != current_level);

        if (level_changed) {
            // Extract the UTF-8 substring for this run.
            const std::size_t run_byte_start = utf8_offsets[run_start];
            const std::size_t run_byte_end = (i < utf32_len)
                ? utf8_offsets[i]
                : text.size();
            const std::size_t run_byte_len = run_byte_end - run_byte_start;

            BidiRun run;
            run.text = std::string(text.substr(run_byte_start, run_byte_len));
            // Even level → LTR, odd level → RTL
            run.direction = (current_level % 2 == 0)
                ? TextDirection::LTR : TextDirection::RTL;
            run.byte_offset = run_byte_start;
            runs.push_back(std::move(run));

            if (i < utf32_len) {
                run_start = i;
                current_level = embedding_levels[i];
            }
        }
    }

    // ── Step 6: Merge consecutive runs with the same direction ─────────
    // (FriBidi may produce adjacent runs with different embedding levels
    //  but the same visual direction, e.g. level 0→2→0 for nested LTR).
    if (runs.size() > 1) {
        std::vector<BidiRun> merged;
        merged.push_back(std::move(runs[0]));
        for (std::size_t i = 1; i < runs.size(); ++i) {
            if (runs[i].direction == merged.back().direction) {
                merged.back().text += runs[i].text;
            } else {
                merged.push_back(std::move(runs[i]));
            }
        }
        runs = std::move(merged);
    }

    return runs;

#else
    // ── Fallback: single LTR run ───────────────────────────────────────
    runs.push_back({std::string(text), TextDirection::LTR, 0});
    return runs;
#endif
}

} // namespace chronon3d
