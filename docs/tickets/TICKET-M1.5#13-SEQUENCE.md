# TICKET-M1.5#13-SEQUENCE — Refactor del registry preset testuali (4-step extract)

> **Status**: PLANNED → for the umbrella ticket — `M1.5#13` itself
> **Type**: refactor (cluster architecture / ANTI_DUPLICATION_RULES alignment)
> **Owner**: Agent 3
> **Created**: 2026-07-04
> **Freeze compliance (AGENTS.md v0.1 Cat-5)**: yes — pure structural
> refactor of an existing module with no new registry / preset / sampler /
> sampler / cache surface.  Step 1 (this commit) ships the **basic**
> category factory; steps 2/3/4 ship the remaining three.

---

## Goal

Dividere `src/registry/text_preset_registry.cpp` in una serie di factory
TU dedicate — una per categoria — mantenendo **invariata** la struttura
del registro centrale (single-source-of-truth) e introducendo un nuovo
accessor pubblico `std::span<const TextPresetDescriptor> builtin_text_presets()`
per view non-owning sui 22 descrittori built-in.

### Vincoli architetturali (hard)

1. **Registro centrale resta UNICO**: nessun nuovo registry/sampler/cache.
2. **Factory NON si registrano autoonomamente**: i factory TU
   (`text_preset_factories_*.cpp`) espongono solamente `std::vector<TextPresetDescriptor>`
   che il bridge `register_helpers_internal::register_text_preset_<category>(r)`
   consuma e inoltra a `r.register_preset(...)`.  L'umbrella
   `register_builtin_presets(r)` resta l'unico seed canonico.
3. **Companion accessor surface (M1.5#13)**: aggiungere
   `std::span<const TextPresetDescriptor> builtin_text_presets()`
   accanto a `const TextPresetRegistry& builtin_text_preset_registry()`
   (coesistenza, NON sostituzione).
4. **Zero expansion `include/chronon3d/`**: nessun nuovo header
   pubblico.  I factory TU e l'header degli helper condivisi vivono
   solo in `src/registry/`.

---

## Step sequence

| Step | Status     | Tema              | Factory file                                       | Branch commit su `main` |
| ---- | ---------- | ----------------- | -------------------------------------------------- | ----------------------- |
| 1/4  | **DONE**   | `basic` (Subtitle)| `text_preset_factories_basic.cpp`                  | (this commit)           |
| 2/4  | PLANNED    | `kinetic` (Reveal)| `text_preset_factories_kinetic.cpp`               | TBD                     |
| 3/4  | PLANNED    | `cinematic`       | `text_preset_factories_cinematic.cpp`             | TBD                     |
| 4/4  | PLANNED    | `social` (Emphasis)| `text_preset_factories_social.cpp`               | TBD                     |

> **Category-name mapping rationale** (per user clarifications):
> - `basic` ← category `Subtitle` (4 entries — minimal/yellow/glow/caption)
> - `kinetic` ← category `Reveal` (10 entries — fade/blur/slide/scale/…)
> - `cinematic` ← category `Cinematic` (4 entries — composition/camera/title/tilt)
> - `social` ← category `Emphasis` (4 entries — word-pop/scale-punch/color/gradient)

---

## Step 1/4 (DONE) — Subtitle/basic factory extraction

### Diff summary

- **+ `src/registry/text_preset_internal_helpers.hpp`** (new)
  - `chrono3d::registry::internal::make_presetc_template(preset_id)` —
    verbatim port from the anon-namespace helper in
    `text_preset_registry.cpp` (TEXT-RES-01 scaffold factory).
  - `chrono3d::registry::internal::wire_through_resolver(lb, id, spec)` —
    verbatim port from the anon-namespace helper (engine-side factory body).
  - Type aliases: `LayerBuilderT` / `SceneBuilderT` / `TextSpecT`.
  - NO install; reachable only from sibling internal TUs.

- **+ `src/registry/text_preset_factories_basic.cpp`** (new)
  - 4 Subtitle entry() functions (verbatim port).
  - 4 compose_* helpers (verbatim port, including `compose_minimal_white`
    no-motion path).
  - Public surface:
    `chrono3d::registry::register_helpers_internal::factory_basic::create_text_presets()`
    returns `std::vector<TextPresetDescriptor>` of length 4 in canonical
    Subtitle insertion order.

- **~ `src/registry/text_preset_registry.cpp`**
  - `#include "text_preset_internal_helpers.hpp"`
  - Deletion of anon-namespace helpers:
    - `make_presetc_template` (verbatim moved to internal header).
    - `wire_through_resolver` (forward-decl + body — verbatim moved).
  - Deletion of 4 Subtitle compose_* helpers (verbatim moved).
  - Deletion of 4 Subtitle entry() functions (verbatim moved).
  - Update of `register_text_preset_subtitle(r)` bridge:
    bridge consumes `factory_basic::create_text_presets()` and forwards
    each descriptor to `r.register_preset(std::move(d))`.
  - + `builtin_text_presets()` impl: span accessor delegating to
    `builtin_text_preset_registry().list()` (process-stable static
    vector; magic-static lifetime).

- **~ `include/chronon3d/registry/text_preset_registry.hpp`**
  - `#include <span>`
  - + `[[nodiscard]] std::span<const TextPresetDescriptor> builtin_text_presets();`
    declared alongside `builtin_text_preset_registry()` (COESISTENZA).

- **~ `src/registry/CMakeLists.txt`**
  - + `text_preset_factories_basic.cpp` source line in
    `chronon3d_registry` OBJECT library.

### Architectural invariants preserved

- ✅ **SINGLE registry**: `chrono3d::registry::builtin_text_preset_registry()`
  remains the single source of truth; `builtin_text_presets()` is a
  VIEW onto it (delegated `list()` call).
- ✅ **NO new registry / cache**: factory TU is descriptor-only; no
  auto-register, no internal map state.
- ✅ **NO new public header in include/chronon3d/**: the shared helpers
  header lives under `src/registry/` and is not installed.
- ✅ **NO content→core reverse-edge**: factory TU does NOT include any
  `content/text/text_*.hpp`; reaches canonical `::chronon3d::TextSpec`
  via `builder_params.hpp`.
- ✅ **Cluster B contract intact**: `wire_through_resolver` is still a
  thin delegate to `wire_preset_text_run_params`; lambda bodies in
  the factory TU route through the same single canonical pipeline.

### Validation gates (post-edit, this step)

- `bash tools/check_doc_sync.sh` → PASS (canonical docs unchanged; only
  canonical sources touched via CHANGELOG + new ticket file +
  FOLLOWUP_TICKETS P1 row).
- `bash tools/check_legacy_text_pipeline.sh` → PASS (no legacy text
  pipeline code path regressed).
- `bash tools/check_main_clean.sh` → PASS (canonical doc-sync state).
- `bash bash tools/wrap_push.sh origin main` → push lands at commit
  `…M1.5#13.1/4` (or whichever hash GATE-MNT-01 picks).
- Compile: `cmake --build build --target chronon3d_registry` PASS
  (the canary OBJECT library that links the factory TU + the modified
  registry TU together).

### Tests that MUST continue to pass

- `tests/registry/test_text_preset_descriptor.cpp` (22-built-in
  Sub-cases A1-A4 / B1-B2 / C1 / D1-D2) — verifies
  `builtin_text_preset_registry()` size and metadata.
- `tests/text/test_text_preset_visual.cpp` (singleton-mirror fixture)
  — verifies the canonical "default text preset registry" surface.
- `tests/visual/` golden fixtures for `subtitle_*` (the 4 fixture
  paths referenced verbatim by the basic factory) — no golden path
  rewrite expected; fixture filenames and content are unchanged.

---

## Step 2/4 (PLANNED) — Reveal/kinetic factory extraction

### Plan (forward-looking)
- Same architectural pattern as Step 1/4, applied to the 10 Reveal entries.
- Reveal constitutes the LARGEST category (10 entries); regex
  `TextCategory::Reveal` / `register_text_preset_reveal` for entry audit.
- The `compose_*_animator_compose` closures for Reveal are
  interleaved with non-Reveal closures in the current anon-namespace;
  extraction is by COHERENT-GROUP (not by spatial locality).  Use
  grep + manual review for safe extraction scope.

### Deliverables
- `+ src/registry/text_preset_factories_kinetic.cpp`  <!-- drift-allow: stale-ref -->
- `~ src/registry/text_preset_registry.cpp` (delete 10 Reveal
  entry() + 10 Reveal compose_* + update `register_text_preset_reveal`)
- `~ src/registry/CMakeLists.txt` (+ source line)

---

## Step 3/4 (PLANNED) — Cinematic factory extraction

### Plan (forward-looking)
- 4 Cinematic entries (preserved verbatim from PR `41cda40c` per
  `register_text_preset_cinematic` block).
- 1 of the 4 (`cinematic_text_camera`) has the unique 2-stage wiring
  body — preserved byte-identical.

### Deliverables
- `+ src/registry/text_preset_factories_cinematic.cpp`
- `~ src/registry/text_preset_registry.cpp` (delete 4 Cinematic
  entry() + 4 Cinematic compose_* + update `register_text_preset_cinematic`)
- `~ src/registry/CMakeLists.txt` (+ source line)

---

## Step 4/4 (PLANNED) — Emphasis/social factory extraction + umbrella rewiring

### Plan (forward-looking)
- 4 Emphasis entries (Stage 3).
- After this step, all 4 factory TUs are live.  The umbrella
  `register_builtin_presets(r)` switches from direct anon-namespace
  helper calls to factory-vector calls (one per category).  This is
  the ONLY step that touches the umbrella.

### Deliverables
- `+ src/registry/text_preset_factories_social.cpp`  <!-- drift-allow: stale-ref -->
- `~ src/registry/text_preset_registry.cpp` (delete 4 Emphasis
  entry() + 4 Emphasis compose_* + REWIRE `register_builtin_presets`
  umbrella to use factory aggregates)
- `~ src/registry/CMakeLists.txt` (+ source line)
- Optional: Final tightening — `builtin_text_presets()` refactor to
  aggregate factory outputs directly (skip `builtin_text_preset_registry().list()`
  delegation).  This is OPTIONAL and depends on whether the per-step
  delegation proved clunky in practice.

### Validation gates (post Step 4)
- 11/11 architectural gates + SDK install + CMake boundary regressions
  unchanged (Step 4 is the LAST refactor commit; no follow-up).
- Spot-check `text_preset_register_helpers.hpp` reachability from
  any sibling internal TU (no additional public surface expansion).

---

## Anti-pattern checks (per step)

- ❌ NO new registry / sampler / cache.
- ❌ NO new header in `include/chronon3d/` (helpers live in
  `src/registry/`).
- ❌ NO content→core reverse-edge.
- ❌ NO `register_preset` call from a factory TU.
- ❌ NO silent descriptor-shape change (factory TU is verbatim port,
  identical struct field-population order).

---

## Cross-references

- AGENTS.md v0.1 §Feature Freeze — freeze-compliant is preserved
  (Cat-5 refactor only; no new feature surface).
- `docs/ANTI_DUPLICATION_RULES.md` §registry/resolver — single
  registry contract remains canonical.
- `docs/CHANGELOG.md` — M1.5#13 entries 1/4 → 4/4 (reverse-chrono).
- `docs/FOLLOWUP_TICKETS.md` — umbrella row updated each step.
