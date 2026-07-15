#!/usr/bin/env bash
# Permanent anti-regression gate for the canonical Chronon3D render surface.
# The active source tree must expose one RenderJob builder/executor and no
# deprecated still/video command, planner, argument type, target or macro.

set -euo pipefail

REPO_ROOT="${BOUNDARY_CHECK_ROOT:-$(git rev-parse --show-toplevel 2>/dev/null || pwd)}"
cd "$REPO_ROOT" || {
    echo "INTERNAL_ERROR: cannot cd to $REPO_ROOT" >&2
    exit 2
}

SCAN_PATHS=()
for path in apps include src tests cmake CMakeLists.txt; do
    [ -e "$path" ] && SCAN_PATHS+=("$path")
done

if [ "${#SCAN_PATHS[@]}" -eq 0 ]; then
    echo "INTERNAL_ERROR: no active source paths found" >&2
    exit 2
fi

LEGACY_PATTERNS=(
    'RenderJobPlan'
    'VideoJobPlan'
    'plan_render_job[[:space:]]*\('
    'plan_video_job[[:space:]]*\('
    'make_video_render_job[[:space:]]*\('
    'execute_render_job[[:space:]]*\([^,]+,[^)]*RenderJob'
    'command_still[[:space:]]*\('
    'command_video[[:space:]]*\('
    'command_video_camera[[:space:]]*\('
    'register_video_commands[[:space:]]*\('
    '(^|[^[:alnum:]_])StillArgs([^[:alnum:]_]|$)'
    '(^|[^[:alnum:]_])VideoArgs([^[:alnum:]_]|$)'
    '(^|[^[:alnum:]_])VideoCameraArgs([^[:alnum:]_]|$)'
    'CHRONON3D_BUILD_CLI_VIDEO'
    'CHRONON3D_HAS_CLI_VIDEO([^_A-Z]|$)'
    'chronon3d_cli_video([^_a-zA-Z]|$)'
)

LEGACY_FILES=(
    apps/chronon3d_cli/commands/render/command_still.cpp
    apps/chronon3d_cli/commands/video/register_video_commands.cpp
    apps/chronon3d_cli/commands/video/command_video.cpp
    apps/chronon3d_cli/commands/video/command_video_camera.cpp
    apps/chronon3d_cli/commands/group_video.cpp
    apps/chronon3d_cli/utils/video/video_job_plan.hpp
    apps/chronon3d_cli/utils/video/video_job_plan.cpp
)

VIOLATIONS=()

echo "=== Canonical Render CLI Gate ==="
echo "  scanning active source surface for retired planners, aliases and targets"

for pattern in "${LEGACY_PATTERNS[@]}"; do
    hits=$(grep -RnE \
        --exclude='check_no_legacy_render_cli.sh' \
        --exclude-dir='.git' \
        --exclude-dir='build' \
        -- "$pattern" "${SCAN_PATHS[@]}" 2>/dev/null || true)
    if [ -n "$hits" ]; then
        VIOLATIONS+=("pattern: $pattern")
        echo ""
        echo "  [FAIL] retired pattern: $pattern"
        echo "$hits" | sed 's/^/    /'
    fi
done

for path in "${LEGACY_FILES[@]}"; do
    if [ -e "$path" ]; then
        VIOLATIONS+=("file: $path")
        echo ""
        echo "  [FAIL] retired file still exists: $path"
    fi
done

CANONICAL_HEADER="apps/chronon3d_cli/utils/job/render_job.hpp"
CANONICAL_EXECUTOR="apps/chronon3d_cli/utils/job/render_job_execute.cpp"
CANONICAL_REGISTRATION="apps/chronon3d_cli/command_registry.cpp"

# Flatten files to make canonical signature checks robust to clang-format line breaks.
if ! tr '\n' ' ' < "$CANONICAL_HEADER" | grep -qE 'make_render_job[[:space:]]*\('; then
    VIOLATIONS+=("canonical make_render_job declaration missing")
    echo "  [FAIL] canonical make_render_job declaration missing"
fi
if ! tr '\n' ' ' < "$CANONICAL_HEADER" | grep -qE 'execute_render_job[[:space:]]*\([[:space:]]*const RenderJob&'; then
    VIOLATIONS+=("canonical immutable executor declaration missing")
    echo "  [FAIL] canonical execute_render_job(const RenderJob&) declaration missing"
fi
if ! tr '\n' ' ' < "$CANONICAL_EXECUTOR" | grep -qE 'execute_render_job[[:space:]]*\([[:space:]]*const RenderJob&'; then
    VIOLATIONS+=("canonical immutable executor definition missing")
    echo "  [FAIL] canonical execute_render_job(const RenderJob&) definition missing"
fi
if ! tr '\n' ' ' < "$CANONICAL_REGISTRATION" | grep -qE 'register_render_commands[[:space:]]*\('; then
    VIOLATIONS+=("canonical render command registration missing")
    echo "  [FAIL] canonical render command registration missing"
fi

if [ "${#VIOLATIONS[@]}" -ne 0 ]; then
    echo ""
    echo "GATE_FAIL: legacy render CLI surface detected (${#VIOLATIONS[@]} violation(s))" >&2
    printf '  - %s\n' "${VIOLATIONS[@]}" >&2
    exit 1
fi

echo "  [PASS] no retired RenderJob planner or mutable executor adapter"
echo "  [PASS] no still/video command alias, argument type, target or macro"
echo "  [PASS] canonical make_render_job + execute_render_job(const RenderJob&) present"
echo "GATE_PASS: canonical render CLI is the only active render surface"
exit 0
