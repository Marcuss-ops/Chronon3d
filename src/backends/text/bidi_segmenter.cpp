#include <chronon3d/backends/text/bidi_segmenter.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d {

// ── segment_bidi_runs ─────────────────────────────────────────────────────
//
// Bi-directional text segmentation.
//
// FriBidi is not currently available via CMake config (vcpkg's fribidi port
// does not provide CMake config files).  When fribidi was available, the
// code path performed proper Unicode bidi analysis with UTF-32 conversion,
// embedding levels, and logical-run grouping.
//
// For now, the fallback treats the entire text as a single LTR run.  This
// is sufficient for the vast majority of content (CJK, Latin, Greek,
// Cyrillic are all LTR) and for simple English/serif text titles like
// "LIL DIRK".  Full bidi support can be restored when fribidi is properly
// configured with find_package(PkgConfig) + pkg_check_modules.
//
// See git history for the fribidi implementation, preserved at:
//   src/backends/text/bidi_segmenter.cpp (prior to 2025-06-15)

std::vector<BidiRun> segment_bidi_runs(std::string_view text, int /*base_dir*/) {
    std::vector<BidiRun> runs;
    if (text.empty()) return runs;

    // Fallback: single LTR run for the entire text.
    runs.push_back({std::string(text), TextDirection::LTR, 0});
    return runs;
}

} // namespace chronon3d
