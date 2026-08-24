#!/usr/bin/env bash
set -euo pipefail

# Build the same checkout twice from clean CMake trees and compare the produced
# release artifact.  Build-tree metadata is intentionally excluded: it embeds
# the absolute build directory by design and is not a shipped artifact.

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="linux-hardened-release"
target="chronon3d_c"
build_a="${repo_dir}/build/chronon/repro-a"
build_b="${repo_dir}/build/chronon/repro-b"
source_date_epoch=""

usage() {
    echo "usage: $0 [--preset NAME] [--target TARGET] [--build-a DIR] [--build-b DIR] [--source-date-epoch EPOCH] [CMake -D options...]" >&2
}

cache_args=()
while (($#)); do
    case "$1" in
        --preset) preset="$2"; shift 2 ;;
        --target) target="$2"; shift 2 ;;
        --build-a) build_a="$2"; shift 2 ;;
        --build-b) build_b="$2"; shift 2 ;;
        --source-date-epoch) source_date_epoch="$2"; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        -D*) cache_args+=("$1"); shift ;;
        *) echo "unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

if [[ -z "$source_date_epoch" ]]; then
    source_date_epoch="$(git -C "$repo_dir" show -s --format=%ct HEAD)"
fi

if [[ "$build_a" == "$build_b" ]]; then
    echo "reproducible build directories must be distinct" >&2
    exit 2
fi

artifact_tmp="$(mktemp -d "${TMPDIR:-/tmp}/chronon-repro-artifacts.XXXXXX")"
trap 'rm -rf "$artifact_tmp"' EXIT

configure_and_build() {
    local build_dir="$1"
    echo "[repro] configure: $build_dir"
    env SOURCE_DATE_EPOCH="$source_date_epoch" \
        cmake --preset "$preset" -B "$build_dir" --fresh "${cache_args[@]}"
    echo "[repro] build: $target in $build_dir"
    env SOURCE_DATE_EPOCH="$source_date_epoch" \
        cmake --build "$build_dir" --target "$target" -j12
}

stage_artifact() {
    local build_dir="$1"
    local destination="$2"
    mkdir -p "$destination"

    mapfile -t artifacts < <(
        find "$build_dir" -type f \( \
            -name "lib${target}.so" -o -name "lib${target}.so.*" -o \
            -name "$target" \
        \) -print | sort
    )
    if ((${#artifacts[@]} == 0)); then
        echo "no output artifact found for target $target in $build_dir" >&2
        return 1
    fi
    for artifact in "${artifacts[@]}"; do
        cp -a "$artifact" "$destination/$(basename "$artifact")"
    done
}

configure_and_build "$build_a"
configure_and_build "$build_b"
stage_artifact "$build_a" "$artifact_tmp/a"
stage_artifact "$build_b" "$artifact_tmp/b"

python3 "$repo_dir/tools/check_reproducible_artifacts.py" \
    "$artifact_tmp/a" "$artifact_tmp/b" \
    --pattern "lib${target}.so" --pattern "lib${target}.so.*" --pattern "$target"
echo "REPRODUCIBLE_BUILD_GATE_PASS: commit $(git -C "$repo_dir" rev-parse --short HEAD)"
