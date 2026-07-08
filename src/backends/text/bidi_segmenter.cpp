#include <chronon3d/backends/text/bidi_segmenter.hpp>

#include <cstdlib>  // std::getenv for CHRONON3D_FORCE_NO_FRIBIDI golden-test env override

#ifdef CHRONON3D_HAS_FRIBIDI
#include <fribidi.h>
#endif

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// M1.5#8 — CHRONON3D_FORCE_NO_FRIBIDI runtime override.
//
// The golden test (tests/text/test_text_font_resolver_golden.cpp) exercises
// the FriBidi-on AND FriBidi-off paths from a SINGLE build, so the golden
// snapshot can lock determinism end-to-end on either branch.
//
// When the env var is set to "1", `segment_bidi_runs` returns a single
// LTR run regardless of the CHRONON3D_HAS_FRIBIDI compile-time flag.
// This short-circuits the FriBidi dependency at test time.
//
// The check is a single `std::getenv` call (cached per-process via
// function-local static) — zero per-call cost.
namespace {
[[nodiscard]] bool force_no_fribidi_via_env() {
    static const bool flag = []() noexcept {
        const char* v = std::getenv("CHRONON3D_FORCE_NO_FRIBIDI");
        if (!v || v[0] == '\0') return false;
        // Treat any non-zero value as "on" (1, true, yes, etc.).
        return !(v[0] == '0' || v[0] == '\0');
    }();
    return flag;
}
} // namespace

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

    // M1.5#8 — golden-test env override.  When CHRONON3D_FORCE_NO_FRIBIDI
    // is set, skip the FriBidi branch unconditionally and return a
    // single LTR run.  This is the canonical "FriBidi opzionale,
    // fallback env" path the user spec calls out.
    if (force_no_fribidi_via_env()) {
        runs.push_back({std::string(text), TextDirection::LTR, 0});
        return runs;
    }

#ifdef CHRONON3D_HAS_FRIBIDI

    // ── Step 1: Convert UTF-8 to UTF-32 ────────────────────────────────
    const FriBidiStrIndex len = static_cast<FriBidiStrIndex>(text.size());

    // ── P1-#7: thread-local scratch (zero per-call heap alloc on hot path) ──
    // `segment_bidi_runs` is called per-text-shape per-frame on cinematic
    // scenes (~12k calls/sec); capacity preserved across calls (zero realloc).
    static thread_local std::vector<FriBidiChar> tl_logical;
    tl_logical.assign(len + 1, 0);  // +1 for null terminator
    auto& logical = tl_logical;

    // fribidi_charset_to_unicode returns the number of UTF-32 characters.
    FriBidiStrIndex utf32_len = fribidi_charset_to_unicode(
        FRIBIDI_CHAR_SET_UTF8,
        text.data(), len,
        logical.data());
    if (utf32_len == 0) return runs;

    // ── Step 2: Get bidi types ─────────────────────────────────────────
    // ── P1-#7: thread-local scratch buffer (capacity preserved) ─────
    static thread_local std::vector<FriBidiCharType> tl_bidi_types;
    tl_bidi_types.resize(utf32_len);
    auto& bidi_types = tl_bidi_types;
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
    // ── P1-#7: thread-local scratch buffer (capacity preserved) ─────
    static thread_local std::vector<FriBidiLevel> tl_embedding_levels;
    tl_embedding_levels.resize(utf32_len);
    auto& embedding_levels = tl_embedding_levels;
    FriBidiLevel max_level = fribidi_get_par_embedding_levels_ex(
        bidi_types.data(), nullptr, utf32_len,
        &par_type, embedding_levels.data());

    // ── Step 5: Group into directional runs (logical order) ────────────
    // Consecutive characters with the same embedding level form a run.
    // Even level = LTR, odd level = RTL.
    // We need to convert back from UTF-32 indices to UTF-8 byte offsets.
    //
    // Precompute the UTF-8 byte offset for each UTF-32 character.
    static thread_local std::vector<std::size_t> tl_utf8_offsets;
    tl_utf8_offsets.resize(utf32_len);
    auto& utf8_offsets = tl_utf8_offsets;
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

    // ── Step 6: Merge consecutive runs with the same direction (in-place) ─
    // (FriBidi may produce adjacent runs with different embedding levels
    //  but the same visual direction, e.g. level 0→2→0 for nested LTR).
    // P1-#7: in-place merge eliminates a 5th std::vector<BidiRun> allocation;
    // the surviving runs stay in the same `runs` vector (capacity-stable).
    if (runs.size() > 1) {
        std::size_t write_idx = 0;
        for (std::size_t i = 1; i < runs.size(); ++i) {
            if (runs[i].direction == runs[write_idx].direction) {
                runs[write_idx].text += runs[i].text;
            } else {
                ++write_idx;
                if (write_idx != i) {
                    runs[write_idx] = std::move(runs[i]);
                }
            }
        }
        runs.resize(write_idx + 1);
    }

    return runs;

#else
    // ── Fallback: single LTR run ───────────────────────────────────────
    runs.push_back({std::string(text), TextDirection::LTR, 0});
    return runs;
#endif
}

} // namespace chronon3d
