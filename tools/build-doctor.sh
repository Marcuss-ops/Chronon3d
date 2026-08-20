#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-${BUILD_DIR_OVERRIDE:-build/chronon/linux-video-fast-dev}}"
jobs="${JOBS:-8}"
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

[[ -d "$root_dir/$build_dir" && "$build_dir" != /* ]] && build_dir="$root_dir/$build_dir"

echo "Chronon3d Build Doctor"
echo "====================="
printf 'Build dir:      %s\n' "$build_dir"
printf 'Generator:       %s\n' "$(grep -m1 '^CMAKE_GENERATOR:' "$build_dir/CMakeCache.txt" 2>/dev/null | cut -d= -f2- || echo unknown)"
printf 'Build type:      %s\n' "$(grep -m1 '^CMAKE_BUILD_TYPE:' "$build_dir/CMakeCache.txt" 2>/dev/null | cut -d= -f2- || echo unknown)"
printf 'Unity:           %s\n' "$(grep -m1 '^CHRONON3D_UNITY_BUILD:' "$build_dir/CMakeCache.txt" 2>/dev/null | cut -d= -f2- || echo unknown)"
printf 'Ccache:          %s\n' "$(command -v ccache >/dev/null 2>&1 && echo YES || echo NO)"
printf 'Ccache launcher:  %s\n' "$(grep -m1 '^CMAKE_CXX_COMPILER_LAUNCHER:' "$build_dir/CMakeCache.txt" 2>/dev/null | cut -d= -f2- || echo unset)"
printf 'Linker flags:     %s\n' "$(grep -m1 '^CMAKE_EXE_LINKER_FLAGS:' "$build_dir/CMakeCache.txt" 2>/dev/null | cut -d= -f2- || echo unset)"
printf 'Compiler:         %s\n' "$(grep -m1 '^CMAKE_CXX_COMPILER:' "$build_dir/CMakeCache.txt" 2>/dev/null | cut -d= -f2- || echo unknown)"
printf 'Jobs:             %s\n' "$jobs"
printf 'Free disk:        %s\n' "$(df -h "$root_dir" | awk 'NR==2 {print $4}')"

# A second Ninja process against the same directory invalidates timing
# measurements and can make archives appear truncated. Detect both absolute
# and repository-relative spellings before reporting a build as healthy.
relative_build_dir="${build_dir#"$root_dir/"}"
active_ninja="$(ps -eo pid=,args= | awk -v absolute="$build_dir" -v relative="$relative_build_dir" \
    '$0 ~ /(^|[[:space:]])ninja([[:space:]]|$)/ &&
     (index($0, absolute) || index($0, relative)) { print }')"
if [[ -n "$active_ninja" ]]; then
    echo "Concurrent Ninja:  YES (same build directory)"
    echo "$active_ninja" | sed 's/^/  /'
    echo "GATE_FAIL: stop the other build before measuring or building this directory" >&2
    exit 2
fi
echo "Concurrent Ninja:  NO"

if [[ -d "$build_dir" ]]; then
    bash "$root_dir/tools/check_environment_specific_paths.sh" "$build_dir"
fi

if command -v ccache >/dev/null 2>&1; then
    ccache -s | sed -n '1,8p'
fi

if [[ -f "$build_dir/build.ninja" ]]; then
    start_ns="$(date +%s%N)"
    ninja -C "$build_dir" -n chronon3d_cli >/tmp/chronon-build-doctor-ninja.out
    end_ns="$(date +%s%N)"
    dry_ms=$(( (end_ns - start_ns) / 1000000 ))
    pending="$(grep -cE '^\[[0-9]+/' /tmp/chronon-build-doctor-ninja.out || true)"
    printf 'Dry-run:          %s ms, %s pending commands\n' "$dry_ms" "$pending"
fi

if [[ -f "$build_dir/.ninja_log" ]]; then
    echo "Last recorded Ninja timings (cumulative command time):"
    awk -F '\t' '
        NR == 1 { next }
        {
            ms = $2 - $1
            path = $4
            if (path ~ /\.o$|\.gch$/) compile += ms
            else if (path ~ /\.a$/) archive += ms
            else if (path ~ /(^|\/)chronon3d_cli$|\.so(\.|$)/) link += ms
        }
        END {
            printf "  compile: %d ms\n  archive: %d ms\n  link:    %d ms\n", compile + 0, archive + 0, link + 0
        }
    ' "$build_dir/.ninja_log"
fi
