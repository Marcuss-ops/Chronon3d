// ═══════════════════════════════════════════════════════════════════════════
//  cinematic_showcase_config.hpp — Cinematic showcase TEST configuration.
//
//  Phase-2.2 mechanical split.  Single source of truth for the env-var
//  driven runtime mode selection that the monolithic
//  tests/showcase/test_cinematic_camera_showcase.cpp (was 771 LOC)
//  reads via two env vars:
//
//    CHRONON3D_CINEMATIC_FRAME_COUNT  1..6  (default 6)
//
//    CHRONON3D_CINEMATIC_COMP_COUNT   1..5  (default 5)
//
//    CHRONON3D_CINEMATIC_PERF         "1" | "true" | "on" | "yes"
//                                     explicitly whitelisted by Agent 2
//                                     Step 4/6 review to force A4.6 in
//                                     smoke runs.
//
//  Defaults preserve the historical 6-frame + 5-preset behaviour so
//  callers observe zero regression.
//
//  C++17 inline const header — single ODR definition shared across
//  all 4 split test TUs (single CMake executable).
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <algorithm>
#include <cstdlib>
#include <string>

namespace chronon3d::testing::cinematic_cfg {

// ─── Runtime cinematic config ─────────────────────────────────────────
// One-shot env-var read with safe defaults + clamping.  std::atoi can
// throw std::invalid_argument on garbage input; we trivially catch and
// fall back to default.  Never read the env after this initialisation.
struct CinematicConfig {
    int  frame_count;        // 1..6 (default 6).
    int  comp_count;         // 1..5 (default 5).
    bool skip_contact_sheet; // = (frame_count < 6) — A4.5 not run.
    bool smoke_mode;         // = skip_contact_sheet || (comp_count < 5)
                             //   — A4.6 not run unless PERF opt-in.
};

// Inline definition shared ODR-safely across the 4 split test TUs.
// C++17 inline const — initialised at static init (single-threaded),
// never mutated.  Tests read it from inside TEST_CASE bodies (executed
// during main()), so init-order-with-static-libs is irrelevant.
inline const CinematicConfig g_runtime = []() -> CinematicConfig {
    auto read_int = [](const char* name, int dflt) -> int {
        const char* p = std::getenv(name);
        if (!p || !*p) return dflt;
        try { return std::atoi(p); }
        catch (...) { return dflt; }
    };
    CinematicConfig c;
    c.frame_count        = std::clamp(read_int("CHRONON3D_CINEMATIC_FRAME_COUNT", 6), 1, 6);
    c.comp_count         = std::clamp(read_int("CHRONON3D_CINEMATIC_COMP_COUNT",  5), 1, 5);
    c.skip_contact_sheet = (c.frame_count < 6);
    c.smoke_mode         = c.skip_contact_sheet || (c.comp_count < 5);
    return c;
}();

// Strict accept-list for the perf opt-in env var.  Code-review followup
// (Agent 2 / Step 4/6 round 2): previous `*p != '0'` heuristic silently
// turned perf ON for `CHRONON3D_CINEMATIC_PERF=false` (first byte 'f'
// is != '0'); now strictly whitelisted.
inline bool perf_opt_in_from_env() {
    const char* p = std::getenv("CHRONON3D_CINEMATIC_PERF");
    if (!p || !*p) return false;
    const std::string v(p);
    return v == "1" || v == "true" || v == "on" || v == "yes";
}

} // namespace chronon3d::testing::cinematic_cfg
