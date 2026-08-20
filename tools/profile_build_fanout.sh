#!/usr/bin/env bash
set -euo pipefail

# Report the object files whose Ninja dependency manifests contain one or
# more requested source/header paths. This is intentionally read-only and
# works with both normal and UNITY builds.
if (($# < 2)); then
    echo "usage: $0 <build-dir> <source-or-header> [more paths ...]" >&2
    exit 2
fi

build_dir=$1
shift
[[ -f "$build_dir/build.ninja" ]] || {
    echo "build directory has no build.ninja: $build_dir" >&2
    exit 2
}

declare -a deps
deps=( )
while (($#)); do
    path=$1
    shift
    if [[ "$path" != /* ]]; then
        root=$(git -C "$build_dir" rev-parse --show-toplevel)
        path="$root/$path"
    fi
    deps+=("$path")
done

for needle in "${deps[@]}"; do
    echo "[$needle]"
    mapfile -t matches < <(
        {
            ninja -C "$build_dir" -t deps \
              | awk -v needle="$needle" '
                  /^[^[:space:]].*: #deps/ { target = $0; sub(/: #deps.*/, "", target) }
                  index($0, needle) { print target }
                '
            # Source files are direct Ninja inputs rather than entries in a
            # depfile; inspect compile commands as well so a .cpp reports its
            # own object instead of an artificial zero fan-out.
            ninja -C "$build_dir" -t commands \
              | awk -v needle="$needle" '
                  index($0, needle) {
                      for (i = 1; i < NF; ++i)
                          if ($i == "-o") print $(i + 1)
                  }
                '
        } \
          | sort -u
    )
    printf '  fan-out objects: %d\n' "${#matches[@]}"
    if ((${#matches[@]})); then
        printf '  %s\n' "${matches[@]}"
    fi
done
