#!/usr/bin/env bash
# ============================================================================
# check_transition_id_dispatch.sh
#
# TRN-01 architecture gate:
#   Blocks new string-based `if (transition_id == "...")` branches in
#   src/render_graph/nodes/transition_node.cpp unless the new ID has been
#   added to the canonical catalog below AND documented in
#   docs/tickets/TICKET-TRN-01.md.
#
#   The gate only inspects the layer transition dispatch file
#   (src/render_graph/nodes/transition_node.cpp). It does NOT modify code and
#   does not enforce the catalog for Camera or Text transitions (those are
#   tracked in the ticket and will be gated in TRN-02 when a unified registry
#   is introduced). It is intentionally a stand-alone script in TRN-01; wiring
#   into pre-push is a forward-point in the ticket.
# ============================================================================

set -euo pipefail

GATE_NAME="check_transition_id_dispatch"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
TARGET="${REPO_ROOT}/src/render_graph/nodes/transition_node.cpp"

# Canonical catalog of layer transition IDs.
# Keep in sync with docs/tickets/TICKET-TRN-01.md §2 Inventario LayerReveal.
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

if [ ! -f "${TARGET}" ]; then
    echo "GATE_FAIL: ${GATE_NAME}: target file not found: ${TARGET}" >&2
    exit 1
fi

# Extract literal strings matched by `transition_id == "..."`.
# This works on the exact C++ expression used in the dispatch branch.
IDS_IN_FILE=$(grep -oE 'transition_id\s*==\s*"[^"]+"' "${TARGET}" \
    | sed -E 's/transition_id\s*==\s*"([^"]+)"/\1/' \
    | sort -u)

MISSING=()
UNEXPECTED=()

# Check every expected ID is present in the file.
for id in "${!EXPECTED[@]}"; do
    if ! grep -qxF "${id}" <<< "${IDS_IN_FILE}"; then
        MISSING+=("${id}")
    fi
done

# Check every ID found in the file is expected.
while IFS= read -r id; do
    [ -n "${id}" ] || continue
    if [ -z "${EXPECTED[${id}]+x}" ]; then
        UNEXPECTED+=("${id}")
    fi
done <<< "${IDS_IN_FILE}"

if [ ${#MISSING[@]} -ne 0 ] || [ ${#UNEXPECTED[@]} -ne 0 ]; then
    echo "GATE_FAIL: ${GATE_NAME}: layer transition_id dispatch catalog mismatch in ${TARGET}" >&2
    if [ ${#MISSING[@]} -ne 0 ]; then
        echo "  missing from file: ${MISSING[*]}" >&2
    fi
    if [ ${#UNEXPECTED[@]} -ne 0 ]; then
        echo "  unexpected in file: ${UNEXPECTED[*]}" >&2
    fi
    # shellcheck disable=SC2068
    echo "  expected catalog: ${!EXPECTED[@]}" | sort >&2
    exit 1
fi

cnt=$(wc -l <<<"${IDS_IN_FILE}" | tr -d '[:space:]')
echo "GATE_PASS: ${GATE_NAME}: layer transition_id dispatch matches canonical catalog (${cnt} ID(s))"
echo "[INFO] ${GATE_NAME}: ${cnt} transition_id match(es) verified in ${TARGET}"
exit 0
