// ============================================================================
// tools/test_software_renderer_boundary_consistency.cpp
// ============================================================================
//
// Cat-2 regression test: LOCKS the path-list-parity invariant between each
// gate-3 TICKET row's acceptance-criteria recipe (in
// `docs/FOLLOWUP_TICKETS.md`) and the actual scan paths in
// `tools/check_software_renderer_boundary.sh`'s I5 walker.
//
// WHY THIS EXISTS
// ---------------
// On `main@872993ea` the TICKET-079 row's acceptance-criterion recipe cited
// 6 paths; the gate-3 I5 walker scans 5 paths -- the row was drifted from
// the script, breaking the "TICKET row's recipe reproduces gate-3 PASS"
// invariant.  Any maintainer editing one but not the other would silently
// ship a broken doc + gate combo.  This test FAILS on drift and PASSES
// once both sides agree.
//
// TEST APPROACH
// -------------
// 1. Slurp both files relative to CMAKE_SOURCE_DIR (WORKING_DIRECTORY of
//    `chronon3d_core_tests`).
// 2. EXTRACT_GATE3_I5_WALK_PATHS(s): scan lines in the `# -- I5:` block
//    of the gate script after the `grep -RIn 'SoftwareRenderer&'`
//    invocation.  Collect every path-token argument (one per line because
//    the script writes each path on its own line and uses backslash for
//    continuation).  Strip the trailing `\` continuation marker, trim
//    indent, dedup into `std::set<std::string>`.
// 3. FIND_GATE3_ROWS(tickets): iterate markdown table rows starting with
//    `| TICKET-` whose Area column contains `gate-3`.  Extract `(id,
//    body)` per row.
// 4. EXTRACT_PATHS_FROM_RECIPE(body): scan the row body for backtick-
//    quoted blocks whose first word is `grep`; tokenise and collect every
//    argument token that looks like a path (starts with `src/`,
//    `include/`, `apps/`, `tests/`, `cmake/`, `content/`, AND contains
//    `/`).  Skip flags, the `grep` keyword, and `SoftwareRenderer&`-
//    family tokens (lvalue/rvalue ref).
// 5. For each row with a non-empty recipe path set, assert
//    `gate3_i5_paths == row_paths` (set equality, order-insensitive).
// 6. Report any drift via `INFO` diff (paths in gate-3 but not in row;
//    paths in row but not in gate-3) so future maintainers see the exact
//    diff on first failure.
//
// ROW-SCOPE
// ---------
// Gate-3 rows whose acceptance criterion is metric-shaped (e.g. "header
// LOC ≤ 200" for TICKET-077 or "non-local include count ≤ 6" for
// TICKET-078) have NO backtick-quoted grep recipe; the parser emits an
// empty path set for them and they are SKIPPED in the parity loop.  Only
// rows with path-list recipes -- presently TICKET-079 (gate-3 / I5 walker)
// -- drive the parity assertion.  The test is forward-compatible: any
// future TICKET that introduces a path-list recipe in its acceptance
// clause will be auto-checked by this same loop.
//
// FREEZE COMPLIANCE (AGENTS.md v0.1)
// ----------------------------------
// * Cat-2 ("test deterministici" per AGENTS.md v0.1) -- allowed.
// * Read-only filesystem access: slurps 2 files; no writes.
// * No new dependencies: std::filesystem, std::ifstream, std::set,
//   std::string.  No Boost, ICU, MSDF, or libtess2 (Gate 5 deny-list).
// * No `include/chronon3d/` surface expansion -- this file does NOT
//   include any chronon3d public header.
//
// REGISTRATION
// ------------
// Compiled into `chronon3d_core_tests` via `target_sources(...)` from
// `tests/core_tests.cmake`.  The source file lives at `tools/` per
// the user's explicit Cat-2 path request, colocated with the gate
// script it asserts against.  This is the lightest CMake touch: no
// new add_executable, no new per-area `.cmake` file.
// ============================================================================

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace chronon3d::test::boundary_consistency {

namespace {

// ─── Slurp helper ─────────────────────────────────────────────────────
std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p);
    if (!in) {
        throw std::runtime_error(
            std::string{"test_software_renderer_boundary_consistency: cannot open "} +
            p.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> split_lines(const std::string& txt) {
    std::vector<std::string> out;
    std::string line;
    std::istringstream iss(txt);
    while (std::getline(iss, line)) out.push_back(line);
    return out;
}

std::string trim_copy(std::string s) {
    auto lead = s.find_first_not_of(" \t\r");
    if (lead == std::string::npos) return {};
    s.erase(0, lead);
    auto trail = s.find_last_not_of(" \t\r");
    if (trail != std::string::npos) s.resize(trail + 1);
    return s;
}

// ─── Path-token heuristic (shared by walker + recipe extractors) ─────
// Canonical Chronon3D path roots; gate-3, AGENTS.md and this test all
// start under one of these.
bool looks_like_walk_path(const std::string& tok) {
    if (tok.find('/') == std::string::npos) return false;
    static constexpr const char* prefixes[] = {
        "src/", "include/", "apps/", "tests/", "cmake/", "content/"
    };
    for (auto* p : prefixes)
        if (tok.rfind(p, 0) == 0) return true;
    return false;
}

// ─── Gate-3 I5 walker path extractor ───────────────────────────────────
// Scans lines in the `# -- I5:` block of
// `tools/check_software_renderer_boundary.sh`, collects every path
// argument to the `grep -RIn 'SoftwareRenderer&'` invocation.  In the
// canonical script each path sits on its own line with a trailing `\`
// (shell line continuation); we strip the `\`, trim indent, dedup.
std::set<std::string> extract_gate3_i5_walk_paths(const std::string& script) {
    std::set<std::string> paths;
    auto lines = split_lines(script);
    bool in_i5 = false;
    bool invocation_started = false;

    for (auto& line : lines) {
        // Enter the I5 block at the `# -- I5:` marker.
        if (!in_i5 && line.find("# -- I5:") != std::string::npos) {
            in_i5 = true;
        }
        // Exit when we hit a different `# --` marker that isn't I5.
        if (in_i5 &&
            line.find("# -- ") != std::string::npos &&
            line.find("# -- I5:") == std::string::npos) {
            break;
        }
        if (!in_i5) continue;

        // Wait until we hit the `grep -RIn 'SoftwareRenderer&'` invocation.
        if (!invocation_started) {
            if (line.find("grep -RIn 'SoftwareRenderer&'") != std::string::npos) {
                invocation_started = true;
                // Path args on the same line as the invocation (rare
                // but handle for robustness): trim and check.
                auto same = trim_copy(line);
                // Strip everything up to and including the `'` close
                // of the lookup pattern on this very line.
                auto close_pos = same.find('\'', same.find('\'') + 1);
                if (close_pos != std::string::npos) {
                    auto tail = trim_copy(same.substr(close_pos + 1));
                    if (!tail.empty() && tail.back() == '\\') tail.pop_back();
                    tail = trim_copy(tail);
                    if (!tail.empty() && tail.find('/') != std::string::npos) {
                        paths.insert(tail);
                    }
                }
            }
            continue;
        }

        auto l = trim_copy(line);
        if (l.empty()) continue;

        // The path-list invocation ends at `2>/dev/null`, `| grep -v '`,
        // or `wc -l`.
        if (l.find("2>/dev/null") != std::string::npos) { break; }
        if (l.find("| grep -v") != std::string::npos)   { break; }
        if (l.find("| wc -l") != std::string::npos)     { break; }

        // Strip the trailing `\` line-continuation marker.
        if (l.size() >= 1 && l.back() == '\\') {
            l.pop_back();
            l = trim_copy(l);
        }
        if (l.empty()) continue;

        // Each path arg in the canonical script is itself the line,
        // after trim + continuation-strip looks like a path (starts
        // with `src/`, `include/`, etc.)
        if (looks_like_walk_path(l)) {
            paths.insert(l);
        }
    }
    return paths;
}

// ─── TICKET row parser ─────────────────────────────────────────────────
struct TicketRow {
    std::string id;
    std::string body;
};

// Matches rows of the form `| TICKET-NNN | <area containing gate-3> | ...`.
// The Area column typically reads "gate-3 / I2 — ..." or similar; we
// gate the test to rows whose Area column substring contains "gate-3".
std::vector<TicketRow> find_gate3_rows(const std::string& tickets) {
    std::vector<TicketRow> rows;
    auto lines = split_lines(tickets);
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        // Data row begins with `| TICKET-` (not a separator row, not the
        // header row).
        if (line.rfind("| TICKET-", 0) != 0) continue;
        if (line.find("gate-3") == std::string::npos) continue;

        // Extract TICKET-NNN token.
        std::string id;
        auto sp = line.find(' ', 2);
        if (sp != std::string::npos) id = line.substr(2, sp - 2);

        // Body: append rows until a blank or next `| TICKET-` row.
        std::string body = line + "\n";  // include the row line: recipe can live IN-CELL (Acceptance column)
        for (size_t j = i + 1; j < lines.size(); ++j) {
            if (lines[j].empty()) break;
            if (lines[j].rfind("| TICKET-", 0) == 0) break;
            body += lines[j];
            body += '\n';
        }
        if (!id.empty()) rows.push_back({id, body});
    }
    return rows;
}

// ─── Path extractor from a backtick-quoted grep recipe ───────────────
// Scans the row body for backtick-quoted blocks whose first word is
// `grep`; tokenises; collects every argument that looks like a path.
std::set<std::string> extract_paths_from_recipe(const std::string& body) {
    std::set<std::string> paths;
    size_t i = 0;
    while (i < body.size()) {
        if (body[i] != '`') { ++i; continue; }
        auto j = body.find('`', i + 1);
        if (j == std::string::npos) break;
        std::string block = body.substr(i + 1, j - i - 1);
        auto trimmed = trim_copy(block);
        // Recipe must START with `grep` (any flag set is allowed:
        // `grep -r`, `grep -rnE`, `grep -RIn`, etc.).
        if (trimmed.rfind("grep", 0) != 0) {
            i = j + 1;
            continue;
        }
        std::istringstream iss(trimmed);
        std::string tok;
        while (iss >> tok) {
            if (tok.empty()) continue;
            if (tok[0] == '-') continue;        // skip flags
            if (tok == "grep") continue;       // skip keyword
            // Strip surrounding single-quotes.
            if (tok.size() >= 2 && tok.front() == '\'' && tok.back() == '\'') {
                tok = tok.substr(1, tok.size() - 2);
            }
            if (tok.empty()) continue;
            // Skip the `SoftwareRenderer&` / `SoftwareRenderer&&` ref
            // tokens (they aren't walk paths).
            if (tok.rfind("SoftwareRenderer", 0) == 0) continue;
            // Path-token heuristic.
            if (looks_like_walk_path(tok)) {
                paths.insert(tok);
            }
        }
        i = j + 1;
    }
    return paths;
}

// ─── Diff driver (symmetric, returns elements only in `a`) ─────────────
template <typename Set>
std::vector<std::string> diff_a_minus_b(const Set& a, const Set& b) {
    std::vector<std::string> out;
    for (auto& x : a) if (b.find(x) == b.end()) out.push_back(x);
    return out;
}

} // namespace

TEST_CASE("gate-3 / TICKET-row path-list parity (Cat-2 regression)") {
    namespace fs = std::filesystem;
    const fs::path root{"."};  // WORKING_DIRECTORY = CMAKE_SOURCE_DIR.

    std::string script, tickets;
    try {
        script  = slurp(root / "tools/check_software_renderer_boundary.sh");
        tickets = slurp(root / "docs/FOLLOWUP_TICKETS.md");
    } catch (const std::exception& e) {
        FAIL("slurp failure: " << e.what());
    }

    // ── Step 1: extract canonical gate-3 I5 walk path list ───────────
    const auto gate3_i5 = extract_gate3_i5_walk_paths(script);

    INFO("gate-3 I5 walker paths (from tools/check_software_renderer_boundary.sh):");
    for (const auto& p : gate3_i5) INFO("  " << p);
    // Locked at 5 (not `>= 4`): an under-bounded counter silently
    // masks row/walker drift (TICKET-079 on `main@872993ea` cited 6
    // paths while the walker scanned 5; this test closes that gap).
    REQUIRE(gate3_i5.size() == 5);

    // ── Step 2: enumerate gate-3 TICKET rows ────────────────────────
    auto rows = find_gate3_rows(tickets);
    INFO("gate-3 TICKET rows parsed (id, body-length):");
    for (const auto& r : rows) INFO("  " << r.id << " (body=" << r.body.size() << " bytes)");
    REQUIRE_FALSE(rows.empty());

    // ── Step 3: per-row parity assertion ─────────────────────────────
    bool tested_at_least_one_row_with_recipe = false;
    for (const auto& row : rows) {
        CAPTURE(row.id);
        const auto row_paths = extract_paths_from_recipe(row.body);

        if (row_paths.empty()) {
            // I2 (LOC), I3 (includes) gate-3 rows have metric-shaped
            // acceptance criteria, no grep recipe.  Skip silently.
            INFO(row.id << " has no backtick-quoted grep recipe; "
                           "non-path-list acceptance criterion -- skipping parity.");
            continue;
        }

        INFO(row.id << " recipe paths (from docs/FOLLOWUP_TICKETS.md):");
        for (const auto& p : row_paths) INFO("  " << p);

        const auto only_in_gate = diff_a_minus_b(gate3_i5, row_paths);
        const auto only_in_row  = diff_a_minus_b(row_paths, gate3_i5);

        if (!only_in_gate.empty()) {
            INFO("paths in gate-3 I5 walker but NOT in " << row.id << " row recipe:");
            for (const auto& p : only_in_gate) INFO("  >> " << p);
        }
        if (!only_in_row.empty()) {
            INFO("paths in " << row.id << " row recipe but NOT in gate-3 I5 walker:");
            for (const auto& p : only_in_row) INFO("  >> " << p);
        }

        CHECK(only_in_gate.empty());
        CHECK(only_in_row.empty());

        tested_at_least_one_row_with_recipe = true;
    }

    // Sanity gate: at least one gate-3 row in the table HAD a recipe,
    // else the test is vacuous.  At time of writing TICKET-079 has
    // such a recipe; if the doc drops it the test fails to lock any
    // invariant.
    CHECK(tested_at_least_one_row_with_recipe);
}

} // namespace chronon3d::test::boundary_consistency
