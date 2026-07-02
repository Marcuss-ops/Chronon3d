#!/usr/bin/env bash
# tools/sdk/check_feature_ghosts.sh — Step 2.5 (feature-OFF ghost sweep).
#
# Validates that the SDK's feature toggles (CHRONON3D_BUILD_DIAGNOSTICS
# and CHRONON3D_BUILD_CONTENT) actually DROP their respective .o files
# from libchronon3d_sdk_impl.a when OFF.  Companion to the manifest
# count gate (Step 3.5): the manifest proves the archive contains the
# EXPECTED objects, while this ghost sweep proves the build pipeline
# actually drops gated TUs when the toggle is OFF.
#
# Procedure:
#   1. Reconfigure the SAME $SDK_BUILD with DIAG=OFF + CONTENT=OFF
#      (preserving all other cache variables).
#   2. Rebuild via the `chronon3d_sdk_impl` target (CMake ≥3.27 natively
#      aggregates OBJECT .o files into STATIC archives).
#   3. Reinstall into the SAME $SDK_PREFIX.
#   4. Inspect the OFF archive: assert NO diagnostics.cpp.o / NO
#      diagnostics-software.cpp.o / NO content.cpp.o made it.
#
# Outputs a single GHOST-OK or GHOST-FAIL marker to stderr; the
# orchestrator's cleanup trap gets propagated through the bash
# subshell-level invocation.
#
# Env inputs (REQUIRED):
#   SDK_PREFIX  — install prefix root (re-install target)
#   SDK_BUILD   — cmake build dir (re-configure + rebuild target)
#   REPO_ROOT   — repo root (passed to cmake -S)
#   PRESET      — cmake preset (must match the prior build's preset)
#
# Invocation pattern:  bash tools/sdk/check_feature_ghosts.sh
#
# Exit codes:
#   0 = GHOST-OK (no diagnostics/content .cpp.o leaked)
#   1 = GHOST-FAIL (leak detected OR reconfigure/rebuild/install failed)

HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=./common.sh
source "$HERE/common.sh"

# ── Required env vars ─────────────────────────────────────────────────
: "${SDK_PREFIX:?SDK_PREFIX env var required}"
: "${SDK_BUILD:?SDK_BUILD env var required}"
: "${REPO_ROOT:?REPO_ROOT env var required (cmake source dir)}"
: "${PRESET:?PRESET env var required (cmake preset)}"

log "ghost sweep starting (DIAG=OFF, CONTENT=OFF; SDK_BUILD=$SDK_BUILD, SDK_PREFIX=$SDK_PREFIX)"

# ── 1. Reconfigure OFF ────────────────────────────────────────────────
cmake -S "$REPO_ROOT" -B "$SDK_BUILD" --preset "$PRESET" \
    -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
    -DCHRONON3D_BUILD_DIAGNOSTICS=OFF \
    -DCHRONON3D_BUILD_CONTENT=OFF 1>&2 \
    || fail "ghost sweep: cmake reconfigure (DIAG=OFF, CONTENT=OFF) failed"

# ── 2. Rebuild chronon3d_sdk_impl ─────────────────────────────────────
# CMake ≥3.27 natively aggregates OBJECT .o into STATIC archives;
# the former sdk_archive_merge custom target (ar crs workaround) is retired.
cmake --build "$SDK_BUILD" --target chronon3d_sdk_impl -j8 1>&2 \
    || fail "ghost sweep: chronon3d_sdk_impl rebuild failed"

# ── 3. Reinstall into the SAME prefix ────────────────────────────────
cmake --install "$SDK_BUILD" --prefix "$SDK_PREFIX" 1>&2 \
    || fail "ghost sweep: cmake --install failed"

# ── 4. Inspect OFF archive ────────────────────────────────────────────
impl_archive_off="$(find "$SDK_PREFIX" -type f -name 'libchronon3d_sdk_impl.a' 2>/dev/null | head -1 || true)"
[[ -n "$impl_archive_off" ]] || fail "libchronon3d_sdk_impl.a missing after OFF re-install"

GATE_TMP="$(mktemp_dir chronon3d_install_gate_off)"
cleanup_register "$GATE_TMP"
ar_ghost_list="$GATE_TMP/ar_off.txt"
ar t "$impl_archive_off" | sort > "$ar_ghost_list"

ghost_fail_count=0
ghost_fail_list=""
# Each ghost object is the EXPECTED to be DROPPED when its respective
# CHRONON3D_BUILD_* flag is OFF.  Add to this list as new gated TUs
# land in the SDK manifest.
for ghost in chronon3d_diagnostics.cpp.o \
             chronon3d_backend_software_diagnostics.cpp.o \
             chronon3d_content.cpp.o; do
    if grep -qF -- "$ghost" "$ar_ghost_list"; then
        log "GHOST-FAIL: $ghost (rebuilt with DIAG=OFF/CONTENT=OFF but leaked into OFF archive)"
        ghost_fail_count=$((ghost_fail_count + 1))
        ghost_fail_list="${ghost_fail_list}${ghost_fail_list:+, }$ghost"
    fi
done

if (( ghost_fail_count > 0 )); then
    fail "Ghost sweep: $ghost_fail_count feature-OFF .cpp.o leaked: $ghost_fail_list.  " \
         "Registry must drop these when their CHRONON3D_BUILD_* flag is OFF."
fi

log "GHOST-OK: feature-OFF archive is clean (no diagnostics/content .cpp.o leak)"
exit 0
