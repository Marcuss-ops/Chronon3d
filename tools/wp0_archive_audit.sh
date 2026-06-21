#!/usr/bin/env bash
# tools/wp0_archive_audit.sh — WP-0 PR 0.3 archive audit
# ─────────────────────────────────────────────────────────────────────
# Source-level analysis that does NOT require a build.  Outputs a
# markdown-friendly report to stdout (and a summary to stderr).
#
# This script captures the data the WP-0 PR 0.3 completion record
# asks for.  Build-derived numbers (Debug archive size, Release
# archive size, no-content archive size, debug-symbol share of the
# archive) require a configured CMake build and are intentionally
# LEFT BLANK with `TBD: pending CI build` markers — the WP-0 PR 0.4
# completion record documents where these need to be filled in once
# the vcpkg-installed CI build matrix produces consistent numbers.
#
# Source-level metrics that ARE captured today:
#   - .cpp / .hpp / .h file counts under src/ + include/
#   - CMake OBJECT library count (per-subsystem)
#   - Static archive library count (per-subsystem)
#   - INTERFACE library count
#   - Cross-subsystem OBJECT duplicate registration check
# ─────────────────────────────────────────────────────────────────────
set -uo pipefail

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

report() { printf '%s\n' "$*"; }
heading() { printf '\n=== %s ===\n' "$*"; }

heading "WP-0 PR 0.3 — source-level archive audit"

# ── Source file counts (per language) ────────────────────────────────
heading "Source file counts (src/)"
report "- src/*.cpp count: $(find "$REPO_ROOT/src" -name '*.cpp' | wc -l)"
report "- include/*.hpp count: $(find "$REPO_ROOT/include" -name '*.hpp' | wc -l)"
report "- include/*.h   count: $(find "$REPO_ROOT/include" -name '*.h'   | wc -l)"

# ── Total source size (rough proxy for compilation work) ─────────────
heading "Source size (KB, uncompressed)"
src_kb=$(find "$REPO_ROOT/src" -type f \
    \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -exec wc -c {} + 2>/dev/null \
    | tail -1 | awk '{print int($1/1024)}')
hdr_kb=$(find "$REPO_ROOT/include" -type f \
    \( -name '*.hpp' -o -name '*.h' \) -exec wc -c {} + 2>/dev/null \
    | tail -1 | awk '{print int($1/1024)}')
report "- src/ size: ${src_kb} KB"
report "- include/ size: ${hdr_kb} KB"

# ── CMake OBJECT / STATIC / INTERFACE library audit ──────────────────
heading "CMake library audit (per-subsystem OBJECT/STATIC)"
if command -v grep >/dev/null 2>&1; then
    obj_libs=$(grep -RhE '^add_library\(' "$REPO_ROOT/src" --include='CMakeLists.txt' 2>/dev/null \
        | grep -E 'OBJECT' | wc -l || true)
    static_libs=$(grep -RhE '^add_library\(' "$REPO_ROOT/src" --include='CMakeLists.txt' 2>/dev/null \
        | grep -E 'STATIC' | wc -l || true)
    iface_libs=$(grep -RhE '^add_library\(' "$REPO_ROOT/src" --include='CMakeLists.txt' 2>/dev/null \
        | grep -E 'INTERFACE' | wc -l || true)
    report "- OBJECT libraries: $obj_libs"
    report "- STATIC libraries: $static_libs"
    report "- INTERFACE libraries: $iface_libs"

    # ── Duplicate OBJECT registration cross-check ─────────────────────
    # Per the WP-0 PR 0.3 exit criterion: "Confirm no OBJECT library
    # is aggregated more than once."  We scan the CMakeLists.txt
    # tree for any `chronon3d_*` OBJECT library declaration appearing
    # more than once — a re-declaration would mean double aggregation
    # at link time.
    heading "OBJECT library duplicate registration check"
    duplicate_hits=$(grep -RhE '^add_library\((chronon3d_[a-zA-Z0-9_]+)' "$REPO_ROOT/src" \
        --include='CMakeLists.txt' 2>/dev/null \
        | grep -E 'OBJECT' \
        | sed -E 's/.*add_library\((chronon3d_[a-zA-Z0-9_]+).*/\1/' \
        | sort | uniq -c | awk '$1 > 1 { print }' || true)
    if [ -n "$duplicate_hits" ]; then
        report "- FAIL: duplicate OBJECT registrations detected:"
        echo "$duplicate_hits" | sed 's/^/    /'
    else
        report "- PASS: no OBJECT library is declared more than once across src/"
    fi

    # ── Concrete OBJECT libraries (target name list) ────────────────
    heading "OBJECT libraries (cross-subsystem inventory)"
    grep -RhE '^add_library\((chronon3d_[a-zA-Z0-9_]+)' "$REPO_ROOT/src" \
        --include='CMakeLists.txt' 2>/dev/null \
        | grep -E 'OBJECT' \
        | sed -E 's/.*add_library\((chronon3d_[a-zA-Z0-9_]+).*/\1/' \
        | sort -u | sed 's/^/  - /'
fi

# ── Placeholders for build-required metrics ──────────────────────────
heading "Build-required metrics (TBD: pending CI build)"
report "- Debug archive size (libchronon3d_sdk_impl.a, Debug):    TBD"
report "- Release archive size (libchronon3d_sdk_impl.a, Release): TBD"
report "- No-content archive size (BUILD_CONTENT=OFF):            TBD"
report "- Debug-symbol share of archive:                          TBD"
report "- Unique symbol count (nm -P):                            TBD"

heading "End of WP-0 PR 0.3 audit report"
