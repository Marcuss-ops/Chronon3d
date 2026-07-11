#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// asset_preload_check.hpp — Forward-point 0b helper for the
// TICKET-ACCEPTANCE-FORENSIC-SURFACE workstream (Promote forward-points
// 0a/0b/0c of TICKET-ACCEPTANCE-SUITE-PHASE-D).
//
// Usage:
//   asset_preload_check_for_test(std::filesystem::path{"assets"}, stderr);
//   // prints:
//   //   [chronon3d-forensic] assets_root='./assets' existence=true is_directory=true file_count=N
//
// Output format (single line per call):
//   [chronon3d-forensic] assets_root='<path>' existence=<bool> is_directory=<bool> file_count=<N|−1>
//
// The diagnostic is intentionally permissive (no FAIL): a missing or
// empty assets_root is reported verbatim so the acceptance forensic
// surface captures the run-time state without aborting the test.
//
// Lives in tests/helpers/ (NOT include/chronon3d/) per AGENTS.md §Cat-3
// (no new public SDK API surface).
// ──────────────────────────────────────────────────────────────────────────────

#include <cstdio>
#include <filesystem>
#include <string_view>

namespace chronon3d::test_forensic {

/// Write a one-line diagnostic describing the assets_root state to `out`.
///
/// Behaviour:
///  - Prints whether `assets_root` exists and is a directory.
///  - When it IS a directory, also prints the file_count (integer; −1
///    when missing).
///  - null FILE* is a contract-respecting no-op (returns immediately).
inline void asset_preload_check_for_test(const std::filesystem::path& assets_root,
                                         std::FILE* out) noexcept {
    if (out == nullptr) {
        return;
    }
    using std::filesystem::exists;
    using std::filesystem::is_directory;
    const bool does_exist = exists(assets_root);
    const bool is_dir = does_exist && is_directory(assets_root);
    long file_count = -1;
    if (is_dir) {
        file_count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(assets_root)) {
            (void)entry;  // count each entry (files + sub-dirs as one count)
            ++file_count;
        }
    }
    std::fprintf(out,
                 "[chronon3d-forensic] assets_root='%s' existence=%s "
                 "is_directory=%s file_count=%ld\n",
                 assets_root.string().c_str(),
                 does_exist ? "true" : "false",
                 is_dir ? "true" : "false",
                 file_count);
}

} // namespace chronon3d::test_forensic
