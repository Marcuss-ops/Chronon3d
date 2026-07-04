# TICKET-ZERO-EXECUTION-PLAN-2026-07-04

> **Stato:** PLANNED (doc-only) — emissione del piano operativo per la prossima serie di commit su `main` post-FF-pull di `f240ffe2`.
> **Area:** Baseline verde + rimozione legacy + tenuta architetturale. Nessuna nuova feature.
> **Vincolo:** AGENTS.md v0.1 §Feature Freeze, Cat-3 freeze-compliant (rimozione percorsi legacy, test deterministici, doc-sync).
> **Source of truth per ogni item:** riga puntata in `docs/FOLLOWUP_TICKETS.md` + ADR-013 quando il contratto è già documentato + `docs/CURRENT_STATUS.md` snapshot di macchina-verifica.

## 0. Contesto operativo (post-FF-pull di `f240ffe2`)

Repository sincronizzato a `origin/main@f240ffe2` (fast-forward di 2 commit: `f240ffe2` open TICKET-GATE-10-PHASE-4-BLACK DONE + `09e09beb` fix `image_writer.cpp` quantization off-by-one su PNG `save_png` top-end). `tools/check_main_clean.sh` GATE_PASS, working tree clean, per-branch rebase `true`. Tutti i prossimi commit partono da qui.

## 1. Audit del pasted-analysis (claim rettificati contro stato reale)

Il prompt operativo originale elenca 8 claim. Li verifico uno a uno contro codice + ADR + canonical docs correnti. **L'azione su main si basa SOLO sui claim confermati; i claim obsoleti NON generano commit.**

| # | Claim del pasted-analysis | Verità al `f240ffe2` | Decisione |
|---|---------------------------|----------------------|-----------|
| 1 | "CameraSessionLease rollback fittizio: ritorna lease che punta a `e.checkpoint.session` (cache reale), distruttore non fa rollback" | **PARZIALMENTE VERA.** `lease.session()` ritorna `*session_` (ref mutabile) = `*&entry.checkpoint.session` reale della cache (`camera_session_cache.hpp:78`). Distruttore (`~CameraSessionLease()`, `camera_session_cache.cpp:159-162`) ha body vuoto con solo commento "If not committed, the session state is discarded." **REALTÀ: `evaluate(ctx, lease.session())` muta `entry.checkpoint.session` direttamente sul cache.** Il rollback descritto è onesto SOLO per `last_evaluated_frame` (commit_lease è il SOLE writepoint); per `session` è un'illusione. ADR-013 Decision 3 documenta questo **prima** che qualcuno lo fix. | **BUG REALE.** Item P0 #1 del piano (vedi §3.1). |
| 2 | "`CameraFailurePolicy::KeepLastValidCamera` segue ancora il ramo di `Stop`" | **FALSO.** `camera_program.cpp:526-558` mostra esplicitamente il branch distinct: `if (session.last_valid_camera.has_value()) { result.camera = *session.last_valid_camera; ... emit "Recovered:" diagnostic ...; return result; }`. ADR-013 Decision 2 cita il commit `4f4b7c2d`-line. | **NON GENERARE COMMIT.** Claim obsoleto. |
| 3 | "Gate #10 `install_consumer_test.sh` Phase 4 = render nero, root cause `sdk_render_engine.cpp.o` mancante dallo stale `libchronon3d_sdk_impl.a`" | **RISOLTO.** commit `8fd0588e` (canary entry per `chronon3d::sdk::RenderEngine` in `cmake/Chronon3DCanarySymbols.cmake`) + force-rebuild di `chronon3d_sdk_impl`. commit `09e09beb` aggiusta off-by-one arrotondamento PNG (`src/backends/image/image_writer.cpp`). `[BOUNDARY-OK] 230400/230400 pixels >5/255` su build pulita. | **NON GENERARE COMMIT.** Già verde. |
| 4 | "TICKET-011 build rot residuo: rename rotti in `test_text_run_umbrella_contract.cpp` + cast `std::string→bool` in `test_text_preset_descriptor.cpp:1140` + `text_unit_map` build rot" | **CONFERMATO** in `docs/FOLLOWUP_TICKETS.md` + `docs/CURRENT_STATUS.md` blocchi di TICKET-011. Sub-tickets inventariati: (i) field-rename `font_size`→`size` (M1.5#4 commit `1b44e521`), (ii) `static_cast<std::string>→bool`, (iii) `text_unit_map.cpp` + `test_text_material.cpp` linkage. | **BUG REALE.** Item P0 #2 del piano (vedi §3.2). Meccanico: 1 file rotto per commit. |
| 5 | "TICKET-GATE-11-PRINTF-FIX su `software_grid_background_processor.cpp:23`: il file attuale non contiene printf" | **DA VERIFICARE** a `f240ffe2`. La claim è coerente con CURRENT_STATUS "Gate #11 FAIL pre-`b62ef4429`" + l'audit più recente che gira carica `backend sanitization`. `printf` può esistere ma non necessariamente nel processor ipotizzato. | **Item P1 #1 (audit) + sub-fix atomico per file.** Vedi §3.6. |
| 6 | "M1.5#10: eliminare `include/chronon3d/text/rich_text.hpp` + `src/backends/text/rich_text.cpp`. Step 1 (drop TEST_CASE in `test_design_kit.cpp`) fatta in working tree, commit pending this session" | **CONFERMATO** in `docs/tickets/TICKET-M1.5#10-SEQUENCE.md`. 4 step pianificati; Step 1/4 done (test_design_kit.cpp TEST_CASE rimosso, ~22 LOC). Steps 2/3 sweep-only (atteso zero residui); Step 4 = rm + include cleanup. Freeze Cat-3 compliant. | **Item P1 #2.** Vedi §3.4. |
| 7 | "M1.5#9: continuare svuotamento legacy `software_text_processor.cpp`. Step 1 (migrate `tests/install_consumer/main.cpp:115-130` da `TextShape` a `TextRunShape`) fatto in working tree, commit pending this session" | **CONFERMATO** in `docs/tickets/TICKET-M1.5#9-SEQUENCE.md`. 5 step pianificati; Step 1/5 done. Step 2 = migrate `RenderNodeFactory::text()` (signature invariante, payload `ShapeType::TextRun` + handle) — callsites da aggiornare: `tests/scene/rendering/test_render_node_factory.cpp:104,116` + `tests/presets/test_presets.cpp`. Steps 3-5 = drop registration + delete directory tree (7 file) + delete legacy rasterizer infrastructure (4 cpp). | **Item P1 #3.** Vedi §3.5. |
| 8 | "Rendere il font I/O fence production-path-attivo (oggi default-OFF nella CLI/exporter/SDK consumer)" | **CONFERMATO** da `BLFontFaceCache::get_face()` doc + `FT_New_Face` call-site documentati in `docs/TEXT_BOTTLENECKS.md`. Rischio reale: cache miss font apre file via `BLFontFace::createFromFile()` nel render thread. | **Item P2 #2.** Vedi §3.8. |
| 9 | "Allocazioni nascoste nel text hot path: `TextScratchManager::acquire()` fa `new` quando il pool è vuoto + `GlyphOutlineBuilder` non è una cache" | **CONFERMATO** indirettamente in M1.5#6 review (carry-over nit #4: `build_blur_tiers` ×2 BEFORE check `s.silent_success_empty` è O(G) wasted work per off-canvas text). `TextScratchManager` `new` su pool-vuoto è Cat-3 internal optimization. | **Item P2 #1.** Vedi §3.7. |
| 10 | "Doc-drift P0: ROADMAP parla di `c73f7493` 9/11 PASS, CURRENT_STATUS più avanti registra `1078ab46` 11/11 PASS" | **CONFERMATO** dallo snapshot `main@1078ab46` Gate audit (11/11 PASS nella sezione `Gate audit snapshot — main@1078ab46 (2026-07-04, 11-gate audit)` di CURRENT_STATUS.md). ROADMAP snapshot `c73f7493` 9/11 PASS è historical. | **Item P1 #4 (audit doc-sync).** Vedi §3.3. |

**Claim-to-action mapping:** 7/10 generano commit (2 sono obsoleti/falso, 1 è transitorio).

## 2. Regole di lavoro (coerenti con AGENTS.md v0.1)

- **NO branches**, **NO feature**. Commit direttamente su `main`.
- **Push frequente** via `tools/wrap_push.sh origin main` (GATE-MNT-01 + auto-FF + clean-tree gate).
- **Doc-sync nello stesso commit** per ogni code change: `docs/CURRENT_STATUS.md` (stato), `docs/FOLLOWUP_TICKETS.md` (indice tracker), `docs/CHANGELOG.md` (chiusure).
- **Per-branch rebase** invariant: `git config --local --get branch.main.rebase` deve restare `true`; `tools/check_main_clean.sh` lo verifica (Step 4) e `tools/wrap_push.sh` Step 2.5 auto-ripara se missing.
- **Nessun singleton/registry/cache/service locator nuovo** (regola permanente).
- **Nessun nuovo `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>`** (Gate 5 deny-everywhere, vedi `tools/check_architecture_boundaries.sh` Check 11).
- **Nessuna GPU/GUI/browser** nel core headless CPU-first.
- **Nessun nuovo pubblico SDK surface** durante freeze (l'internal-only M1.5#9 + M1.5#10 muove `%s`/`%s` resta consentito).

## 3. Sequenza operativa (7 sotto-azioni + 2 follow-up)

Per ogni item: **(owner gate, ticket canonical, file cluster, build/test check, commit pattern)**.

### §3.1 [P0 #A1] — `fix(camera): make CameraSessionLease rollback real for session state`

- **Ticket canonical:** `docs/FOLLOWUP_TICKETS.md` → row `TICKET-A3-CACHE-LEASE` (PLANNED).
- **File cluster:**
  - `include/chronon3d/scene/camera/camera_v1/camera_session_cache.hpp` (lease struct + commit semantics).
  - `src/scene/camera/camera_v1/camera_session_cache.cpp` (acquire/commit/destructor + commit_lease).
  - NEW test `tests/runtime/test_camera_session_cache_failed_no_commit_session_state.cpp` (lock contract).
- **Bug descrizione:** `lease.session()` ritorna ref mutabile. Necessario: `lease.session()` deve restituire una **copia di lavoro**. Solo `lease.commit()` deve scrivere `entry.checkpoint.session = working_session;`. `lease.~()` deve scartare `working_session_`. `entry.checkpoint.last_evaluated_frame` resta single-writepoint commit-side.
- **Pre-fix:** `lease.session()` = `*session_` (ref mutabile a cache). `evaluate()` muta cache directly. Distruttore vuoto.
- **Post-fix atteso:** `lease.session()` = `*working_session_` (local copy allocata in lease ctor, `std::optional<CameraSession>` field on lease). `commit()` fa `entry.checkpoint.session = *working_session_; committed_ = true; entry.checkpoint.last_evaluated_frame = target_frame_;`. Destructor: if not committed, `working_session_.reset()` (impl. via `std::optional<CameraSession>`, no heap alloc beyond working_session_ itself).
- **Build/test check:**
  - `cmake --build build --target chronon3d_runtime --target chronon3d_core_tests` exit 0.
  - NEW `test_camera_session_cache_failed_no_commit_session_state.cpp` 2 SUBCASEs: failed eval → `cp_pre == cp_post` byte-identical (session + last_evaluated_frame); successful eval + commit() → only `last_evaluated_frame` advances AND `entry.checkpoint.session` has the post-eval state. Locked by ADR-013 Decision 3 verifier.
- **Doc-sync:** ADR-013 Decision 3 source anchor update + `docs/FOLLOWUP_TICKETS.md` row TICKET-A3-CACHE-LEASE = PARTIAL → in-progress. Cross-link to `docs/CHANGELOG.md`.
- **Commit pattern:** `fix(camera)!: make CameraSessionLease rollback real (TICKET-A3-CACHE-LEASE)`. `!` perché il contratto API surface della lease è semanticamente contratto (ref mutabile → working copy).
- **Effetto su feature freeze:** Item P0 #1 / M0 blocker → se fix verde + canary, contribuisce a 11/11 PASS su `main`.

### §3.2 [P0 #A2] — `fix(tests): TICKET-011 second-wave build rot (atomic per file)`

- **Ticket canonical:** `docs/FOLLOWUP_TICKETS.md` → row `TICKET-011` (PARTIAL, multi-file).
- **Sub-tickets (1 commit per file rotto):**
  - **(a)** `tests/text/test_text_run_umbrella_contract.cpp` — field rename `font_size`→`size` (+ 4 altri: `font_path`→`resolved_path`, `font_family`→`family`, `font_weight`→`weight`, `font_style`→`style`). Origine rot: M1.5#4 commit `1b44e521` ha rinominato ma il test usa nomi vecchi.
  - **(b)** `tests/text/test_text_preset_descriptor.cpp:1140` — `static_cast<std::string>→bool` su `CHECK_FALSE(d.fixture)` (probabilmente `d.fixture` è `std::string`, non `optional<X>`).
  - **(c)** `tests/text/test_text_material.cpp` — namespace lookup rot (`chronon3d::runtime::RenderRuntime` non esiste post-Fase B2).
  - **(d)** `tests/test_text_preset_registry.cpp` + `tests/support/layer_builder_inspection.hpp` — `chronon3d::registry::TextAnimatorSpec` non esiste (è `chronon3d::TextAnimatorSpec`).
  - **(e)** atteso discovery wave 3/4 con build pulita (TextRunBuilderInspector + 53 migrate callsites come da FOLLOWUP).
- **Pattern:** atomic commit per Ogni sub-ticket. Command pattern: `git mv` se rinomina + `// TODO(post-baseline)` se scope > file rot.
- **Doc-sync:** `docs/CURRENT_STATUS.md` text area row + `docs/FOLLOWUP_TICKETS.md` row TICKET-011 sub-row pointer.
- **Commit pattern:** `fix(tests,text): TICKET-011(a) FontLayoutIdentity field-rename rot`.
- **Effetto su feature freeze:** sblocca `chronon3d_core_tests` LINK step. Contribuisce a 11/11 PASS gate #9.

### §3.3 [P1 #A3] — `docs(sync): align ROADMAP snapshot to CURRENT_STATUS 11-gate audit`

- **Ticket canonical:** `docs/FOLLOWUP_TICKETS.md` → row `TICKET-GATE-7-R1` (PLANNED).
- **File cluster:**
  - `docs/ROADMAP.md` snapshot section update: `c73f7493` 9/11 PASS → `1078ab46` 11/11 PASS (post-fix `TICKET-GATE-10-PHASE-4-FIX` + canary `8fd0588e`).
  - `docs/CURRENT_STATUS.md` hyperlink freshness check.
- **Verità da allineare:**
  - Snapshot macchina-verificato più alto: `aaf70032` (10/11 PASS, gate #10 FAIL pre-fix).
  - Snapshot Gate audit 11/11 PASS: `1078ab46` (segnalato da CURRENT_STATUS.md ma non ancora macchina-verificato >10/11).
  - Goal: macchina-verifica `f240ffe2 + §3.1 + §3.2` lands at 11/11 PASS + processo di `docs/baselines/main-<sha>-baseline.md`.
- **Doc-sync:** solo ROADMAP + CURRENT_STATUS cross-link + CHANGELOG + entry in `docs/baselines/main-<sha>-baseline.md` post-machine-verification.
- **Commit pattern:** `docs(sync): ROADMAP snapshot 11/11 audit + GATE-MNT-01 baseline candidates`.

### §3.4 [P1 #A4] — `refactor(text): M1.5#10 step 2-4 — sweep + rm rich_text.hpp/cpp`

- **Ticket canonical:** `docs/tickets/TICKET-M1.5#10-SEQUENCE.md`.
- **Pre-step check:** Step 1/4 done in working tree, commit pending. Per `docs/tickets/TICKET-M1.5#10-SEQUENCE.md` Step 2 = grep sweep `RichTextRun` (atteso ZERO prod callers post-Step-1); Step 3 = grep sweep `draw_rich_text` (atteso ZERO prod callers); Step 4 = `rm include/chronon3d/text/rich_text.hpp + src/backends/text/rich_text.cpp` + drop include da `design_kit.hpp` line 17.
- **Verifica preliminare (in questo commit):** `grep -rnE '\bRichTextRun\b' --include='*.cpp' --include='*.hpp'` e `grep -rnE '\bdraw_rich_text\b' --include='*.cpp' --include='*.hpp'`. Se ZERO residui (escludendo i file da eliminare in Step 4), Step 2-3 sono doc-only commit.
- **Step 4 atomiche:** rm i 2 file + drop include + sweep globale `rich_text.hpp|RichTextLine|RichTextRun|draw_rich_text|RichTextAnchor|RichTextVerticalAnchor|RichTextRunMetrics|RichTextLineMetrics|RichTextLayoutOptions` → ZERO atteso. AGENTS.md Cat-3 freeze-compliant (zero new public API surface, zero new singletons/registries/caches).
- **Build/test check:** `bash tools/check_legacy_text_pipeline.sh` PASS + `cmake --build build --target chronon3d_text_core --target chronon3d_backend_text --target chronon3d_layout_tests` exit 0.
- **Doc-sync:** CURRENT_STATUS text area row M1.5#10 + FOLLOWUP_TICKETS row + CHANGELOG.
- **Commit pattern:** `refactor(text): M1.5#10 step 2-4 progressive deletion of rich_text.hpp`.

### §3.5 [P1 #A5] — `refactor(software-text): M1.5#9 step 2-5 — finish legacy TextShape pipeline`

- **Ticket canonical:** `docs/tickets/TICKET-M1.5#9-SEQUENCE.md`.
- **Step 1/5 done** in working tree (commit pending): `tests/install_consumer/main.cpp:115-130` migrazione `TextShape` → `TextRunShape` + inventory header su `software_text_processor.cpp:21-65`.
- **Step 2/5:** migrate `src/scene/model/render_node_factory.cpp::RenderNodeFactory::text()` — firma pubblica invariata; internamente delega a `materialize_text_run_shape(p, engine, sample_time)`. Callsites da aggiornare: `tests/scene/rendering/test_render_node_factory.cpp:104,116` + `tests/presets/test_presets.cpp`.
- **Step 3/5:** drop `create_text_processor()` factory + `registry.register_shape(ShapeType::Text, ...)` in `src/backends/software/processors/builtin_processors.cpp`.
- **Step 4/5:** delete `src/backends/software/processors/text/` (7 file: software_text_processor.cpp + software_text_effects.cpp + text_glow.cpp + text_shadow.cpp + text_processor_helpers.hpp + software_text_effects.hpp + text_effects.hpp).
- **Step 5/5:** delete `include/chronon3d/backends/text/text_rasterizer_utils.hpp` + `src/backends/text/text_rasterizer_render.cpp` + `text_rasterizer_cache.cpp` + `text_rasterizer_ink.cpp` + `text_layout_engine.hpp`. Migrate o delete i 2 test legacy (`test_text_material.cpp` + `test_text_cache_key.cpp`).
- **Build/test check:** cmake re-configure dopo ogni step (zero nuovi target + zero rotti). `cmake --build build` exit 0.
- **Doc-sync:** `docs/CURRENT_STATUS.md` text area row + `docs/FOLLOWUP_TICKETS.md` row + `docs/CHANGELOG.md`.
- **Commit pattern:** uno per step: `refactor(software-text): M1.5#9 step 2 — RenderNodeFactory::text delegates to materialize_text_run_shape`.

### §3.6 [P1 #A6] — `fix(sw-print): TICKET-GATE-11-PRINTF-FIX atomic per file reale`

- **Ticket canonical:** `docs/FOLLOWUP_TICKETS.md` → row `TICKET-GATE-11-PRINTF-FIX` (PLANNED).
- **Audit preliminare (in questo commit):** `grep -rn '\bprintf\b' src/backends/software/processors/`. Se trovati: lista file + line per fix atomico. Se trovati in `software_grid_background_processor.cpp:23` come claim originale: drop + replace con `chronon3d_log::log_info` o `diagnostic_chronicler::info(msg)` (API canonica).
- **Test/build check:** `bash tools/check_backend_sanitization.py` exit 0.
- **Doc-sync:** `docs/FOLLOWUP_TICKETS.md` row + `docs/CHANGELOG.md`.
- **Commit pattern:** `fix(backends,software): TICKET-GATE-11-PRINTF-FIX drop printf in <file>:<line>`.

### §3.7 [P2 #A7] — `perf(text): add observability benchmark for TextScratchManager`

- **Ticket canonical:** implicit / `docs/PERFORMANCE_BOTTLENECKS.md`.
- **Scope (post-baseline):** non c'è un ticket canonical aperto per questo. È un follow-up P1/P2 che diventa canonico dopo baseline verde.
- **File cluster:** `tests/benchmarks/test_text_scratch_pool_bench.cpp` (NEW) con micro-benchmark deterministico (no PRNG) che conta fallback allocation e miss del pool.
- **Decisione: defer dopo baseline verde.** Tracked in `docs/FOLLOWUP_TICKETS.md` come P2 #NEW dopo §3.1-§3.6.

### §3.8 [P2 #A8] — `feat(tools): font I/O fence e2e test in install_consumer`

- **Ticket canonical:** implicit; va aperto in `docs/FOLLOWUP_TICKETS.md` come P2 #NEW.
- **Decisione: defer dopo baseline verde + consumer SDK certificato.**

## 4. Pre-push checklist (per ogni commit)

Eseguiti in ordine prima di ogni `git push`:

```bash
# 1. Verifica stato locale
git status -sb                       # atteso: nothing to commit, working tree clean + ahead 1
git rev-parse HEAD                   # catturare SHA per il commit message + baseline doc

# 2. Se tool modificati / CMakeLists modificati → rebuild mirato
cmake --build build --target <target_toccato> -- -j$(nproc)

# 3. Se codice di camera modificato → camera gate
bash tools/check_camera_architecture.sh   # atteso: 6/6 PASS

# 4. Se codice di text/software modificato → software + backend
bash tools/check_software_renderer_boundary.sh   # atteso: PASS
bash tools/check_backend_sanitization.py         # atteso: PASS

# 5. Se testo/FOLLOWUP_TICKETS/CURRENT_STATUS toccati → doc-sync
bash tools/check_doc_sync.sh                    # atteso: PASS

# 6. Gate fail-on-dirty + auto-FF
bash tools/check_main_clean.sh                  # atteso: GATE_PASS
# oppure (drop-in):
bash tools/wrap_push.sh origin main            # step 1=git fetch; step 2=auto-FF; step 3=gates; step 4=nudge
```

## 5. Sequenza di priorità consigliata (commit order)

```text
[f240ffe2 HEAD]
    │
    ├── §3.1 [P0 #A1] CameraSessionLease rollback real           atomic 1 commit
    ├── §3.2 [P0 #A2] TICKET-011 sub (a) FontLayerIdentity       atomic 1 commit
    ├──     (b) text_preset_descriptor cast                       atomic 1 commit
    ├──     (c) test_text_material ns lookup                      atomic 1 commit
    ├── §3.3 [P1 #A3] DOC-SYNC (ROADMAP/CURRENT_STATUS)           atomic 1 commit
    ├── §3.6 [P1 #A6] TICKET-GATE-11-PRINTF-FIX audit+fix         atomic 1 commit
    ├── §3.4 [P1 #A4] M1.5#10 step 2-4 (sweep + rm rich_text)    atomic 1 commit
    ├── §3.5 [P1 #A5] M1.5#9 step 2-5 (5 commit)                  atomic 5 commit
    │
    └── # macchina-verifica 11/11 PASS + revoke feature freeze
       ├── docs/baselines/main-<sha>-baseline.md
       └── AGENTS.md §"Feature Freeze" revoca section
```

## 6. Cross-reference (link canonici)

- AGENTS.md §"Feature Freeze — V0.1 (attivo)" — vincolo temporaneo di non-nuove-feature.
- `docs/CURRENT_STATUS.md` — snapshot corrente + stato per area.
- `docs/ROADMAP.md` M0/M2.A3 (Agent3 camera cluster).
- `docs/FOLLOWUP_TICKETS.md` — indice blocker aperti (canonical). Tutti i ticket P0/P1 qui referenziati sono righe esistenti di questa tabella.
- `docs/adr/ADR-013-camera-v1-semantics.md` — 6 decision contracts + ADR-013-EXT (Decision 7: DiagnosticChannel canonical emit; Decision 8: compiler determinism).
- `docs/adr/ADR-011-camera-legacy-deletion.md` Decision 5 — `Camera2_5D::projection_mode` rot precedent (commit `ac514fea` fix).
- `docs/tickets/TICKET-M1.5#9-SEQUENCE.md` + `docs/tickets/TICKET-M1.5#10-SEQUENCE.md` — schema M1.5 cluster pattern.
- `tools/check_main_clean.sh` + `tools/wrap_push.sh` + per-branch rebase invariant (`branch.main.rebase = true`) — GATE-MNT-01 triade.

## 7. Non-goal (per questa azione)

- **NO** nuovi render graph node, effect processor, backend.
- **NO** nuovi preset, registry, resolver, sampler.
- **NO** estensioni V3 tile-first.
- **NO** Expressions V2 `enabled` flip.
- **NO** nuovi formati input/output.

## 8. Stats finali attese (post-§3.1-§3.6)

- `rich_text.hpp` (380 LOC) + `rich_text.cpp` (90 LOC) → 0 LOC (-100%).
- `src/backends/software/processors/text/*` (7 file, ~1145 LOC) → 0 LOC (-100%).
- `include/chronon3d/backends/text/text_rasterizer_utils.hpp` + `text_layout_engine.hpp` → 0 LOC (-100%).
- `src/backends/text/text_rasterizer_render.cpp` + `text_rasterizer_cache.cpp` + `text_rasterizer_ink.cpp` → 0 LOC (-100%).
- CameraSessionLease semantics: working-copy on rollback → atomic + safe + contract-honest (matches ADR-013 Decision 3 source-anchor docs).
- TICKET-011 sub-rot: tutti risolti atomic.
- Public API surface delta: 0 (only shrinkage: 4 legacy types removed).
- New singleton/registry/cache/service-locator: 0.
- New `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>`: 0.
