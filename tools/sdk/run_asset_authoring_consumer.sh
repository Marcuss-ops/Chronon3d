#!/usr/bin/env bash
# Installed-SDK gate for explicit authoring headers + per-engine asset roots.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=./common.sh
source "$HERE/common.sh"

: "${SDK_PREFIX:?SDK_PREFIX env var required}"
: "${REPO_ROOT:?REPO_ROOT env var required}"

SOURCE="$REPO_ROOT/tests/install_consumer/main_assets.cpp"
[[ -f "$SOURCE" ]] || fail "asset authoring consumer missing: $SOURCE"

# Compile-contract probes: exact explicit includes and authoring syntax.
for include in \
    chronon3d/authoring/asset.hpp \
    chronon3d/authoring/layer.hpp \
    chronon3d/authoring/text.hpp \
    chronon3d/sdk/render_engine.hpp; do
    grep -Fq "#include <$include>" "$SOURCE" \
        || fail "asset consumer missing explicit include <$include>"
done

grep -Fq 'layer.image("logo", asset("images/logo.png"))' "$SOURCE" \
    || fail "asset consumer missing canonical layer.image(..., asset(...)) syntax"
grep -Fq '.font(asset("fonts/Inter-Bold.ttf"), 28.0f)' "$SOURCE" \
    || fail "asset consumer missing canonical text.font(asset(...), size) syntax"

if grep -Eq '#include[[:space:]]*<chronon3d/(chronon3d|authoring|render|advanced)\.hpp>' "$SOURCE"; then
    fail "asset consumer uses a retired umbrella header"
fi

CONS_BUILD="$(mktemp_dir chronon3d_asset_authoring_consumer)"
cleanup_register "$CONS_BUILD"

CONS_PREFIX_PATH="$SDK_PREFIX"
if [[ -n "${VCPKG_INSTALLED_DIR:-}" ]]; then
    triplet="${VCPKG_TARGET_TRIPLET:-x64-linux}"
    CONS_PREFIX_PATH="${CONS_PREFIX_PATH};${VCPKG_INSTALLED_DIR}/${triplet}"
fi
if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    CONS_PREFIX_PATH="${CONS_PREFIX_PATH};${CMAKE_PREFIX_PATH}"
fi

CMAKE_ARGS=(
    -S "$REPO_ROOT/tests/install_consumer"
    -B "$CONS_BUILD"
    "-DCMAKE_PREFIX_PATH=$CONS_PREFIX_PATH"
    -DCMAKE_BUILD_TYPE=Release
    "-DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR:-}"
    "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET:-x64-linux}"
)

VCPKG_TC="$REPO_ROOT/vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake"
if [[ -f "$VCPKG_TC" ]]; then
    CMAKE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=$VCPKG_TC")
elif [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
    CMAKE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
fi

log "configuring installed authoring asset consumer"
cmake "${CMAKE_ARGS[@]}" 1>&2 \
    || fail "asset authoring consumer configure failed"
cmake --build "$CONS_BUILD" --target check_assets 1>&2 \
    || fail "asset authoring consumer build failed"

[[ -d "$REPO_ROOT/assets" ]] \
    || fail "repository assets directory missing"
ln -sfn "$REPO_ROOT/assets" "$CONS_BUILD/assets"

consumer="$CONS_BUILD/check_assets"
[[ -x "$consumer" ]] || fail "check_assets binary missing"

set +e
output="$(cd "$CONS_BUILD" && "$consumer" assets 2>&1)"
rc=$?
set -e
printf '%s\n' "$output"

[[ "$rc" -eq 0 ]] || fail "check_assets exited with rc=$rc"
[[ "$output" == *"[ASSETS-OK]"* ]] \
    || fail "check_assets did not emit [ASSETS-OK]"

log "installed authoring asset consumer PASS"
