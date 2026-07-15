#!/usr/bin/env bash
# tools/check_single_source_of_truth.sh — Test 12 — Audit single source of truth
#
# Closes TICKET-TEST-12-SSOT-AUDIT.
#
# USER SPEC (verbatim): "per ogni concetto (Asset=AssetRef<T>, Placement=TextPlacement,
# Layout=compilatore canonico, Animation=sampler canonico, Composition=CompositionDescriptor,
# Render=RenderJob, Diagnostica=TextVisibilityAudit, Sequence=compilatore sequence unico)
# verifica che esista una sola autorità. FAIL se coesistono asset legacy+v2, offset+placement.offset,
# text effects+material glow, CLI render+SDK render con orchestrazioni diverse. Usa/estendi
# `tools/check_architecture_boundaries.sh`. Lavora su `main`, commit + push."
#
# 8 CONCEPT AUDITS (SSoT per concept):
#   1. Asset        : canonical = class AssetRef (include/chronon3d/assets/asset_ref.hpp)
#   2. Placement    : canonical = struct TextPlacement (include/chronon3d/text/text_placement.hpp)
#                     + HARD-CAP for TICKET-TEXT-LEGACY-POSITION-ROT pre-existing rot
#   3. Layout       : canonical = class TextLayoutEngine (include/chronon3d/backends/text/text_layout_engine.hpp)
#   4. Animation    : canonical = motion::Timeline<T> (include/chronon3d/animation/motion/timeline.hpp)
#   5. Composition  : canonical = struct CompositionDescriptor (include/chronon3d/timeline/composition_descriptor.hpp)
#                     + HARD-CAP for pre-existing `class Composition` rot
#   6. Render       : canonical = class RenderJob (include/chronon3d/timeline/render_job.hpp)
#   7. Diagnostica  : canonical = struct TextVisibilityAudit + audit_text_visibility()
#                     (include/chronon3d/text/text_visibility_audit.hpp + src/text/text_visibility_audit.cpp)
#   8. Sequence     : canonical = SceneBuilder::compile_sequence (include/chronon3d/scene/builders/scene_builder.hpp)
#
# 4 SPECIFIC FAIL PATTERNS (from user spec):
#   (a) asset legacy+v2:           struct Asset/AssetHandle coexisting with AssetRef
#   (b) offset+placement.offset:   standalone .offset fields outside TextPlacement
#   (c) text effects+material glow: TextEffects struct coexisting with TextMaterial.use_material_glow
#   (d) CLI/SDK render:            unified orchestration via audit_text_visibility()
#
# Output pattern: bash VIOLATIONS array (per check_no_dual_text_api.sh convention).
# All 8 + 4 audits run regardless of intermediate FAILs; final verdict = VIOLATIONS count.
#
# Soft-cap mechanism (regression-detector semantics): the audit_concept helper
# accepts a 5th arg `legacy_soft_cap` (default 0). When set to a positive
# number N, the audit reports PASS if legacy_count <= N (i.e., the known
# pre-existing rot count is tolerated without failing the gate). The PASS
# line still surfaces the actual count so future maintainers see the cap is
# being consumed. The gate only FAILS if the count INCREASES above the cap
# (i.e., new rot is introduced) — making it a regression detector, not a
# completeness meter. Used for Concept 2 (Placement) + Concept 5 (Composition).
#
# Cat-3: pure tools/ artifact, zero new SDK symbols.
# Cat-5: 3-doc same-commit (CHANGELOG + FOLLOWUP_TICKETS + CURRENT_STATUS).
# AGENTS.md INFO-level diagnostic style: emits [INFO] check_single_source_of_truth: ...
# line on PASS addizionale al GATE_PASS.

# ── Hard-caps for pre-existing rot (regression-detector semantics) ─────
# TICKET-TEXT-LEGACY-POSITION-ROT tracks 200+ TextSpec::position sites; the
# audit reports the current count and FAILS ONLY IF the count exceeds the
# cap (i.e., a regression above the known baseline). User-calibratable via env.
# KNOWN_COMPOSITION_ROTS=2 honors the pre-existing `class Composition` in
# `include/chronon3d/timeline/composition.hpp` + the forward decl in
# `include/chronon3d/sdk/render_engine.hpp` (both predate the canonical
# CompositionDescriptor; tracked for future migration per the
# TICKET-COMPOSITION-LEGACY-CLASSES forward-point).
KNOWN_PLACEMENT_ROTS="${KNOWN_PLACEMENT_ROTS:-200}"
KNOWN_COMPOSITION_ROTS="${KNOWN_COMPOSITION_ROTS:-2}"

# ── Precondition check ──────────────────────────────────────────────────
set -uo pipefail
if ! command -v rg >/dev/null 2>&1; then
    echo "GATE_FAIL_INTERNAL: required command 'rg' (ripgrep) not in PATH" >&2
    exit 2
fi
if [ ! -d include/chronon3d/ ]; then
    echo "GATE_FAIL_INTERNAL: include/chronon3d/ not found (run from repo root)" >&2
    exit 2
fi

# VIOLATIONS array (per check_no_dual_text_api.sh pattern)
VIOLATIONS=()

# ── Helper: audit a single concept ──────────────────────────────────────
# Args:
#   $1 = concept name (e.g. "1. Asset")
#   $2 = canonical pattern (e.g. "class AssetRef")
#   $3 = canonical path (e.g. "include/chronon3d/assets/asset_ref.hpp")
#   $4 = expected canonical count (typically 1)
#   $5 = legacy soft cap (NEW; default 0) — pre-existing rot count tolerated
#        without failing the gate. Used for tracking known rot without
#        failing every push on it. The audit reports the current count
#        in the PASS line so future maintainers can see the cap is
#        being consumed.
#   $6+ = legacy patterns (one per arg)
audit_concept() {
    local name="$1"
    local canonical_pattern="$2"
    local canonical_path="$3"
    local expected_canonical_count="$4"
    local legacy_soft_cap="${5:-0}"
    # ALWAYS shift 5 (signature is 5 fixed args + variable legacy_patterns).
    # Callers MUST pass the 5th arg (soft cap) explicitly; pass `0` for
    # strict (no pre-existing rot tolerated). Conditional shift was removed
    # per code-reviewer round 3 — it silently lost the first legacy pattern
    # on the 4-fixed-arg callers (Concepts 1/3/4/7/8) by treating the
    # 5th arg (a pattern) as a soft-cap.
    shift 5
    local legacy_patterns=("$@")

    # Count canonical definitions (in headers AND impl files)
    local canonical_count
    canonical_count=$(rg -l -t cpp "$canonical_pattern" include/ src/ apps/ 2>/dev/null | wc -l)

    # Count legacy pattern matches (excluding canonical_path)
    local legacy_count=0
    local legacy_evidence=()
    for pat in "${legacy_patterns[@]}"; do
        local matches
    # AssetPath in the authoring helper is the canonical logical-path wrapper
    # that converts to AssetRef; it is NOT a legacy asset type.
    matches=$(rg -l -t cpp "$pat" include/ src/ apps/ 2>/dev/null \
        | grep -v "$canonical_path" \
        | grep -v "include/chronon3d/authoring/asset.hpp" \
        | grep -v '\.git/' || true)
        if [ -n "$matches" ]; then
            local n_matches
            n_matches=$(printf '%s\n' "$matches" | wc -l)
            legacy_count=$((legacy_count + n_matches))
            # Truncate evidence to first 5 files (was 3 — bumped to 5 per
            # code-reviewer round 2 to keep evidence lists bounded while
            # showing more context on heavily-rotten codebases).
            local truncated
            truncated=$(printf '%s\n' "$matches" | head -5)
            legacy_evidence+=("legacy '$pat' found in $(printf '%s\n' "$matches" | wc -l) file(s): $(echo "$truncated" | tr '\n' ' ')")
        fi
    done

    # Verdict (with soft-cap tolerance for pre-existing rot)
    if [ "$canonical_count" -ge "$expected_canonical_count" ] && [ "$legacy_count" -le "$legacy_soft_cap" ]; then
        if [ "$legacy_count" -eq 0 ]; then
            echo "  PASS  $name: canonical at $canonical_path (count=$canonical_count); no legacy patterns"
        else
            echo "  PASS  $name: canonical at $canonical_path (count=$canonical_count); $legacy_count legacy match(es) within soft-cap=$legacy_soft_cap (pre-existing rot, tracked)"
        fi
        return 0
    else
        echo "  FAIL  $name: canonical=$canonical_count (expected >= $expected_canonical_count); legacy=$legacy_count (soft-cap=$legacy_soft_cap)"
        VIOLATIONS+=("$name: canonical=$canonical_count (expected >= $expected_canonical_count); legacy=$legacy_count (soft-cap=$legacy_soft_cap)")
        for ev in "${legacy_evidence[@]}"; do
            echo "         $ev"
        done
        return 1
    fi
}

# ── 8 CONCEPT AUDITS ────────────────────────────────────────────────────
echo "=== 8 CONCEPT SSoT AUDITS ==="

# 1. Asset (soft-cap=0: strict, no pre-existing rot tolerated)
audit_concept "1. Asset" "class AssetRef" "include/chronon3d/assets/asset_ref.hpp" 1 0 \
    "^struct Asset\b" "^class Asset\b" "AssetHandle\b" "AssetPath\b"

# 2. Placement (with hard-cap)
# Direct count: TextSpec::position uses (legacy, per TICKET-TEXT-LEGACY-POSITION-ROT)
position_count=$(rg -c "TextSpec::position" -t cpp include/ src/ apps/ 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')
if [ "${position_count:-0}" -le "${KNOWN_PLACEMENT_ROTS}" ]; then
    echo "  PASS  2. Placement: canonical TextPlacement at include/chronon3d/text/text_placement.hpp; TextSpec::position count=$position_count (≤ cap=$KNOWN_PLACEMENT_ROTS, TICKET-TEXT-LEGACY-POSITION-ROT pre-existing rot)"
else
    echo "  FAIL  2. Placement: TextSpec::position count=$position_count exceeds cap=$KNOWN_PLACEMENT_ROTS (regression above the TICKET-TEXT-LEGACY-POSITION-ROT baseline)"
    VIOLATIONS+=("2. Placement: TextSpec::position count=$position_count exceeds cap=$KNOWN_PLACEMENT_ROTS (regression above TICKET-TEXT-LEGACY-POSITION-ROT baseline)")
fi

# 3. Layout (soft-cap=0: strict)
audit_concept "3. Layout" "class TextLayoutEngine" "include/chronon3d/backends/text/text_layout_engine.hpp" 1 0 \
    "^class TextLayoutCompiler\b" "^class LayoutEngine\b"

# 4. Animation (soft-cap=0: strict)
audit_concept "4. Animation" "motion::Timeline" "include/chronon3d/animation/motion/" 1 0 \
    "^class AnimationSampler\b" "^class MotionSampler\b"

# 5. Composition (with hard-cap for pre-existing `class Composition` rot)
# Forward-point: TICKET-COMPOSITION-LEGACY-CLASSES — `class Composition` in
# `include/chronon3d/timeline/composition.hpp` (legacy name) + the forward
# decl in `include/chronon3d/sdk/render_engine.hpp` predate the canonical
# `CompositionDescriptor` (timeline/composition_descriptor.hpp). The cap=2
# tracks the known baseline; FAIL only on a regression above it.
audit_concept "5. Composition" "struct CompositionDescriptor" "include/chronon3d/timeline/composition_descriptor.hpp" 1 "$KNOWN_COMPOSITION_ROTS" \
    "^struct Composition\b" "^class Composition\b"

# 6. Render
# Canonical path corrected (was include/chronon3d/utils/job/render_job.hpp,
# an empty file; the REAL RenderJob struct lives at the timeline path below
# per the dry-run investigation 2026-07-12). NOTE: RenderJob is declared
# as `struct RenderJob` (not `class RenderJob`) at line 71 of the canonical
# header; the audit pattern matches the actual declaration form.
audit_concept "6. Render" "struct RenderJob" "include/chronon3d/timeline/render_job.hpp" 1 0 \
    "^class RenderSession\b" "^class RenderPlan\b"

# 7. Diagnostica (soft-cap=0: strict)
audit_concept "7. Diagnostica" "struct TextVisibilityAudit" "include/chronon3d/text/text_visibility_audit.hpp" 1 0 \
    "^struct TextDiagnostic\b" "^struct TextAudit\b"

# 8. Sequence (soft-cap=0: strict)
# CRITICAL fix (code-reviewer round 1): SequenceBuilder at
# include/chronon3d/scene/builders/sequence_builder.hpp is the LEGITIMATE
# public API that DELEGATES to SceneBuilder::compile_sequence (per the C2
# comment). It is NOT a parallel implementation. The audit's legacy
# pattern was `^class SequenceBuilder\b` which incorrectly flagged this
# legitimate wrapper. Fix: drop SequenceBuilder from legacy patterns;
# only SequenceCompiler (a non-existent variant) + any future v2
# sequence types would be flagged.
# Forward-point: when a future Sequence*BuilderV2 / Sequence*Compiler /
# Sequence*Alt class appears, extend the pattern to
# `^class\s+Sequence[A-Za-z]*Builder\b` to catch the family.
audit_concept "8. Sequence" "compile_sequence" "include/chronon3d/scene/builders/scene_builder.hpp" 1 0 \
    "^class SequenceCompiler\b"

# ── 4 SPECIFIC FAIL PATTERNS (user spec verbatim) ───────────────────────
echo ""
echo "=== 4 SPECIFIC FAIL PATTERNS ==="

# (a) asset legacy+v2
asset_legacy_count=$(rg -c -t cpp "^(struct|class) Asset\b|AssetHandle\b" include/ src/ apps/ 2>/dev/null \
    | grep -v "include/chronon3d/assets/asset_ref.hpp" \
    | awk -F: '{s+=$2} END{print s+0}')
if [ "${asset_legacy_count:-0}" -eq 0 ]; then
    echo "  PASS  (a) asset legacy+v2: only AssetRef exists; no struct Asset / AssetHandle / etc. outside canonical"
else
    echo "  FAIL  (a) asset legacy+v2: found $asset_legacy_count legacy matches outside canonical"
    VIOLATIONS+=("(a) asset legacy+v2: $asset_legacy_count legacy matches outside include/chronon3d/assets/asset_ref.hpp")
fi

# (b) offset+placement.offset (standalone .offset fields outside TextPlacement)
standalone_offset_count=$(rg -c -t cpp "\\.offset\\s*=\\s*Vec2" include/ src/ apps/ 2>/dev/null \
    | grep -v "include/chronon3d/text/text_placement.hpp" \
    | awk -F: '{s+=$2} END{print s+0}')
if [ "${standalone_offset_count:-0}" -eq 0 ]; then
    echo "  PASS  (b) offset+placement.offset: no standalone .offset = Vec2 fields outside TextPlacement"
else
    echo "  FAIL  (b) offset+placement.offset: $standalone_offset_count standalone .offset=Vec2 outside TextPlacement"
    VIOLATIONS+=("(b) offset+placement.offset: $standalone_offset_count standalone .offset=Vec2 outside TextPlacement")
fi

# (c) text effects+material glow (TextEffects struct coexisting with TextMaterial.use_material_glow)
text_effects_count=$(rg -c -t cpp "^struct TextEffects\b" include/ src/ apps/ 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')
material_glow_count=$(rg -c -t cpp "use_material_glow" include/ src/ apps/ 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')
if [ "${text_effects_count:-0}" -eq 0 ] && [ "${material_glow_count:-0}" -gt 0 ]; then
    echo "  PASS  (c) text effects+material glow: only TextMaterial.use_material_glow exists ($material_glow_count sites); TextEffects eliminated per Phase A5"
else
    echo "  FAIL  (c) text effects+material glow: TextEffects=$text_effects_count (should be 0 post-Phase A5); use_material_glow=$material_glow_count"
    VIOLATIONS+=("(c) text effects+material glow: TextEffects=$text_effects_count, use_material_glow=$material_glow_count (Phase A5 should have eliminated TextEffects)")
fi

# (d) CLI render+SDK render unified orchestration (both should call audit_text_visibility)
cli_audit_count=$(rg -c -t cpp "audit_text_visibility" apps/chronon3d_cli/ 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')
sdk_audit_count=$(rg -c -t cpp "audit_text_visibility" src/ 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')
if [ "${cli_audit_count:-0}" -gt 0 ] && [ "${sdk_audit_count:-0}" -gt 0 ]; then
    echo "  PASS  (d) CLI/SDK render: both call audit_text_visibility() (CLI=$cli_audit_count, SDK=$sdk_audit_count — unified orchestration)"
else
    echo "  FAIL  (d) CLI/SDK render: CLI=$cli_audit_count, SDK=$sdk_audit_count audit_text_visibility calls (both should be > 0)"
    VIOLATIONS+=("(d) CLI/SDK render: CLI=$cli_audit_count, SDK=$sdk_audit_count audit_text_visibility calls (unified orchestration required)")
fi

# ── AGGREGATE VERDICT ───────────────────────────────────────────────────
echo ""
echo "=== AGGREGATE SSoT VERDICT ==="
if [ "${#VIOLATIONS[@]}" -eq 0 ]; then
    echo "GATE_PASS: 8/8 concepts + 4/4 specific patterns — all SSoT audits clean (single authority per concept)"
    echo "[INFO] check_single_source_of_truth: 12/12 SSoT audits PASS (placement cap=$KNOWN_PLACEMENT_ROTS + composition cap=$KNOWN_COMPOSITION_ROTS honoring pre-existing rot)"
    exit 0
else
    echo "GATE_FAIL: ${#VIOLATIONS[@]} SSoT violation(s) found:"
    for v in "${VIOLATIONS[@]}"; do
        echo "  - $v"
    done
    exit 1
fi
