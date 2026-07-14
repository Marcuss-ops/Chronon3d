#!/usr/bin/env bash
# ==============================================================================
# tools/install_vcpkg_bootstrap_linux.sh
#
# TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV (FASE 1.5) — minimal-surface
# bootstrap of vcpkg + 5 catalog packages (glm + magic-enum + gtest + doctest +
# catch2) into ~/.vcpkg-clone/installed/x64-linux/ on a Debian/Ubuntu VPS.
#
# SCOPE: this script is the root-cause RESOLVER of every DEFERRED-WBH chore
# that env-blocks on missing glm / magic_enum / doctest (apr–jun 2026). Once
# it runs end-to-end on this VPS, the downstream F-chores become locally
# macchina-verificabili without depending on a remote working build host.
# See `docs/cert_sequence_wbh_protocol.md` §Pre-0 for the manual lineage,
# and `docs/FOLLOWUP_TICKETS.md` `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`
# for the canonical ticket.
#
# This script does NOT replace `tools/install_consumer_test.sh` (the
# canonical chronon3d SDK consumer CI orchestrator at ag-canonical-path).
# The user-spec named the latter file path informally; per AGENTS.md
# Cat-3 anti-dup the non-conflicting alternative lives here:
#   tools/install_vcpkg_bootstrap_linux.sh
# See TICKET ticket-body for explicit user-spec naming-resolution note.
#
# ------------------------------------------------------------
# Pipeline (5 steps):
#   P0  network probe  (fail-fast on github.com unreachable)
#   P1  idempotent clone  (~/.vcpkg-clone if absent)
#   P2  idempotent bootstrap  (~/.vcpkg-clone/vcpkg if absent or stale)
#   P3  idempotent package install  (5 marker-file gated installs)
#   P4  verify-triad  (vcpkg-version + 5 marker-files + lib/ non-empty)
#   P5  summary  (single-line JSON for downstream CI log scraping)
#
# Exit codes:
#   0 = VERIFY_TRIAD:PASS — packages installed + triads green
#   1 = VERIFY_TRIAD:FAIL — broken state, see diagnostic + remediation hint
#   2 = ENV_BLOCK         — missing prerequisite (apt-get / curl / github)
# ==============================================================================
set -euo pipefail

# ── Self-identifier + log channel ─────────────────────────────────────────
# AGENTS.md §"INFO-level diagnostic style" → declare GATE_NAME near top for
# grep-discoverability; emit both the canonical GATE_PASS at the end AND an
# additional `[INFO] ${GATE_NAME}: ...` one-liner (≤200 chars, on clean
# state only — never on FAIL).
readonly GATE_NAME="install_vcpkg_bootstrap_linux"

log()  { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
fail() { printf 'GATE_FAIL: %s\n' "$*" >&2; exit 1; }

# ── Constants ──────────────────────────────────────────────────────────────
VCPKG_ROOT="${HOME}/vcpkg-clone"
VCPKG_REPO_URL="https://github.com/microsoft/vcpkg.git"
TRIPLET="x64-linux"
INSTALLED_DIR="${VCPKG_ROOT}/installed/${TRIPLET}"

# 5 packages: CLI hyphenated form (matches vcpkg.json manifest convention;
# canonical source: `https://github.com/microsoft/vcpkg/tree/master/ports/<name>`).
# magic-enum is shipped under ports/magic-enum/, installed under include/magic_enum/.
PACKAGES=(glm magic-enum gtest doctest catch2)

# Marker files (per package, used for idempotency guards + verify triad).
# Format: pkg → marker-file-path (relative to INSTALLED_DIR)
declare -A MARKER=(
    [glm]="include/glm/glm.hpp"
    [magic-enum]="include/magic_enum/magic_enum.hpp"
    [gtest]="include/gtest/gtest.h"
    [doctest]="include/doctest/doctest.h"
    # catch2 v3 ships under include/catch2/; v2 ships catch.hpp. Accept either.
    [catch2_v3]="include/catch2/catch_test_macros.hpp"
    [catch2_v2]="include/catch2/catch.hpp"
)

log "++ bootstrap session begin =="
log "++ GATE_NAME       = ${GATE_NAME}"
log "++ VCPKG_ROOT      = ${VCPKG_ROOT}"
log "++ triplet         = ${TRIPLET}"
log "++ INSTALLED_DIR   = ${INSTALLED_DIR}"
log "++ packages        = ${PACKAGES[*]}"

# ── P0: Network probe ──────────────────────────────────────────────────────
# Fail-fast at the start (before git clone begins the 5-min shallow fetch)
# to surface Ubuntu-flaky-network patterns. A timeout of 8 seconds is
# generous for `github.com` round-trip.
log "P0: network probe (github.com round-trip ≤ 8 s)"
if ! curl --max-time 8 -fsSL -o /dev/null https://github.com 2>/dev/null; then
    fail "github.com unreachable — need network for git clone. fix: check connectivity + proxy + DNS"
fi
log "P0: github.com reachable"

# # apt-get presence guard (Debian/Ubuntu-only per VPS probe; multi-distro
# support is forward-point: TICKET-VCPKG-BOOTSTRAP-FEDORA/CENTOS/<other>)
if ! command -v apt-get >/dev/null 2>&1; then
    fail "apt-get not present on PATH — script targets Debian/Ubuntu only. Forward-point: TICKET-VCPKG-BOOTSTRAP-FEDORA for dnf support"
fi

# cmake version guard (analog of tools/install_consumer_test.sh's
# require_cmake_3_27). bootstrap-vcpkg.sh invokes cmake internally; if cmake
# is missing or too old, the gate fails confusingly inside bootstrap rather
# than here with a clean remediation hint. Fail-fast at P0.
# Accepts cmake >= 3.27 OR cmake >= 4.x (4.x is backward-compat per Kitware).
if ! command -v cmake >/dev/null 2>&1; then
    fail "cmake not present on PATH — vcpkg bootstrap-vcpkg.sh requires cmake >=3.27. fix: sudo apt-get install -y cmake"
fi
cmake_version_full="$(cmake --version | head -n 1 | awk '{print $3}')"
# RegEx guard catches BOTH empty AND non-empty malformed strings (e.g. "garbled", "unknown").
# Catches the silent-pass boundary: when the version string is not MAJOR.MINOR-shaped,
# fail-loud instead of failing downstream in a confusing find_package() error.
# AGENTS.md §honest-limitation: fail-loud > silent-pass on unrecognized.
if [[ ! "${cmake_version_full}" =~ ^[0-9]+\.[0-9]+ ]]; then
    fail "cmake version '${cmake_version_full}' does not match MAJOR.MINOR pattern. fix: install cmake >= 3.27 or >= 4.x from kitware.com or apt"
fi
cmake_major="$(echo "${cmake_version_full}" | awk -F. '{print $1}')"
cmake_minor="$(echo "${cmake_version_full}" | awk -F. '{print $2}')"
if [[ "${cmake_major}" -lt 3 ]] || { [[ "${cmake_major}" -eq 3 ]] && [[ "${cmake_minor}" -lt 27 ]]; }; then
    fail "cmake version ${cmake_version_full} below required >= 3.27 or 4.x. fix: upgrade or sudo apt-get install -y cmake"
fi
log "P0: cmake version OK (${cmake_version_full})"

# ── P1: Idempotent vcpkg clone (~/.vcpkg-clone/) ───────────────────────────
log "P1: idempotent vcpkg clone → ${VCPKG_ROOT}"
if [[ -d "${VCPKG_ROOT}/.git" ]] && [[ -x "${VCPKG_ROOT}/vcpkg" ]]; then
    log "P1: SKIP — ${VCPKG_ROOT}/vcpkg already present (clone + bootstrap done in a prior run)"
else
    log "P1: git clone ${VCPKG_REPO_URL} → ${VCPKG_ROOT}  (≈5 min, single shallow fetch)"
    git clone "${VCPKG_REPO_URL}" "${VCPKG_ROOT}" 1>&2 \
        || fail "git clone failed — check network/auth + retry"
fi

# ── P2: Idempotent vcpkg bootstrap ─────────────────────────────────────────
# `~/.vcpkg-clone/vcpkg` is the bootstrap result. Skip if executable already
# present. bootstrap-vcpkg.sh requires Linux/MacOS + cmake; we already
# checked apt-get (Debian) + cmake is assumed-present (chronon3d toolchain
# prerequisite, validated by chronon-linux.sh).
log "P2: idempotent vcpkg bootstrap"
if [[ -x "${VCPKG_ROOT}/vcpkg" ]]; then
    log "P2: SKIP — ${VCPKG_ROOT}/vcpkg executable already present"
else
    log "P2: invoking bootstrap-vcpkg.sh -disableMetrics"
    "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics \
        || fail "bootstrap-vcpkg.sh failed — see vcpkg stderr for cmake/toolchain rot"
fi

# Fetch full history if shallow (baseline-lookup requirement: vcpkg
# manifest mode needs `cb2981c4e03d421fa03b9bb5044cd1986180e7e4` reachable).
log "P2: checking shallow-repository + auto-fetching full history (manifest baseline)"
if "${VCPKG_ROOT}/vcpkg" --version >/dev/null 2>&1 && \
   git -C "${VCPKG_ROOT}" rev-parse --is-shallow-repository 2>/dev/null | grep -q true; then
    log "P2: shallow repo detected — fetching full history for baseline lookup"
    git -C "${VCPKG_ROOT}" fetch --unshallow 2>&1 | tail -3
fi

# ── P3: Idempotent package installs (5 marker-file-gated) ─────────────────
log "P3: package install loop (5 packages) — VCPKG_INSTALLED_DIR=${INSTALLED_DIR}"
log "P3: pre-existing marker inspection:"

# Pre-scan: emit SKIP-IF-PRESENT for each marker to make re-runnable
# idempotency immediately visible in the log.
declare -A NEED_INSTALL=()
for pkg in "${PACKAGES[@]}"; do
    marker_path="${INSTALLED_DIR}/${MARKER[$pkg]:-}"
    if [[ -n "${marker_path}" ]] && [[ -f "${marker_path}" ]]; then
        log "P3: SKIP ${pkg} — ${marker_path} already present"
        NEED_INSTALL[$pkg]=0
    else
        log "P3: NEED ${pkg} — marker ${marker_path} MISSING"
        NEED_INSTALL[$pkg]=1
    fi
done

# Install only what's missing. Group-by-pkg preserves any install-order
# hints vcpkg may print (better debuggability vs. installing all together).
for pkg in "${PACKAGES[@]}"; do
    if [[ "${NEED_INSTALL[$pkg]}" -ne 1 ]]; then continue; fi
    log "P3: INSTALL ${pkg} (≈1–3 min per pkg; vcpkg exits on install error by default)"
    # MANIFEST-MODE ESCAPE: vcpkg auto-enables manifest mode when a
    # vcpkg.json exists in CWD (the repo root contains vcpkg.json),
    # rejecting per-package install with: "In manifest mode, vcpkg
    # install does not support individual package arguments".  We cd
    # into /tmp before invoking vcpkg to bypass manifest auto-detect;
    # the package-install inputs are NOT affected by the CWD swap (vcpkg
    # resolves the install target via VCPKG_ROOT + the triplet flag).
    # This is the canonical, version-independent escape (works across
    # vcpkg CLI revisions: --no-manifest flag name, --classic flag name,
    # etc., differ across releases).
    (cd /tmp && "${VCPKG_ROOT}/vcpkg" install "${pkg}" \
        --triplet "${TRIPLET}") 1>&2 \
        || fail "vcpkg install ${pkg} failed — inspect ${VCPKG_ROOT}/vcpkg config.log + retry"
    # Defensive `:-` on MARKER[$pkg] — for catch2, the canonical key
    # `MARKER[catch2]` is populated by the fall-through block below
    # (lines ~191-197) AFTER this loop iteration completes, so during
    # the loop iteration `MARKER[catch2]` is temporarily unbound.  The
    # `:-` default-empty preserves set -u semantics (nounset) without
    # crashing the script.  See commit context for the manifest-mode
    # + nounset bug-fix lineage.
    log "P3: INSTALLED ${pkg} — marker ${MARKER[$pkg]:-} should now be present"
done

# catch2 fall-through: detect which marker (v2 vs v3) is actually present.
if [[ -f "${INSTALLED_DIR}/${MARKER[catch2_v3]}" ]]; then
    MARKER[catch2]="${MARKER[catch2_v3]}"
    log "P3: catch2 detected as v3.x layout (include/catch2/catch_test_macros.hpp)"
elif [[ -f "${INSTALLED_DIR}/${MARKER[catch2_v2]}" ]]; then
    MARKER[catch2]="${MARKER[catch2_v2]}"
    log "P3: catch2 detected as v2.x layout (include/catch2/catch.hpp)"
else
    log "P3: WARNING catch2 marker detection DEFERRED to verify triad"
fi

# ── P4: Verify triad (vcpkg-version + 5 marker-files + lib/ non-empty) ─────
log "P4: verify triad (vcpkg-version + markers + lib/) — begin"

# (a) vcpkg binary version
vcpkg_version_output="$("${VCPKG_ROOT}/vcpkg" --version 2>&1 || true)"
if [[ -z "${vcpkg_version_output}" ]]; then
    fail "P4(a): ~/.vcpkg-clone/vcpkg --version returned empty — bootstrap corrupted"
fi
log "P4(a): vcpkg version = $(echo "${vcpkg_version_output}" | head -1)"

# (b) 5 marker files
log "P4(b): marker presence inspection"
markers_present=0
markers_total=${#PACKAGES[@]}
for pkg in "${PACKAGES[@]}"; do
    marker_rel="${MARKER[$pkg]:-}"
    [[ -z "${marker_rel}" ]] && { log "P4(b): ${pkg}  NO_MARKER_DEFINED (skip)"; continue; }
    marker_abs="${INSTALLED_DIR}/${marker_rel}"
    if [[ -f "${marker_abs}" ]]; then
        log "P4(b): ${pkg}  PRESENT  ${marker_rel}"
        markers_present=$((markers_present + 1))
    else
        log "P4(b): ${pkg}  MISSING  ${marker_rel}  <-- remediate: re-run P3"
    fi
done
if [[ "${markers_present}" -ne "${markers_total}" ]]; then
    fail "P4(b): ${markers_present}/${markers_total} markers present — re-run P3 loop"
fi

# (c) lib/ non-empty
if [[ ! -d "${INSTALLED_DIR}/lib" ]]; then
    fail "P4(c): ${INSTALLED_DIR}/lib MISSING — install produced no artifacts"
fi
lib_count=$(find "${INSTALLED_DIR}/lib" -maxdepth 2 -name '*.a' 2>/dev/null | wc -l)
if [[ "${lib_count}" -lt 3 ]]; then
    fail "P4(c): only ${lib_count} .a files in ${INSTALLED_DIR}/lib (expected ≥3 across 5 pkgs)"
fi
log "P4(c): lib/  PRESENT  ${lib_count} .a files"

# ── P5: Summary line + downstream-usage hint ──────────────────────────────
log "P5: verify triad PASSED — bootstrap session complete"
log ""
log "USAGE: to consume from downstream phases, export:"
log "  export VCPKG_INSTALLED_DIR=\"${VCPKG_ROOT}/installed\""
log "  export VCPKG_TARGET_TRIPLET=\"${TRIPLET}\""
log "  export CMAKE_TOOLCHAIN_FILE=\"${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake\""
log ""
log "NOTE: this is the vcpkg-clone SEPARATE install. The canonical"
log "  Chronon3D SDK toolchain continues to use <REPO>/vcpkg_bootstrap/"
log "  (single source of truth per cmake/Chronon3DVcpkgToolchain.cmake"
log "  Invariant I1). Downstream phases may override the canonical"
log "  toolchain via env-var (the wrapper honors VCPKG_INSTALLED_DIR)."
log ""

# Canonical GATE_PASS + AGENTS.md INFO-level diagnostic style addizionale
printf 'GATE_PASS: vcpkg-bootstrap triad green (%d/%d markers + %d libs + vcpkg=%s)\n' \
    "${markers_present}" "${markers_total}" "${lib_count}" \
    "$(echo "${vcpkg_version_output}" | head -1)"
# INFO addizionale on clean state (≤ 200 chars; never on FAIL)
printf '[INFO] %s: clean triad across 5 marker-files (%s)%s\n' \
    "${GATE_NAME}" \
    "$(IFS=,; echo "${PACKAGES[*]}")" \
    " — boot root=${VCPKG_ROOT}, triplet=${TRIPLET}, lib_count=${lib_count}"

# Single-line JSON for CI log scraping (forward-compat with chronon3d_verify_linux family)
printf '{"gate":"install_vcpkg_bootstrap_linux","status":"passed","triplet":"%s","installed_dir":"%s","markers_present":%d,"markers_total":%d,"lib_count":%d}\n' \
    "${TRIPLET}" "${INSTALLED_DIR}" "${markers_present}" "${markers_total}" "${lib_count}"

exit 0
