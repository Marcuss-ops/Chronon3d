#!/usr/bin/env bash
# tools/audit_aggregate_archive.sh
# ─────────────────────────────────────────────────────────────────────
# WP-0 (PR 0.5) — Aggregate archive audit.
#
# Inspects one or more `.a` archives and reports:
#   - total size (bytes);
#   - member count (via `ar t`);
#   - debug-info share (via `nm --debug-syms` heuristic — accurate on
#     Linux/GCC/Clang);
#   - apparent duplicate objects across archives (same basename appearing
#     in more than one archive).
#
# Designed to be idempotent and OFFLINE: the script does NOT build,
# configure, or write outside the working directory. Run it on existing
# build artifacts (e.g. `build/chronon/linux-release/src/libchronon3d_sdk_impl.a`)
# to get a reproducible size + structure snapshot.
#
# NOT wired into the gate pipeline by default — it is an audit, not a
# pass/fail check. Results are intended to populate the WP-0 / PR 0.5
# "Aggregate archive size" record in `docs/refactor-roadmap/00-baseline-and-gates.md`.
#
# Usage:
#   tools/audit_aggregate_archive.sh <archive.a> [<archive.a> ...]
#   tools/audit_aggregate_archive.sh --json <out.json> <archive.a> [<archive.a> ...]
#
# Exit codes:
#   0 = audit completed (results MAY show large archives — no threshold
#       is enforced; PR 0.5 is "document before optimizing")
#   1 = tooling error (missing `ar`, `nm`, `du`; archive paths not found)
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

print_usage() {
    cat <<EOF
Usage: $0 [--json OUTPUT] <archive.a> [<archive.a> ...]

Audits .a archives for size, member count, debug-info share, and apparent
duplicate objects. Prints a human-readable report to stdout; with --json,
writes a machine-readable summary alongside.

Arguments:
  <archive.a>            One or more .a archive paths (must exist).

Options:
  --json OUTPUT          Additionally write JSON summary to OUTPUT.
  --help                 Show this help.
EOF
}

# ────────────────────────────────────────────────────────────────────────
# Defaults / arg parsing
# ────────────────────────────────────────────────────────────────────────
JSON_OUT=""
ARCHIVES=()

while [ $# -gt 0 ]; do
    case "$1" in
        --json)
            shift
            JSON_OUT="${1:-}"
            if [ -z "$JSON_OUT" ]; then
                echo "ERROR: --json requires an output path" >&2
                exit 1
            fi
            shift
            ;;
        --help|-h)
            print_usage
            exit 0
            ;;
        --*)
            echo "ERROR: unknown option: $1" >&2
            print_usage >&2
            exit 1
            ;;
        *)
            ARCHIVES+=("$1")
            shift
            ;;
    esac
done

if [ "${#ARCHIVES[@]}" -eq 0 ]; then
    print_usage >&2
    exit 1
fi

# ────────────────────────────────────────────────────────────────────────
# Tooling guard
# ────────────────────────────────────────────────────────────────────────
for tool in ar nm du awk grep find; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: required tool '$tool' not on PATH" >&2
        exit 1
    fi
done

# Verify each archive path exists.
for a in "${ARCHIVES[@]}"; do
    if [ ! -f "$a" ]; then
        echo "ERROR: archive not found: $a" >&2
        exit 1
    fi
done

# ────────────────────────────────────────────────────────────────────────
# Per-archive analysis
# ────────────────────────────────────────────────────────────────────────
TMP_DIR=""
cleanup() {
    local rc=$?
    [[ -n "$TMP_DIR" ]] && rm -rf "$TMP_DIR"
    exit "$rc"
}
trap cleanup EXIT
TMP_DIR="$(mktemp -d)"

ALL_BASENAMES_FILE="$TMP_DIR/all_basenames.txt"
: > "$ALL_BASENAMES_FILE"

JSON_RECORDS=()

printf '%-30s  %12s  %8s  %12s  %s\n' \
    "Preset" "Size(B)" "Members" "DebugShare" "Path"
printf '%-30s  %12s  %8s  %12s  %s\n' \
    "------------------------------" "------------" "--------" "------------" "----"

for a in "${ARCHIVES[@]}"; do
    abs_a="$(readlink -f "$a")"
    SIZE_B=$(du -b "$abs_a" | awk '{print $1}')
    MEMBER_LIST=$(ar t "$abs_a")
    MEMBER_COUNT=$(printf '%s\n' "$MEMBER_LIST" | grep -cv '^$' || true)

    # Extract members into a per-archive tmp dir; iterate objects to get
    # debug-symbol counts via `nm --debug-syms`. The flag exists on GNU binutils
    # and is the canonical filter for DWARF/stabs symbols.
    EXTRACT_DIR="$TMP_DIR/$(basename "$abs_a" .a)"
    mkdir -p "$EXTRACT_DIR"
    (cd "$EXTRACT_DIR" && ar x "$abs_a" 2>/dev/null || true)

    TOTAL_SYM=0
    DEBUG_SYM=0
    OBJ_COUNT=0
    while IFS= read -r -d '' obj; do
        [ -z "$obj" ] && continue
        OBJ_COUNT=$((OBJ_COUNT + 1))
        nm_total=$(nm --debug-syms "$obj" 2>/dev/null | wc -l | tr -d ' ')
        DEBUG_SYM=$((DEBUG_SYM + nm_total))
        # Total = debug-syms + everything-else (nm without --debug-syms).
        nm_full=$(nm "$obj" 2>/dev/null | wc -l | tr -d ' ')
        TOTAL_SYM=$((TOTAL_SYM + nm_full))
    done < <(find "$EXTRACT_DIR" -type f -name '*.o' -print0 2>/dev/null)

    # Debug-share heuristic: ratio of `--debug-syms` output to full nm output.
    # This is approximate but stable across Linux/GCC/Clang builds.
    if [ "$TOTAL_SYM" -gt 0 ]; then
        DEBUG_SHARE=$(awk -v d="$DEBUG_SYM" -v t="$TOTAL_SYM" \
                      'BEGIN{printf "%.1f%%", (d/t)*100}')
    else
        DEBUG_SHARE="n/a"
    fi

    # Preset label = parent dir basename (works for build/chronon/<preset>/src/foo.a).
    PRESET_LABEL=$(basename "$(dirname "$abs_a")")

    printf '%-30s  %12d  %8d  %12s  %s\n' \
        "$PRESET_LABEL" "$SIZE_B" "$MEMBER_COUNT" "$DEBUG_SHARE" "$abs_a"

    # Track basenames across archives for cross-archive duplicate detection.
    printf '%s\n' "$MEMBER_LIST" >> "$ALL_BASENAMES_FILE"

    # Build JSON record. Paths are escape-sanitized for JSON (handle backslashes
    # and double quotes that would otherwise produce malformed JSON).
    local safe_path="${abs_a//\\/\\\\}"
    safe_path="${safe_path//\"/\\\"}"
    local safe_preset="${PRESET_LABEL//\\/\\\\}"
    safe_preset="${safe_preset//\"/\\\"}"
    JSON_RECORDS+=("$(printf '{"preset":"%s","path":"%s","size_bytes":%d,"member_count":%d,"object_count":%d,"debug_symbols":%d,"total_symbols":%d}' \
        "$safe_preset" "$safe_path" "$SIZE_B" "$MEMBER_COUNT" "$OBJ_COUNT" "$DEBUG_SYM" "$TOTAL_SYM")")
done

# ────────────────────────────────────────────────────────────────────────
# Cross-archive duplicate-object detection
# ────────────────────────────────────────────────────────────────────────
echo ""
echo "=== Apparent duplicate object basenames (across all archives) ==="
DUP_BASENAMES=$(sort "$ALL_BASENAMES_FILE" | uniq -c | awk '$1>1 {printf "  %4d × %s\n", $1, $2}' || true)
if [ -n "$DUP_BASENAMES" ]; then
    DUP_COUNT=$(printf '%s\n' "$DUP_BASENAMES" | wc -l | tr -d ' ')
    echo "  Found $DUP_COUNT basenames appearing more than once across the"
    echo "  audited archives:"
    printf '%s\n' "$DUP_BASENAMES" | head -20 | sed 's/^/    /'
    if [ "$(printf '%s\n' "$DUP_BASENAMES" | wc -l | tr -d ' ')" -gt 20 ]; then
        echo "    ... (showing first 20 of $DUP_COUNT)"
    fi
else
    echo "  None — every object basename appears in at most one archive."
fi

# ────────────────────────────────────────────────────────────────────────
# Debug-info share interpretation
# ────────────────────────────────────────────────────────────────────────
echo ""
echo "=== Debug-info share interpretation ==="
echo "  Debug-share is approximated as (nm --debug-syms count) / (nm full count)."
echo "  This script does NOT enforce a threshold (the script is audit-only)."
echo "  Expected pattern across presets:"
echo "    Release  → low (<30%)"
echo "    Debug/CI → high (>50%)"
echo "  If Release also shows high debug-share, investigate: the linker is"
echo "  carrying debug info into the Release archive."

# ────────────────────────────────────────────────────────────────────────
# Optional JSON output
# ────────────────────────────────────────────────────────────────────────
if [ -n "$JSON_OUT" ]; then
    {
        printf '{\n'
        printf '  "archives": [\n'
        first=1
        for r in "${JSON_RECORDS[@]}"; do
            if [ "$first" -eq 1 ]; then
                printf '    %s\n' "$r"
                first=0
            else
                printf '    ,\n    %s\n' "$r"
            fi
        done
        printf '  ],\n'
        printf '  "duplicate_basenames": [\n'
        first=1
        if [ -n "$DUP_BASENAMES" ]; then
            while IFS= read -r line; do
                # Format: "  NNNN × basename"
                count=$(printf '%s\n' "$line" | awk '{print $1}')
                name=$(printf '%s\n' "$line" | awk '{print $3}')
                local safe_name="${name//\\/\\\\}"
                safe_name="${safe_name//\"/\\\"}"
                if [ "$first" -eq 1 ]; then
                    printf '    {"basename":"%s","occurrences":%d}\n' "$safe_name" "$count"
                    first=0
                else
                    printf '    ,\n    {"basename":"%s","occurrences":%d}\n' "$safe_name" "$count"
                fi
            done <<< "$DUP_BASENAMES"
        fi
        printf '  ]\n'
        printf '}\n'
    } > "$JSON_OUT"
    echo ""
    echo "JSON summary written to $JSON_OUT"
fi

echo ""
echo "=== Audit complete (exit 0: audit-only, no thresholds enforced) ==="
exit 0
