#!/usr/bin/env bash
# tools/check_no_dual_text_api.sh
# ─────────────────────────────────────────────────────────────────────
# M1.8 §1 — Anti-duplication gate for NEW parallel text APIs.
#
# Permanent forward-only invariant (per AGENTS.md §Anti-duplication + §Feature
# Freeze permanent rules): no NEW helper, no NEW builder-parallel variant, no
# NEW positioning system.  This gate enforces the §1 rule of the canonical
# Text Simplicity plan (`docs/TEXT_SIMPLICITY_ACTION_PLAN.md §F1.1`) by
# scanning the active source surface for 4 categories of regressions:
#
#   [1/4] LayerBuilder::text_<variant>              (canonical: text + text_run only)
#   [2/4] centered_text / glow_text DEFINITION       (canonical: src/presets/ + include/
#                                                      chronon3d/presets/ registry only)
#   [3/4] TextSpec.position non-migrated ASSIGNMENT  (canonical: TextFrame::place(.offset))
#   [4/4] pin_to + TextAnchor co-occurrence in text layers (advisory; ADR-019
#                                                       coordinate-level confusion)
#
# All checks scan `include/`, `src/`, `tests/`, `content/`, `apps/`.  `docs/`
# is intentionally NOT searched — historical references are part of the
# audit trail.
#
# No path whitelists remain for [3/4]: pre-existing usages in `content/`
# are now treated the same as any other path.  This gate is a forward-only
# blast barrier: any introduction of these patterns is FAIL.
#
# EXIT CODES:
#   0 = no NEW violations detected (PASS)
#   1 = at least one violation detected (FAIL)
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

REPO_ROOT="${BOUNDARY_CHECK_ROOT:-$(git rev-parse --show-toplevel 2>/dev/null || pwd)}"
cd "$REPO_ROOT" || { echo "INTERNAL_ERROR: cannot cd to $REPO_ROOT" >&2; exit 2; }

# M1.8 §5A / TICKET-SIMPLICITY-DEPRECATION — collect ALL violations into
# a single bash array so the user gets a complete report at the end (vs
# stopping at the first failure).  The gate's exit code is determined by
# the array size: if `${#VIOLATIONS[@]} -ne 0`, exit 1.  This is the
# "vector size NEQ 0 in master" contract per the user spec for the §5A
# atomic commit.
VIOLATIONS=()
SCAN_PATHS='src include content apps tests'
FAILED=0

# ── Comment-strip filter ───────────────────────────────────────────────
# Same pattern as check_legacy_text_pipeline.sh — drops pure-comment lines
# AND trailing `//` comment text.  Reads grep -Rn format
# <path>:<line>:<content> from stdin; emits non-comment lines only.

filter_code_only() {
    local sym="$1"
    awk -F: -v sym="$sym" '
        {
            rest = ""
            for (i = 3; i <= NF; i++) {
                rest = rest (i == 3 ? "" : ":") $i
            }
            if (rest ~ /^[[:space:]]*(\/\/\/|\/\/|\/\*|\*)/) next
            pre = rest
            sub(/\/\/.*/, "", pre)
            if (pre !~ sym) next
            print
        }
    '
}

# ═══════════════════════════════════════════════════════════════════════
echo "=== No-Dual-Text-API Gate (M1.8 §1 — forward-only anti-duplication) ==="

# ── [1/4] LayerBuilder::text_* method variants ────────────────────────
# Canonical methods (F2.B / F2.C) are `text` and `text_run`.  Any other
# `text_<X>` would re-introduce a parallel builder method — AGENTS.md
# "non duplicare builder/resolver" violation.
#
# Detection: grep `\bLayerBuilder::text_[a-zA-Z_]+\b`, then drop the
# canonical `text_run` (PCRE-free POSIX alternative to lookahead).

echo -n "  [1/4] LayerBuilder::text_* (canonical: text + text_run only) ... "
raw=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    '\bLayerBuilder::text_[a-zA-Z_]+\b' $SCAN_PATHS 2>/dev/null || true)
hits=""
if [ -n "$raw" ]; then
    # Drop canonical text_run, then dedup by suffix.
    hits=$(echo "$raw" \
        | grep -v 'LayerBuilder::text_run' \
        | grep -E -o 'LayerBuilder::text_[a-zA-Z_]+' \
        | sort -u || true)
fi
if [ -n "$hits" ]; then
    echo "FAIL"
    echo "  NEW unauthorized LayerBuilder::text_<variant> method(s):"
    echo "$hits" | sed 's/^/    /'
    echo "  → Consolidate into LayerBuilder::text() / LayerBuilder::text_run()"
    echo "    (canonical F2.B / F2.C).  Adding a new text_* variant conflicts"
    echo "    with AGENTS.md §Anti-duplication + M1.8 §1 'no new builder parallel'."
    VIOLATIONS+=("[1/4] LayerBuilder::text_* variant: $(echo "$hits" | tr '\n' ' ')")
    FAILED=1
else
    echo "PASS"
fi

# ── [2/4] centered_text / glow_text DEFINITION outside canonical scope ─
# `centered_text` and `glow_text` are pre-existing canonical preset macros
# exported from the preset registry
# (`src/scene/presets/builtin_text_presets.cpp` or
# `include/chronon3d/presets/*`).  Definitions anywhere else would
# re-introduce parallel presets — AGENTS.md violation.
#
# Detection: look for function/class/macro DEFINITION-shape (`^[ws]*(...)?X(`,
# `#define X`, `class X`, `auto X(`) outside the canonical preset paths.

echo -n "  [2/4] centered_text / glow_text definitions in canonical scope only ... "
# Match DEFINITION-shaped patterns with 3 precise branches:
#   1. Macro definition: `#define centered_text(` OR `#define centered_text `
#      (optional whitespace before `(`)
#   2. Class/struct declaration: `class centered_text` / `struct glow_text`
#   3. C++ function definition: explicit start token (modifier keyword +
#      return type + name + `(`) — the start-token whitelist prevents the
#      false-positive `return centered_text()` pattern (where `return` was
#      previously treated as a generic identifier by the old regex).
def_pat='(^[[:space:]]*#[[:space:]]*define[[:space:]]+(centered_text|glow_text)([[:space:]]|\()|(^[[:space:]]*(class|struct)[[:space:]]+(centered_text|glow_text)\b)|(^[[:space:]]*(template[[:space:]]*<[^>]*>[[:space:]]*)?(static|inline|const|void|int|bool|auto|Layer|std::string)[a-zA-Z0-9_:[:space:]*&]*\b(centered_text|glow_text)[[:space:]]*\()'
raw=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' -E "$def_pat" $SCAN_PATHS 2>/dev/null || true)
hits=""
if [ -n "$raw" ]; then
    code_only=$(echo "$raw" | filter_code_only '(centered_text|glow_text)' || true)
    # Path whitelist: only canonical preset registry.
    hits=$(echo "$code_only" \
        | grep -Ev '(src/scene/presets/|src/presets/|include/chronon3d/presets/)' \
        || true)
fi
if [ -n "$hits" ]; then
    echo "FAIL"
    echo "  NEW centered_text/glow_text definition(s) outside canonical preset registry:"
    echo "$hits" | sed 's/^/    /'
    echo "  → Move definitions to the canonical preset registry"
    echo "    (src/scene/presets/ or include/chronon3d/presets/).  A second"
    echo "    declaration path violates AGENTS.md §Anti-duplication + M1.8 §1"
    echo "    'no new helper parallel' rule."
    VIOLATIONS+=("[2/4] centered_text/glow_text outside canonical: $(echo "$hits" | tr '\n' ' ')")
    FAILED=1
else
    echo "PASS"
fi

# ── [3/4] TextSpec.position non-migrated ASSIGNMENT ───────────────────
# `TextSpec.position` is the legacy ambiguous-pos semantic discouraged by
# ADR-019 Decision 3.  Migration target: `.place(TextPlacement::CanvasCenter)`
# + `.offset(...)` on `TextFrame`.  New raw assignments of
# `TextSpec::position` indicate the migration wasn't applied.
#
# Detection: filter `.position\s*[={]` on lines that ALSO reference
# `TextSpec` (limiting false positives from .position field on Layer,
# Shape, etc.).  Path scope excludes `include/chronon3d/scene/builders/`
# (the header that DEFINES the field is allowed to declare it as a
# default-initialized field; assignments happen in callers).

echo -n "  [3/4] TextSpec.position non-migrated assignment ... "
hits=""
while IFS= read -r f; do
    [ -z "$f" ] && continue
    case "$f" in
        include/chronon3d/scene/builders/builder_params.hpp) continue ;;
    esac
    # Match ONLY TextSpec-qualified `.position` assignments. Other `.position`
    # fields (RenderNode.world_transform.position, Layer.transform.position,
    # Camera.position, MotionState.position, etc.) are NOT TextSpec-derived
    # and don't trigger the gate. Real TextSpec.position sites emit patterns
    # like `spec.position = X`, `text.position = X`, `p.text.position = X`,
    # or brace-init `TextSpec{.position = X}` — the regex requires a var-name
    # qualifier before `.position` (whitelisted TextSpec var names: spec, text,
    # TextSpec, text_def, ts, myText, def, textSpec, t_spec, td, p, run_params)
    # with optional `[{(]` between var-name and `.position` to handle brace-init
    # and copy-constructor syntax. Gate TIGHTENING (precision improvement) per
    # TICKET-CHECK-NO-DUAL-TEXT-API-REGEX-IMPROVEMENT.md.
    # grep -P (PCRE): native support for `|` alternation, `\b` word-boundary,
    # `(?=)` lookahead, `\s` newline-aware — no need for -E.
    file_hits=$(grep -Pon '\b(spec|text|TextSpec|text_def|ts|myText|def|textSpec|t_spec|td|p|run_params)\s*[{(]?\s*\.position(?=\s*[={])' "$f" 2>/dev/null || true)
    if [ -n "$file_hits" ]; then
        hits="${hits}$(echo "$file_hits" | sed "s|^|$f:|")"$'\n'
    fi
done < <(grep -Rl --include='*.hpp' --include='*.cpp' --include='*.h' \
         '\bTextSpec\b' $SCAN_PATHS 2>/dev/null || true)
hits=$(echo "$hits" | grep -v '^$' || true)
if [ -n "$hits" ]; then
    echo "FAIL"
    echo "  NEW TextSpec.position raw assignment(s) outside migration target scope:"
    echo "$hits" | sed 's/^/    /'
    echo "  → Migrate to TextFrame::place(TextPlacement::CanvasCenter).offset(...)"
    echo "    or TextFrame::position explicitly with intent cross-link in"
    echo "     ADR-019 §3.  Direct TextSpec.position assignment violates F2.A"
    echo "    canonical DTO mandate + M1.8 §6 'no API deprecate in compositions'."
    VIOLATIONS+=("[3/4] TextSpec.position non-migrated: $(echo "$hits" | tr '\n' ' ')")
    FAILED=1
else
    echo "PASS"
fi

# ── [4/4] pin_to + TextAnchor co-occurrence (ADVISORY) ─────────────────
# ADR-019 Decision 3: `pin_to(Anchor)` operates on LAYER coordinates; the
# canvas-relative placement is the role of `TextPlacement`.  Mixing
# `pin_to(...)` with `TextAnchor::` on a layer that's also a text layer
# (.text(...)) indicates a coordinate-level confusion that may produce
# the predicted_bbox vs world_ink_bbox divergence (TICKET-TEXT-CLIP-PREDICTED-BBOX).
#
# ADVISORY (not blocking): the exact cross-level confusion requires
# human review of the call site; the gate only flags co-occurrence for
# follow-up investigation.

echo -n "  [4/4] pin_to + TextAnchor co-occurrence (advisory)            ... "
raw=$(grep -Rl --include='*.hpp' --include='*.cpp' --include='*.h' \
    '\bpin_to\(' $SCAN_PATHS 2>/dev/null || true)
suspect_files=""
if [ -n "$raw" ]; then
    while IFS= read -r f; do
        [ -z "$f" ] && continue
        if grep -lqE '\bTextAnchor::' "$f" 2>/dev/null \
           && grep -lqE '\.text\(' "$f" 2>/dev/null; then
            suspect_files="${suspect_files}${f}"$'\n'
        fi
    done <<< "$raw"
fi
suspect_files=$(echo "$suspect_files" | grep -v '^$' | sort -u || true)
if [ -n "$suspect_files" ]; then
    # M1.8 §5A / TICKET-SIMPLICITY-DEPRECATION — promote [4/4] from
    # ADVISORY to BLOCKING per the §5A migration step 4.  Any file
    # with `pin_to()` + `TextAnchor::` + `.text()` co-occurrence is
    # recorded as a violation; the consolidated report at the end
    # of the script decides the final exit code.
    echo "FAIL (M1.8 §5A — promoted from ADVISORY to BLOCKING)"
    echo "  Files with pin_to + TextAnchor + .text() co-occurrence:"
    echo "$suspect_files" | sed 's/^/    /'
    echo "  → MIGRATE to TextFrame::place(TextPlacement::*) chain on the"
    echo "    TextDefinition DTO.  pin_to(Anchor) operates on Layer coords;"
    echo "    TextAnchor + .text() co-occurrence indicates Canvas/Layer/Box"
    echo "    coordinate confusion per ADR-019 Decision 3.  See §5A banner"
    echo "    in include/chronon3d/scene/builders/layer_builder.hpp."
    VIOLATIONS+=("[4/4] pin_to+TextAnchor+.text() co-occurrence: $(echo "$suspect_files" | tr '\n' ' ')")
    FAILED=1
else
    echo "PASS"
fi

# ═══════════════════════════════════════════════════════════════════════
echo ""
# M1.8 §5A — print the consolidated violation list (the "vector" of
# findings from the 4 check categories).  In master branch, the vector
# size MUST be 0 (NEQ 0 = GATE_FAIL per the user spec for the §5A
# atomic commit).  In feature branches, the vector can be NEQ 0
# (informational; the gate still fails per the pre-§5A FAILED counter).
if [ "${#VIOLATIONS[@]}" -ne 0 ]; then
    echo "=== No-Dual-Text-API gate FAILED ==="
    echo "  Vector of violations: ${#VIOLATIONS[@]} entries"
    echo "  Per-§5A contract: vector size MUST be 0 in master; NEQ 0 = FAIL."
    echo ""
    for v in "${VIOLATIONS[@]}"; do
        echo "  - $v"
    done
    exit 1
fi
if [ "$FAILED" -ne 0 ]; then
    echo "=== No-Dual-Text-API gate FAILED ==="
    exit 1
fi
echo "=== No-Dual-Text-API gate PASSED ==="
echo "  Vector of violations: 0 entries (per-§5A contract satisfied)"
exit 0
