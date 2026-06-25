#!/usr/bin/env bash
# tools/check_gitignored_dirs.sh
# ─────────────────────────────────────────────────────────────────────
# WP-0 (PR 0.2 deferred close-out) — Gitignored build/output dirs guard.
#
# Verifies that the canonical build / output / dependency-cached
# directories declared in `.gitignore` do NOT contain any tracked file
# in the git index. A non-empty result means someone has run
# `git add build/...` (or equivalent) and bypassed `.gitignore` — a
# common cause of repo bloat and of CI rebuild noise (a tracked
# CMakeCache.txt triggers an unnecessary diff in the build-tree).
#
# This is the standalone `.gitignore`-driven audit that the deferred
# item at the bottom of `docs/refactor-roadmap/00-baseline-and-gates.md`
# PR 0.2 calls for.
#
# Source-of-truth: `.gitignore` (commit `d3d71e10`, 2026-06-21).
# The lists below mirror the `.gitignore` entries for directories.
# Update both together when `.gitignore` is amended (see
# IGNORED_DIRS_HEADER_DATE).
#
# Per-directory granularity: each directory is reported individually
# with its own PASS/FAIL line — easy to identify which category
# regressed when the gate goes red on CI.
#
# Wired into:
#   - CI:      `.github/workflows/gates.yml` Gate 5
#              (`architecture-check`), third step after
#              `tools/test_architectural.sh` and
#              `tools/check_architecture_boundaries.sh`.
#   - CMake:   N/A (called by the gate job, not by CTest).
#   - CTest:   N/A (gate-pipeline-only).
#
# Exit codes:
#   0 = all gitignored dirs are untracked
#   1 = at least one gitignored dir has tracked entries (per-dir list dumped)
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

# IGNORED_DIRS_HEADER_DATE — bump this whenever IGNORED_DIRS or
# IGNORED_FILE_PATTERNS below is amended so a `git blame` consumer
# can spot list-vs-gitignore drift.
IGNORED_DIRS_HEADER_DATE='2026-06-21'

# Top-level trailing-/ patterns mirrored from .gitignore. Build
# directories are listed first (most likely to catch violations), then
# package / dependency caches, then engine-output dirs, then IDE / AI
# tool dirs, then generic CMake artifacts.
IGNORED_DIRS=(
    # Build outputs
    build
    .worktrees
    .build-logs
    .build-tmp
    out
    bin
    lib
    obj
    CMakeFiles
    Testing

    # Dependency caches
    vcpkg
    vcpkg_bootstrap
    vcpkg_installed

    # Engine outputs
    output
    output/new_features
    renders
    test_renders
    temp_assets
    artifacts/visual/camera
    logs

    # IDE / AI / Python tool dir
    .vs
    .vscode
    .idea
    .claude
    .antigravitycli
    __pycache__
    .venv
    venv

    # Other / generic
    .history
    _tmp
    tmp
)

# Root-level file patterns complementary to the dirs above. Mirrors
# `.gitignore` entries that target individual files (not dirs).
IGNORED_FILE_PATTERNS=(
    build_output.txt
    CMakeCache.txt
    /vcpkg_bootstrap
    /test_output_*.png
    /path.json
    camera_path*.json
    camera_path_report.json
    CameraTestReport.json
    camera_shot_report*.json
    *_state.bin
    *_report*.json
)

# `/build*/` glob: git ls-files with a literal `build/` does NOT
# match sibling build-tmp/, build-debug/, etc. We handle this
# defensively by listing the dir itself AND a glob-expanded pass.
GLOB_BUILD_DIRS=(build-tmp build-debug build-release build-asan build-*)

# ── Header ───────────────────────────────────────────────────────────
echo "=== Gitignored build/output dirs not tracked (PR 0.2 close-out) ==="
echo "    IGNORED_DIRS_HEADER_DATE = ${IGNORED_DIRS_HEADER_DATE}"
echo "    If `.gitignore` was amended after this date, mirror the change"
echo "    in IGNORED_DIRS / IGNORED_FILE_PATTERNS and bump the date."
echo ""

# ── Per-directory audit ─────────────────────────────────────────────
overall_fail=0
per_dir_lines=()
total_violations=0

for d in "${IGNORED_DIRS[@]}"; do
    # git ls-files <dir>/ exit codes:
    #   0 — at least one tracked entry listed, OR
    #   ≥1 — dir does not exist / no tracked entries (no stdout).
    # `2>/dev/null || true` absorbs the dir-missing case as vacuous pass.
    tracked=$(git ls-files "${d}/" 2>/dev/null || true)
    if [ -n "$tracked" ]; then
        per_dir_lines+=("  [FAIL] ${d}/ has tracked entries (count: $(printf '%s\n' "${tracked}" | wc -l | tr -d ' ')):")
        per_dir_lines+=("$(printf '%s\n' "${tracked}" | sed 's/^/           /')")
        overall_fail=1
        total_violations=$((total_violations + $(printf '%s\n' "${tracked}" | wc -l | tr -d ' ')))
    else
        per_dir_lines+=("  [PASS] ${d}/ — no tracked entries")
    fi
done

# ── glob-variant coverage: /build*/ ─────────────────────────────────
# `git ls-files build*/` expands the glob against real directories on
# the filesystem; this catches sibling build-tmp/, build-linux-fast-dev/,
# etc. without committing to a closed enumeration.
glob_hits=0
for d in "${GLOB_BUILD_DIRS[@]}"; do
    # Skip the literal "build" to avoid double-listing.
    [ "${d}" = "build" ] && continue
    for real in ${d}; do
        if [ "${real}" != "${d}" ] || [ -d "${real}" ]; then
            tracked=$(git ls-files "${real}/" 2>/dev/null || true)
            if [ -n "$tracked" ]; then
                per_dir_lines+=("  [FAIL] (glob) ${real}/ has tracked entries (count: $(printf '%s\n' "${tracked}" | wc -l | tr -d ' ')):")
                per_dir_lines+=("$(printf '%s\n' "${tracked}" | sed 's/^/           /')")
                overall_fail=1
                glob_hits=$((glob_hits + $(printf '%s\n' "${tracked}" | wc -l | tr -d ' ')))
            fi
        fi
    done
done

# ── File-pattern audit (root-level patterns only) ────────────────────
file_violations=0
for f in "${IGNORED_FILE_PATTERNS[@]}"; do
    # `f` may be a literal path or a glob; use compgen to expand globs
    # at the root level (no recursion). Quiet failure on missing files.
    matches=$(compgen -G "${f}" 2>/dev/null || true)
    for m in ${matches}; do
        if [ -f "${m}" ] && git ls-files --error-unmatch "${m}" >/dev/null 2>&1; then
            per_dir_lines+=("  [FAIL] (file-pattern match) ${m} is tracked but should be ignored")
            overall_fail=1
            file_violations=$((file_violations + 1))
        fi
    done
done

# ── Summary ──────────────────────────────────────────────────────────
if [ "${per_dir_lines[@]}" ]; then
    printf '%s\n' "${per_dir_lines[@]}"
fi
echo ""

if [ "${overall_fail}" -ne 0 ]; then
    echo "=== Gitignored-dirs check FAILED (${total_violations} dir + ${glob_hits} glob + ${file_violations} file violations) ==="
    echo ""
    echo "Fix hint: for each tracked entry remove from the index with"
    echo "    git rm --cached <path>"
    echo "or amend .gitignore if the entry should actually be tracked."
    exit 1
fi

echo "=== Gitignored-dirs check PASSED (${#IGNORED_DIRS[@]} listed dirs + ${#GLOB_BUILD_DIRS[@]} globs + ${#IGNORED_FILE_PATTERNS[@]} file patterns, all empty) ==="
exit 0
