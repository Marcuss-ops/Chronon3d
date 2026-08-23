#!/usr/bin/env bash
# ═════════════════════════════════════════════════════════════════════════════
# tools/check_public_targets.sh
#
# Public-target rule gate (SDK Product V1 — "only Chronon3D::SDK / Chronon3D::C").
#
# An external consumer project may link ONLY the two public targets:
#
#     Chronon3D::SDK      (the C++ SDK facade)
#     Chronon3D::C        (the C ABI shared library)
#
# Every other target is internal — the OBJECT/INTERFACE/STATIC libraries
# registered in cmake/Chronon3DRegistry.cmake (chronon3d_graph,
# chronon3d_pipeline, chronon3d_runtime, chronon3d_backend_*, chronon3d_sdk_impl,
# chronon3d_render_plan, …) plus the bare `chronon3d` umbrella and `chronon3d_c`
# (whose public alias is `Chronon3D::C`).  If an external project needs any of
# them directly, the public SDK facade is missing something — the fix is to
# extend the facade, NOT to tell the consumer to link an internal target.
#
# Enforcement is name-based and drift-free: within any link/dependency statement
# (`target_link_libraries`, `link_libraries`, `add_dependencies`), any bare
# `chronon3d…` token (the reserved SDK-internal prefix) or any `Chronon3D::<X>`
# token where X is not `SDK`/`C` is a violation.  No hard-coded list of internal
# target names, so a new internal target is automatically covered the moment it
# is created.  CMake comments (`#…`) and non-link statements (e.g. message()
# text that happens to mention a name) are intentionally ignored — this gate
# enforces the *link* contract, not prose.
#
# Scope: the on-disk external consumer projects (the directories that consume
# the *installed* package, never the source tree).  In-tree build files
# (src/, apps/, tests/*.cmake) legitimately reference internal targets and are
# deliberately NOT scanned.
#
# Wired into:
#   - CI:     .github/workflows/gates.yml (Gate 5 / architecture-check)
#   - SDK:    tools/verify_sdk_product.sh (check [14] "no internal CMake targets")
#
# Exit codes:
#   0 = every consumer links only Chronon3D::SDK / Chronon3D::C
#   1 = at least one consumer links an internal or non-public target
# ═════════════════════════════════════════════════════════════════════════════
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${REPO_ROOT:-$(cd "$HERE/.." && pwd)}"

# External consumer projects (relative to REPO_ROOT).  These are the only
# directories whose build files are checked — they are the "pretend we know
# nothing about internals" projects.
CONSUMER_DIRS=(
    sdk_consumers
    tests/install_consumer
    tests/package_consumer
    examples/text_export_consumer
    sdk_consumers/02_content_registry
    templates/basic
)

# awk scanner: tracks `target_link_libraries` / `link_libraries` /
# `add_dependencies` statements (including multi-line) and flags any internal
# target name inside them.
read -r -d '' AWK_PROG <<'AWK' || true
function count_char(s, c,   n, i) {
    n = 0
    for (i = 1; i <= length(s); i++) {
        if (substr(s, i, 1) == c) n++
    }
    return n
}

function scan(line) {
    rest = line
    while (match(rest, /chronon3d(_[A-Za-z0-9_]+)?/)) {
        printf "%s:%d: references internal target\n", file, FNR > "/dev/stderr"
        printf "    %s\n", orig > "/dev/stderr"
        violations++
        rest = substr(rest, RSTART + RLENGTH)
    }
    rest = line
    while (match(rest, /Chronon3D::[A-Za-z0-9_]+/)) {
        tok = substr(rest, RSTART, RLENGTH)
        suffix = substr(tok, index(tok, "::") + 2)
        if (suffix != "SDK" && suffix != "C") {
            printf "%s:%d: links non-public target %s\n", file, FNR, tok > "/dev/stderr"
            printf "    %s\n", orig > "/dev/stderr"
            violations++
        }
        rest = substr(rest, RSTART + RLENGTH)
    }
}

{
    orig = $0
    code = $0
    sub(/#.*/, "", code)
    if (in_stmt) scan(code)
    if (code ~ /(target_link_libraries|link_libraries|add_dependencies)[[:space:]]*\(/) {
        in_stmt = 1
        scan(code)
    }
    if (in_stmt) {
        depth += count_char(code, "(")
        depth -= count_char(code, ")")
        if (depth <= 0) { in_stmt = 0; depth = 0 }
    }
}

END { if (violations > 0) exit 1 }
AWK

FAILED=0

log() { printf '[check_public_targets] %s\n' "$*" >&2; }

for dir in "${CONSUMER_DIRS[@]}"; do
    abs_dir="$REPO_ROOT/$dir"
    if [[ ! -d "$abs_dir" ]]; then
        log "SKIP $dir (absent)"
        continue
    fi
    while IFS= read -r -d '' file; do
        rel="${file#"$REPO_ROOT"/}"
        if ! awk -v file="$rel" "$AWK_PROG" "$file"; then
            FAILED=1
        fi
    done < <(find "$abs_dir" -type f \
        \( -name 'CMakeLists.txt' -o -name '*.cmake' \) -print0)
done

if [[ "$FAILED" -ne 0 ]]; then
    log "FAIL: external projects link internal/non-public targets"
    log "allowed public targets: Chronon3D::SDK and Chronon3D::C"
    exit 1
fi

log "PASS: all consumer projects link only Chronon3D::SDK / Chronon3D::C"
exit 0
