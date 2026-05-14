#!/usr/bin/env bash
set -euo pipefail

COMP_ID=${1:-}
if [[ -z "$COMP_ID" ]]; then
    echo "Usage: $0 <composition_id>"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

PRESET="${CHRONON_PRESET:-linux-release}"
BUILD_DIR="build/chronon/$PRESET"
BIN="$BUILD_DIR/chronon3d_cli"

build_once() {
    cmake --preset "$PRESET"
    cmake --build "$BUILD_DIR" --target chronon3d_cli -j"${CHRONON_JOBS:-$(nproc)}"
}

render_once() {
    "$BIN" render "$COMP_ID" --frames 0 -o "output/watch_####.png" --diagnostic
}

latest_stamp() {
    local latest=0
    local paths=(
        "include/chronon3d"
        "apps/chronon3d_cli"
        "examples"
        "src"
        "tests"
        "CMakeLists.txt"
        "CMakePresets.json"
        "vcpkg.json"
        "tools"
    )

    for path in "${paths[@]}"; do
        if [[ -d "$path" ]]; then
            while IFS= read -r -d '' file; do
                local stamp
                stamp="$(stat -c '%Y' "$file")"
                if (( stamp > latest )); then
                    latest="$stamp"
                fi
            done < <(find "$path" -type f -print0)
        elif [[ -f "$path" ]]; then
            local stamp
            stamp="$(stat -c '%Y' "$path")"
            if (( stamp > latest )); then
                latest="$stamp"
            fi
        fi
    done

    echo "$latest"
}

echo "Watching include/chronon3d, apps/chronon3d_cli, examples, src, tests, and build files."
echo "Composition: $COMP_ID"

build_once
render_once

last_stamp="$(latest_stamp)"
while true; do
    sleep 1
    current_stamp="$(latest_stamp)"
    if [[ "$current_stamp" != "$last_stamp" ]]; then
        last_stamp="$current_stamp"
        echo "Change detected, rebuilding Chronon3d..."
        build_once
        render_once
    fi
done
