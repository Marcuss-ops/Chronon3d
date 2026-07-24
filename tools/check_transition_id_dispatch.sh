#!/usr/bin/env bash
# ============================================================================
# check_transition_id_dispatch.sh
#
# TRN-01 architecture gate:
#   1. Keeps the canonical LayerReveal transition ID list in sync with the
#      LayerTransitionCatalog (single source of truth:
#      src/render_graph/transition/transition_catalog.cpp).
#   2. Blocks new string-based `transition_id` dispatch across the whole
#      codebase. All layer transition behaviour must be resolved through the
#      catalog; no `if (.*transition_id == "...")` branches are allowed
#      outside the catalog and a few explicit sentinel checks.
#
#   The gate is intentionally stand-alone. It does NOT modify code.
#   Wiring into the pre-push chain is a forward-point (TRN-01 master ticket).
# ============================================================================

set -euo pipefail

GATE_NAME="check_transition_id_dispatch"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

CATALOG="${REPO_ROOT}/src/render_graph/transition/transition_catalog.cpp"

# Directories that contain C++ transition code. Tests are included so that
# test code cannot introduce new string dispatch either.
SCAN_DIRS=()
for dir in src include tests content examples; do
    [ -d "${REPO_ROOT}/${dir}" ] && SCAN_DIRS+=("${REPO_ROOT}/${dir}")
done

# Canonical catalog of LayerReveal transition IDs.
# Any addition/removal must be accompanied by a catalog update and ADR
# review (TRN-01). Keep in sync with docs/tickets/TICKET-TRN-01.md.
declare -A EXPECTED
EXPECTED=(
    ["none"]=1
    ["crossfade"]=1
    ["slide"]=1
    ["wipe_linear"]=1
    ["smooth_wipe"]=1
    ["circle_iris"]=1
    ["flash"]=1
    ["procedural_remotion"]=1
    ["remotion"]=1
)

if [ ! -f "${CATALOG}" ]; then
    echo "GATE_FAIL: ${GATE_NAME}: canonical catalog not found: ${CATALOG}" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# 1. Verify the canonical catalog registers exactly the expected IDs.
# ---------------------------------------------------------------------------
IDS_IN_CATALOG=$(grep -oE 'register_transition\s*\(\s*"[^"]+"' "${CATALOG}" \
    | sed -E 's/register_transition\s*\(\s*"([^"]+)"/\1/' \
    | sort -u)

CATALOG_MISSING=()
CATALOG_UNEXPECTED=()

for id in "${!EXPECTED[@]}"; do
    if ! grep -qxF "${id}" <<< "${IDS_IN_CATALOG}"; then
        CATALOG_MISSING+=("${id}")
    fi
done

while IFS= read -r id; do
    [ -n "${id}" ] || continue
    if [ -z "${EXPECTED[${id}]+x}" ]; then
        CATALOG_UNEXPECTED+=("${id}")
    fi
done <<< "${IDS_IN_CATALOG}"

if [ ${#CATALOG_MISSING[@]} -ne 0 ] || [ ${#CATALOG_UNEXPECTED[@]} -ne 0 ]; then
    echo "GATE_FAIL: ${GATE_NAME}: LayerTransitionCatalog out of sync with canonical inventory" >&2
    if [ ${#CATALOG_MISSING[@]} -ne 0 ]; then
        echo "  missing in catalog: ${CATALOG_MISSING[*]}" >&2
    fi
    if [ ${#CATALOG_UNEXPECTED[@]} -ne 0 ]; then
        echo "  unexpected in catalog: ${CATALOG_UNEXPECTED[*]}" >&2
    fi
    echo "  expected canonical IDs: ${!EXPECTED[@]}" | sort >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# 2. Block string-based transition_id dispatch everywhere except sentinel
#    checks (== "none" / != "none" / .empty()) and the catalog itself.
# ---------------------------------------------------------------------------

# Forbidden patterns: any direct string comparison on `transition_id`
# (member access or bare variable) using ==/!=, or a .compare() call.
# We still allow sentinel comparisons against "none" and the empty string,
# which are used by the graph builder to decide whether to insert a node.

RAW_FORBIDDEN_EQ=$(grep -rnE '\btransition_id\s*(==|!=)\s*"[^"]*"' \
    "${SCAN_DIRS[@]}" 2>/dev/null || true)
RAW_FORBIDDEN_COMPARE=$(grep -rnE '\btransition_id\s*\.\s*compare\s*\(\s*"[^"]*"\s*\)' \
    "${SCAN_DIRS[@]}" 2>/dev/null || true)
RAW_FORBIDDEN="${RAW_FORBIDDEN_EQ}
${RAW_FORBIDDEN_COMPARE}"

# Drop C++ comments (both whole-line and inline) before evaluating matches.
# This prevents doc comments or trailing remarks from being flagged.
FILTERED=""
while IFS= read -r line; do
    [ -n "${line}" ] || continue
    # Strip C++ comments so doc strings/trailing remarks don't count.
    code="${line%%//*}"
    # If nothing is left, the line was only a comment.
    if [[ "${code}" =~ ^[[:space:]]*$ ]]; then
        continue
    fi
    FILTERED="${FILTERED}${line}"$'\n'
done <<< "${RAW_FORBIDDEN}"

# Allow only sentinel checks against "none" and the empty string.
VIOLATIONS=""
while IFS= read -r line; do
    [ -n "${line}" ] || continue
    compared=$(echo "${line}" | sed -E 's/[^"]*"([^"]*)".*/\1/')
    if [ "${compared}" = "none" ] || [ "${compared}" = "" ]; then
        continue
    fi
    VIOLATIONS="${VIOLATIONS}${line}"$'\n'
done <<< "${FILTERED}"

if [ -n "${VIOLATIONS}" ]; then
    echo "GATE_FAIL: ${GATE_NAME}: forbidden string-based transition_id dispatch detected" >&2
    echo "${VIOLATIONS}" | sed '/^$/d' >&2
    echo "  Only LayerTransitionCatalog may map transition_id strings to behaviour." >&2
    echo "  Sentinel checks against 'none' / empty are allowed; everything else is not." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# 3. Ensure LayerTransitionCatalog::register_transition is the only place in
#    src/ / include/ that registers layer transition IDs.
# ---------------------------------------------------------------------------
RAW_REGISTRATIONS=$(grep -rnE 'register_transition\s*\(\s*"[^"]+"' \
    "${REPO_ROOT}/src" "${REPO_ROOT}/include" "${REPO_ROOT}/content" "${REPO_ROOT}/examples" \
    2>/dev/null || true)

# Drop lines that are only comments before deciding whether a registration
# is legitimate.
REGISTRATIONS=""
while IFS= read -r line; do
    [ -n "${line}" ] || continue
    code="${line%%//*}"
    [[ "${code}" =~ ^[[:space:]]*$ ]] && continue
    REGISTRATIONS="${REGISTRATIONS}${line}"$'\n'
done <<< "${RAW_REGISTRATIONS}"

BAD_REGISTRATIONS=""
while IFS= read -r line; do
    [ -n "${line}" ] || continue
    file="${line%%:*}"
    if [[ "${file}" != "${CATALOG}" ]]; then
        BAD_REGISTRATIONS="${BAD_REGISTRATIONS}${line}"$'\n'
    fi
done <<< "${REGISTRATIONS}"

if [ -n "${BAD_REGISTRATIONS}" ]; then
    echo "GATE_FAIL: ${GATE_NAME}: LayerTransitionCatalog registration found outside the canonical catalog" >&2
    echo "${BAD_REGISTRATIONS}" | sed '/^$/d' >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# PASS
# ---------------------------------------------------------------------------
cnt=$(wc -l <<<"${IDS_IN_CATALOG}" | tr -d '[:space:]')
echo "GATE_PASS: ${GATE_NAME}: ${cnt} canonical layer transition ID(s) in catalog; no forbidden string dispatch"
echo "[INFO] ${GATE_NAME}: catalog synchronized with ${cnt} LayerReveal IDs; string-dispatch scan clean"
exit 0
