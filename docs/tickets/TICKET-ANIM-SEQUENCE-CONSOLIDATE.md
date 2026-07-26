# TICKET-ANIM-SEQUENCE-CONSOLIDATE — Sequence surface consolidation

## Stato: OPEN — Phase 1 deprecation DONE (2026-07-26, this session); Phase 2 adapter conversion pending

## Problema
La surface Chronon3D esponeva DUE percorsi produttivi sovrapposti per la
sequence (audit Remotion-like §3):
- `SequenceBuilder + FrameContext::local_time()` — canonical produttivo
  (usato da `SceneBuilder::sequence(...)` → `compile_sequence(...)` via
  `include/chronon3d/scene/builders/detail/scene_builder_sequences.inl:42`).
- `SequenceContext` — adapter non-builder + factory
  `chronon3d::sequence(ctx, from, duration)` in
  `include/chronon3d/timeline/sequence.hpp:36`, sopravvissuto come
  math-context struct con campi `Frame from/duration/frame/active` e
  metodi `progress()/held_progress()/seconds()`.

Audit Remotion-like (§3) classificava `SequenceContext` come
"PASS funzionale, API da consolidare".  Soluzione proposta: rendere
`SequenceBuilder` unico percorso produttivo, trasformare `SequenceContext`
in thin adapter oppure deprecarlo gradualmente.

## Soluzione Phase 1 (questo chore) — DONE

Marcatura `[[deprecated("...")]]` su due entry points, con commento
inline che cita questo ticket:

1. `inline SequenceContext sequence(const FrameContext& ctx, Frame from, Frame duration)`
   in `include/chronon3d/timeline/sequence.hpp:36` — factory adapter.
2. `inline f32 spring(const SequenceContext& ctx, f32 from, f32 to, const SpringConfig& config = {})`
   in `include/chronon3d/animation/easing/spring.hpp:113` — overload
   sequence-context-aware.

`[[deprecated]]` markers sono function-level (NON type-level), per
rispettare la repo convention (vedi `composition_registry.hpp:73`,
`text_font_resolver.cpp:158`, `render_graph/executor/graph_executor.hpp:65`,
`glyph_layout.hpp:144`) ed evitare taint a cascata su tipi/template.

`SequenceContext` struct intatto — non rimosso in questo chore per
AGENTS.md §`### 2×-in-one-chore` rule (deprecation reversal bundles
forward-point ticket atomicamente).

## Build-environment posture

`-Wno-error=deprecated-declarations` (CMakeLists.txt:28) → i 22 caller
esistenti (10 in `tests/core/timeline/test_sequence.cpp` + 9 in
`tests/certification/test_timeline_functional_v1.cpp` + 2 in
`tests/core/animation/test_spring.cpp` + 1 in
`tools/verify_timeline_functional_linux.sh`) continuano a compilare,
emettendo warnings espliciti che fungono da indicator-to-migrate per
Phase 2.

## Soluzione Phase 2 (forward-point) — deferred

Convertire `SequenceContext` in thin adapter su `SequenceWindow`
(struttura matematica pura: solo `Frame from + Frame duration` + helper
statici `active(global_frame, from, duration) / progress(local_frame, duration) /
held_progress(global_frame, from, duration, progress)` — NO builder,
NO parent), oppure deprecare `SequenceContext` completamente dopo
aver migrato i 22 caller a `SequenceBuilder`.

Sub-tasks forward-point (non blocked da questo chore):

1. **SequenceWindow adapter struct** (`include/chronon3d/timeline/sequence_window.hpp`):
   pure-math con `Frame from`, `Frame duration`, e metodi statici
   `active(global_frame, from, duration)` / `progress(local_frame, duration)`
   / `held_progress(global_frame, from, duration, progress)`. Zero side
   effects, zero parent reference, zero builder.
2. **SequenceContext → thin forward** a SequenceWindow con conversione
   `FrameContext::local_time() → SequenceWindow` bridge layer (1-line
   delegations sui 6 metodi).
3. **Caller migration batch**: 22 test call-sites (test_sequence.cpp:12-92
   + test_timeline_functional_v1.cpp:196-256 + test_spring.cpp:113-117) →
   `SequenceBuilder + FrameContext::local_time()` (canonical productive
   path). Per-AREA ordering: animation tests → certification → sequence-v2
   showcases.
4. **Marker rimosso**: `[[deprecated]]` rimosso da factory + spring
   overload; SequenceContext struct merge-folds in SequenceWindow
   (final source-removal Cat-2 freeze — ABI-stability ADR richiesto).

## Cat-3 minimal-surface

- 2 `[[deprecated]]` attribute additive (zero nuova API)
- 2 comment-block sopra i marker (cronaca inline, ~10 linee ciascuno)
- 1 NEW ticket-home + 1 NEW FOLLOWUP row + 1 NEW CHANGELOG entry cite-only
- Zero struct change, zero ABI break, zero deletion

## Cronaca per AGENTS.md §Docs canonical update discipline

- Current_status.md: nessuna update (no area state change; Sequence V1 funzionalmente PASS invariato).
- Roadmap.md: nessuna update (no milestone change).
- Changelog.md: 1-line cite-only entry prepended in `## 2026-07-26` (Cat-5 3-doc same-commit).
- Followup_tickets.md: 1 row prepended in §Open Blockers (NEW P2 OPEN tracker; Cat-5 4-doc same-commit per AGENTS.md §"2×-in-one-chore" pattern, pre-esistente in TICKET-COMPOSITIONDESCRIPTOR-MIGRATION lineage).

## Forward-points (sub-tickets)

1. **TICKET-ANIM-SEQUENCE-WINDOW** (P2 OPEN Fase 2): aggiungere `struct SequenceWindow`
   pure-math in `include/chronon3d/timeline/sequence_window.hpp`. Cat-3 minimal-surface
   (1 NEW POD struct + 3 static free-functions; 0 ABI break).
2. **TICKET-ANIM-SEQUENCE-CALLER-MIGRATION** (P2 OPEN Fase 2): bulk migration dei 22
   call-sites (10 test_sequence + 9 test_timeline_functional_v1 + 2 test_spring +
   1 verify_timeline_functional_linux). Per-AREA ordering: animation → certification
   → sequence-v2 showcases → content/apps.
3. **TICKET-ANIM-SEQUENCE-CONTEXT-REMOVAL** (P2 OPEN Fase 3): post-migration, rimuovere
   `[[deprecated]]` markers + SequenceContext struct (ABI-stability ADR richiesto
   per Cat-2 freeze source-removal gate).
4. **Cat-5 cross-checks**: `tools/check_doc_sync.sh` (verifica cite-only consistency),
   `tools/wrap_push.sh` (GATE-MNT-01 atomic-commit), `tools/check_architecture_boundaries.sh`
   (Gate 5 Check 11 deny-everywhere `#include <msdfgen>/<libtess2>/<unicode[/...]>`
   preservato).
