# TICKET-TEXT-VISIBILITY-PIPELINE — Text visibility audit + bbox contract (13-section roadmap)

> **Status:** PLANNED (parent roadmap; FU01..FU13 follow-up chain to be implemented forward-only on `main`).
>
> **Area:** Render-Time Diagnostics / Text V1 Cert / Compositor.
>
> **Vincoli architetturali (AGENTS.md v0.1):**
> - **Cat-3 freeze-compliant:** zero new public API surface. `TextVisibilityAudit` struct lives behind `#ifdef CHRONON3D_BUILD_DIAGNOSTICS` in `include/chronon3d/text/text_visibility_audit.hpp` (NOT in `include/chronon3d/`). <!-- drift-allow: stale-ref -->
> - **Cat-5:** no new singleton/registry/resolver/cache; audit function is a free function `audit_text_visibility(...)` in `src/text/`.
> - **Cat-1/2:** not applicable (diagnostic + regression-locks only).
>
> **Upstream (already-tracked blocker):** [`TICKET-TEXT-CLIP-PREDICTED-BBOX`](docs/tickets/TICKET-TEXT-CLIP-PREDICTED-BBOX.md) → FU01 of this ticket.
>
> **Source of truth:** main `text_visibility_audit` chain — implemented forward-only as 13 atomic FU commits on `main`, gated by `tools/wrap_push.sh origin main`.
>
> **Forward-only rule:** `TICKET-TEXT-CLIP-PREDICTED-BBOX` must close (FU01) before any subsequent FU lands, otherwise pre-fix regressions mask post-fix regressions.

## User spec (verbatim) — pipeline intent

> Per assicurarti che il testo venga mostrato correttamente non devi affidarti solo ai PNG golden. Serve una **catena di controlli automatici**, dal font fino ai pixel finali.
>
> **L'obiettivo è questo:**
>
> ```text
> Font risolto
> → layout valido
> → glyph presenti
> → bbox locale corretto
> → trasformazione applicata una sola volta
> → predicted_bbox contiene il testo reale
> → clip_rect non elimina il testo
> → pixel finali visibili
> ```
>
> Comportamento attuale "logga errore e renderizza vuoto" è pericoloso perché produce un artifact formalmente valido ma visivamente rotto. Il materializzatore restituisce infatti `nullptr` quando manca il `FontEngine`.

## 13 sectioni (sintesi)

| #  | Sezione | Funzione / Struttura | Scope |
|----|---------|----------------------|-------|
| 1  | `TextVisibilityAudit` + `audit_text_visibility(...)` | `struct` non-pubblico (gated) + free function canonica ri-usata da `TextRunNode`, tile pruning, `TextCompleteness`, diagnostics CLI, telemetria | `include/chronon3d/text/text_visibility_audit.hpp` + `src/text/text_visibility_audit.cpp` | <!-- drift-allow: stale-ref --> <!-- drift-allow: stale-ref -->
| 2  | Pre-render invariants: `MissingFontEngine`, `FontResolutionFailed`, `ShapingProducedNoGlyphs`, `InvalidLayout` | `TextError`-based fail-loud invece di blank-frame silent | materializer `compile_or_cache_layout()` |
| 3  | BBox math contract: `predicted_bbox.contains(world_ink_bbox, 2.0f)` | `transform_aabb` + `expand(rect, 2.0)` + log strutturato `[TEXT-AUDIT] bbox_contract_violation` | `src/text/text_layout.cpp` | <!-- drift-allow: stale-ref -->
| 4  | Conservative fallback: counter + `intersect(expand(world), canvas)`; se anche world inaffidabile → `canvas_rect` + `disable_tile_pruning_for_this_node=true` | correttezza > performance | `src/render_graph/pipeline/text_run_node.cpp` | <!-- drift-allow: stale-ref -->
| 5  | Extend `TextCompleteness` suite (non duplicare); aggiungere per ogni `TEST_CASE`: `font_resolved`, `shaping_succeeded`, `glyph_count > 0`, `predicted_contains_world`, `clip_contains_visible_ink`, `alpha_bbox.height > font_size*0.65f` | niente suite parallele | `tests/text_golden/text_clip/text_completeness.cpp` |
| 6  | Test 3-tier: A struttura (REQUIRE) / B geometria (CHECK world-bbox contract) / C pixel (CHECK alpha-bbox + min-pixel count) | golden = ultimo livello, MAI il primo | suite-wide convention |
| 7  | 12-axis coverage matrix: posizionamento, anchor, allineamento, trasformazioni, animazione, testo (lungo/Unicode), font (regular/bold/italic), Unicode (accenti/RTL/arab/CJK/emoji), effetti, canvas (16:9/9:16/quad), overflow (clip/visible/ellipsis/auto-fit), pipeline (CLI/video) | copertura per nuovi casi (`pin_to + TextAnchor`, `rotation X/Y`, tile pruning on/off, video pipeline, `text_layout_debug`) | estensione suite esistente |
| 8  | Cross-pipeline parity: `SoftwareRenderer` diretto / `chronon3d_cli render` / `chronon3d_cli video` / serial / parallel / `tile_pruning` ON/OFF / `diagnostic` ON/OFF / `text_layout_debug` ON/OFF | confronto alpha_bbox con tolerance 1px (non byte-equal); debug vs normal mode è la diagnosi-chiave | nuova suite `tests/text_golden/text_clip/cross_pipeline_parity.cpp` | <!-- drift-allow: stale-ref --> <!-- drift-allow: stale-ref -->
| 9  | CLI `inspect-text` subcommand: `chronon3d_cli inspect-text <composition> --frame N --json` | output strutturato PASS/FAIL con violazioni diagnostiche | `apps/chronon3d_cli/commands/dev/command_inspect_text.cpp` + registration in `chronon3d_cli_dev` group |
| 10 | Structured logging `[TEXT-AUDIT]` per-node: `glyphs`, `font`, `position`, `local_bbox`, `world_bbox`, `predicted_bbox`, `clip`, `matrix_translation`, `matrix_scale`, `pruned` | no indirizzi di memoria; valori deterministici-comparabili | `src/text/text_visibility_audit.cpp` (call site via `spdlog::info` gated) | <!-- drift-allow: stale-ref -->
| 11 | Investigation procedure (5 step): riduci caso → primo stadio divergente → disabilita pruning → confronta debug/normal → scrivi test PRIMA del fix | nuova sezione in `docs/INVESTIGATION_PROCEDURES.md` (or inline doc) | docs-only | <!-- drift-allow: stale-ref -->
| 12 | Property-based deterministic fuzz: 500 seed × variabili (font 12–300, box var, pos var, scale 0.2–3.0, rot ±180°, ASCII+Unicode, canvas var); `TEXT_FUZZ_FAILURE seed=N` stamp on fail | generator-driven, no test manuali | `tests/text_golden/text_clip/text_fuzz_deterministic.cpp` | <!-- drift-allow: stale-ref --> <!-- drift-allow: stale-ref -->
| 13 | CI fail-on-loss-of-text: log scanner `tools/check_text_visibility_ci.sh` exit 1 su `no FontEngine available\|will render blank\|TextRunShape.*null\|bbox contract violation`; wired into `tools/wrap_push.sh` Step 4.5 + `tools/check_architecture_boundaries.sh` | nessun `--skip-gates` escape hatch | tool-only |

## Ordine pratico — implementazione forward-only

> 8 step sequenziali. Ciascuno è boundary di un singolo commit su `main`, push via `tools/wrap_push.sh origin main`.

1. **FU01** — Chiudere `TICKET-TEXT-CLIP-PREDICTED-BBOX` (upstream blocker, già `PARTIAL`).
2. **FU02** — Inserire fallback conservativo (§4) prima di qualsiasi altra diagnostica (correttezza > perf).
3. **FU03** — Estendere `TextCompleteness` (§5) senza creare suite parallele.
4. **FU04** — Aggiungere confronto pruning ON/OFF + render-debug/normal (§8 come test primitivo).
5. **FU05** — Aggiungere confronto render diretto / CLI / video (§8, matrice estesa).
6. **FU06** — Introdurre `TextVisibilityAudit` + `audit_text_visibility(...)` (§1) come dipendenza condivisa.
7. **FU07** — Aggiungere fuzz deterministico (§12).
8. **FU08** — Rendere bloccanti in CI i gates di §13 + log-scanner.

> Dopo FU01..FU08: §2 (pre-render invariants), §3 (bbox math contract), §9 (CLI inspect-text), §10 (structured logging), §11 (investigation procedure) si inseriscono come follow-up aggiuntivi (FU09..FU13) senza rimescolare i confini dei commit precedenti.

## Sub-tasks (cross-cutting, non bloccanti ma raccomandati)

- **ST-A** — `tests/text_golden/text_clip/cross_pipeline_parity.cpp` (estende §5 + §8): test pair-wise SUITE / RAW vs RUN, serial / parallel, debug mode. Output: `cross_pipeline_alpha_bbox_*` check (1px tol). <!-- drift-allow: stale-ref -->
- **ST-B** — `tests/text_golden/text_clip/text_fuzz_deterministic.cpp` (estende §12): 500 seed × 12 axis; output: `TEXT_FUZZ_FAILURE seed=N` + minimal repro snapshot. <!-- drift-allow: stale-ref -->
- **ST-C** — `tools/check_text_visibility_ci.sh` (estende §13): regex-based scanner; exit 1 on any `bbox contract violation` / `will render blank` / `TextRunShape.*null` / `no FontEngine available`; wired in `tools/wrap_push.sh` Step 4.5d.
- **ST-D** — `docs/CURRENT_STATUS.md` §Stato generale per area "Text Visibility" row con stato FU01..FU13 progress.
- **ST-E** — `docs/ROADMAP.md` §V0.2 milestone (transition Text V1 cert → Text Visibility cert): roadmap forward.

## Trade-offs cross-step

| Trade-off | Resolution |
|-----------|------------|
| `TextVisibilityAudit` API surface | NON pubblico: gated da `#ifdef CHRONON3D_BUILD_DIAGNOSTICS`, isolato in `include/chronon3d/text/` (NON in `include/chronon3d/`). Cat-3 freeze-compliant. |
| Suite duplicata vs estensione`TextCompleteness` | estensione della suite esistente; nessuna nuova suite matrice-cardinalità. Cat-5 (no duplication). |
| `pruning_on vs debug_mode` come test deterministico | test deterministico non-byte-equal; confronto alpha-bbox con 1px tolerance; output: `(pruned_x0, pruned_y0, debug_x0, debug_y0)` per pinpoint. |
| Fuzz 500 seed vs determinismo | seed deterministico (rolling uint32 + per-seed derived), full reproducibility on retest; macchina-verifica con `RandomSeed::FIXED == 1` in CI. |
| CI fail-on-loss-of-text vs `--skip-gates` | nessun escape hatch (AGENTS.md Cat-1 hardblock invariant preserved); violation → exit 1 → wrap_push.sh gate fail. |
| Conservative fallback §4 invasivo vs `predicted_bbox` "elegante" | fallback attivato SOLO quando contratto bbox viola; bbox valido → pruning ottimizzato; bbox sospetto → full-node fallback. Mai `bbox sospetto → testo tagliato`. |

## AGENTS.md v0.1 vincoli (riepilogo)

- **Cat-1 (hardblock):** zero `--skip-gates`; violation → CI fail; nessun test PASS-forzato.
- **Cat-2 (anti-duplication):** `TextVisibilityAudit` è l'unica funzione canonica (§1); nessun check parallelo in 5 file.
- **Cat-3 (freeze):** zero new public API surface. Audit struct lives in `include/chronon3d/text/` NON in `include/chronon3d/`; instrumentation gated.
- **Cat-4 (anti-artefact):** zero commit di file generati; build artifacts fuori da git (`build/` escluso da .gitignore).
- **Cat-5 (no new singleton/registry):** audit è free function + struct locale; nessun RegistroGlobale.

## Statistiche attese

- **New files:** ~6 (`text_visibility_audit.hpp/.cpp`, `command_inspect_text.cpp`, `check_text_visibility_ci.sh`, `cross_pipeline_parity.cpp`, `text_fuzz_deterministic.cpp`, optional `INVESTIGATION_PROCEDURES.md`).
- **Modified files:** ~12 (`compile_or_cache_layout`, `text_layout.cpp`, `text_run_node.cpp`, `text_completeness.cpp` estesa, multiple content/{showcases,examples} per regression-lock, `wrap_push.sh` Step 4.5d, `check_architecture_boundaries.sh`, `FOLLOWUP_TICKETS.md` index, `CURRENT_STATUS.md` row, `CHANGELOG.md` 13 entries).
- **New public API:** 0.
- **New singletons/registries/caches:** 0.
- **CI gate additions:** 1 (`tools/check_text_visibility_ci.sh`).
- **New TEST_CASEs:** ~50 (extension di `TextCompleteness` + nuova `cross_pipeline_parity` + nuova `text_fuzz_deterministic`).

## Cross-references

- [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md) §Open Blockers → row "TICKET-TEXT-VISIBILITY-PIPELINE" (aggiunta con questo commit).
- [`docs/CURRENT_STATUS.md`](../CURRENT_STATUS.md) §Stato generale per area "Text Visibility" (forward update post-FU08).
- [`docs/ROADMAP.md`](../ROADMAP.md) §V0.2 milestone Text V1 → Text Visibility transition.
- [`docs/CHANGELOG.md`](../CHANGELOG.md) 13 entries (1 per FU; FU01 = closer di `TICKET-TEXT-CLIP-PREDICTED-BBOX`).
- [`docs/tickets/TICKET-TEXT-CLIP-PREDICTED-BBOX.md`](docs/tickets/TICKET-TEXT-CLIP-PREDICTED-BBOX.md) — upstream FU01.
- [`docs/CORE_OWNERSHIP.md`](../CORE_OWNERSHIP.md) §Ownership (text subsystem).
- [`docs/RELEASE_GATE.md`](../RELEASE_GATE.md) §Text V1 cert requirements — bbox contract + alpha-bbox assertion sono parte della cert-required post-FU08.
- AGENTS.md v0.1 §Priorità obbligatoria #2 (Text V1 completamento) → questo ticket è la roadmap forward.
