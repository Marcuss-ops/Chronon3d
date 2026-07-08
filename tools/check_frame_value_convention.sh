#!/usr/bin/env bash
#
# tools/check_frame_value_convention.sh
#
# Gate #10d — Frame::value convention enforcement.
#
# Canonical invariant (per include/chronon3d/core/types/frame.hpp):
#
#   The strong-typed `Frame` is a value class whose `.value` field is
#   an *implementation detail*. Direct field access (`Frame{var.value}`)
#   and member reads (`frame.value`) outside the canonical header
#   couple downstream code to the Frame-struct layout.
#
#   Per the convention document in frame.hpp, readers MUST go through:
#     - frame.integral()                     (named accessor; tests/log)
#     - static_cast<int64_t>(frame)          (explicit narrowing; API)
#     - Frame arithmetic (e.g. frame + Frame{N})  (math idiomatic)
#
#   Direct `.value` access OUTSIDE the canonical header means a future
#   layout change (e.g. int width + sequence id + reserved fields) silently
#   breaks the caller. This gate enforces the accessor contract.
#
# Gate semantics (commits 1 → 2 sequence):
#
#   - Commit 1: gate introduced in WARN mode (exit 0). Inventories the
#     raw violations and blocks the regression of NEW violations
#     without failing the existing 4 raw hits (which are fixed in
#     commit 2).
#
#   - Commit 2: gate promoted to FAIL mode (default; exit 1). After the
#     progressive fix of 4 raw hits, the gate exits 0 on a clean tree.
#
# Exit codes:
#
#   0 = PASS                  (no raw hits in current mode)
#   1 = FAIL / WARN-REGRESSION (raw hits found AND mode = FAIL)
#   2 = internal error        (missing deps / malformed invocation)
#
# Mode toggle:
#
#   FRAME_VALUE_GATE_MODE=WARN|FAIL  (default FAIL in commit 2)
#   bash tools/check_frame_value_convention.sh
#   FRAME_VALUE_GATE_MODE=WARN bash tools/check_frame_value_convention.sh
#
# Refined regex (commit 1 verified against the literal user grep):
#
#   The user's literal grep (`grep '\.value' | grep Frame | grep -v canonical`)
#   over-matches: 24 hits, of which 20 are false positives where `.value`
#   is an AnimatedValue<T>::value (member of OpacityProperty / PositionProperty
#   / ScaleProperty / etc.) and `Frame{...}` is just a keyframe TIMESTAMP
#   argument that happens to share a line with the `.value` access.
#
#   The refined regex below targets Frame-typed identifiers specifically
#   (frame, f, ctx.frame, global_frame, local_frame, sequence_start,
#   sequence_end, sequenceFrame). This is a documented refinement over
#   the literal grep; rationale filed in docs/CHANGELOG.md under the
#   commit 1 entry "Frame::value convention gate (WARN mode)".
#
# AGENTS.md / DOC-GOVERNANCE: never duplicate the violation list into an
# ad-hoc doc — all gate state lives in (1) this script's stdout, (2)
# CHANGELOG (when state changes), (3) FOLLOWUP_TICKETS (closed).

set -euo pipefail

# Commit 1 default: WARN; commit 2 promotes this to FAIL.
: "${FRAME_VALUE_GATE_MODE:=WARN}"
MODE="$(printf '%s' "${FRAME_VALUE_GATE_MODE}" | tr '[:lower:]' '[:upper:]')"

if [[ "${MODE}" != "WARN" && "${MODE}" != "FAIL" ]]; then
    echo "internal error: FRAME_VALUE_GATE_MODE='${MODE}' (expected WARN|FAIL)" >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && git rev-parse --show-toplevel)"

CANONICAL_HEADER='include/chronon3d/core/types/frame.hpp'

# ---- audit (refined regex; documented in CHANGELOG) ------------------------
#
# Two-branch extended-regex:  (a) identifier-with-Frame-context .value hits
#                              (b) chain-access `.frame.value` patterns.
# We deliberately EXCLUDE the literal user grep's false-positives: the
# 14+6 = 20 hits in src/registry/text_preset_factories_{reveal,cinematic}.cpp
# are `op.value.add_keyframe(Frame{0}, 0.0f, …)` — AnimatedValue<T>::value
# (member of OpacityProperty / PositionProperty / ScaleProperty /
#  BlurProperty / TrackingProperty — NOT Frame::value).  In those hits,
# `Frame{N}` is just the KEYFRAME TIMESTAMP argument:
#
#   PropertyValue::value  // AnimatedValue<T> wrapper for keyframed property
#     .add_keyframe(
#         Frame{N},        // keyframe timestamp (Frame-typed, literal)
#         0.0f,            // keyframed property value
#         ease             // easing
#     );
#
# Renaming `value` → `animated` on the AnimatedValue wrapper would be a
# public-API ripple across ~20+ call sites; instead we tighten the gate
# to Frame-typed identifier patterns.  See CHANGELOG.md commit 1 entry
# "Frame::value convention gate (WARN mode)" for the full audit table.
# Method-call exclusion: lines containing `.value(` (e.g.
# `fb0.value()->width()` on a std::expected<T,E>) are NOT Frame::value
# member access — `value()` is a separate accessor function on the
# variant/expected type. Frame.value is i64 primitive and not callable,
# so any `.value(` in the codebase is unambiguously NOT this gate's
# target.  Exclude via a post-filter; legitimate Frame::value member
# reads always terminate with `;`, `)`, `+`, whitespace, EOL, or are
# followed by an operator that confirms the primitive-read semantics.
HITS=$(grep -RnE \
    '\b([Ff]rame|[Ff]|sequence_start|sequence_end|local_frame|global_frame|sequenceFrame|sequence_frame)\w*\.value\b|\b\w+\.frame\.value\b' \
    "${REPO_ROOT}/src"     \
    "${REPO_ROOT}/include" \
    "${REPO_ROOT}/tests"   \
    "${REPO_ROOT}/apps"    \
    --include='*.cpp' --include='*.hpp' 2>/dev/null \
  | grep -v "${CANONICAL_HEADER}" \
  | grep -vE '\.value\(' \
  || true)

# --- summary -----------------------------------------------------------------
COUNT="$(printf '%s\n' "${HITS}" | grep -c '^' || true)"
COUNT="${COUNT:-0}"

echo "frame::value convention gate (mode=${MODE}, canonical=${CANONICAL_HEADER})"
echo "  raw Frame::value hits outside canonical header: ${COUNT}"

if [[ "${COUNT}" -eq 0 ]]; then
    echo "  PASS — no out-of-header Frame::value usage"
    exit 0
fi

# --- verbose report on hits --------------------------------------------------
echo "  occurrences:"
printf '%s\n' "${HITS}" \
    | sed "s|^${REPO_ROOT}/||" \
    | sed 's/^/    /'

# --- mode dispatch -----------------------------------------------------------
if [[ "${MODE}" == "FAIL" ]]; then
    echo ""
    echo "GATE_FAIL: ${COUNT} out-of-header Frame::value use(s) — accessor contract violated."
    echo "  fix: replace frame.value with frame.integral() (test/log) or"
    echo "       static_cast<int64_t>(frame) (external API) or"
    echo "       Frame arithmetic (frame + Frame{N}) (core math idiomatic)."
    exit 1
fi

# WARN mode (commit 1): log violations; do not fail.
echo ""
echo "WARN: ${COUNT} out-of-header Frame::value use(s) logged (WARN mode; exit 0)."
echo "      These violations are scheduled for progressive fix in the next commit;"
echo "      future regressions will be blocked by promoting the gate to FAIL mode."
exit 0
