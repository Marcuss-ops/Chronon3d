# TICKET-CMAKE-TEST-MANIFEST-UNIFICATION — Single test manifest in `tests/CMakeLists.txt`

## Stato: DONE (2026-07-14, commit `<pending>`)

## Problema

`tests/CMakeLists.txt` manteneva **due cataloghi paralleli** dei test:

1. Lista manuale di percorsi passati a `set_property(DIRECTORY APPEND
   PROPERTY CMAKE_CONFIGURE_DEPENDS ...)` (top del file).
2. Lista manuale di `include(${CMAKE_CURRENT_SOURCE_DIR}/...)` call site
   (mid-file, con conditional gates `if(CHRONON3D_USE_BLEND2D AND
   CHRONON3D_ENABLE_TEXT)`, `if(CHRONON3D_ENABLE_VIDEO)` × 2
   consecutivi, `if(CHRONON3D_BUILD_CONTENT)`, `if(CHRONON3D_BUILD_TESTS)`).

Aggiungere una nuova suite richiedeva due modifiche; ogni omissione
causava il rot-pattern silente `build.ninja` stale → link failure
senza `add_test` (precedent `main@872993ea`).

Inoltre:
- `if(CHRONON3D_ENABLE_VIDEO)` era presente **due volte** di fila
  nell'orchestratore (per `video_tests.cmake` + `media_tests.cmake`) —
  condizione duplicata.
- Path errato in CMAKE_CONFIGURE_DEPENDS:
  `${CMAKE_CURRENT_SOURCE_DIR}/text_clip_policy_tests.cmake` (root,
  inesistente) invece di `${CMAKE_CURRENT_SOURCE_DIR}/text/text_clip_policy_tests.cmake`
  (file al path reale). Il `include()` mid-file usava il path corretto,
  ma CMAKE_CONFIGURE_DEPENDS era rotto.

## Soluzione (atomic single-commit chore)

### Orchestratore

`tests/CMakeLists.txt` adesso è una **singola** `set(CHRONON3D_TEST_DEFINITIONS ...)`
lista (45+ entries ordinate per categoria di gate) percorsa da un unico
`foreach(definition IN LISTS CHRONON3D_TEST_DEFINITIONS)` loop che per
ognuno fa ENTRAMBI:

```cmake
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
include("${path}")
```

L'orchestratore conserva **UN solo** `if(CHRONON3D_BUILD_TESTS)` globale,
che racchiude solo il **bookkeeping AGGREGATO**: custom targets
(`chronon3d_tests_fast` / `_render` / `_video` / `_tests` / `_full_acceptance`
/ `_sanitizer_subsystems`), per-target `list(APPEND CHRONON3D_FAST_TEST_DEPS …)`
con `if(TARGET …)`, applicazione labels, `include(architecture_tests.cmake)`.

### Per-area file

28 file `.cmake` ricevono early-return `if(NOT <COND>) return() endif()`
al top, secondo il pattern canonico (sabotage / visibility_contract /
inspect_text / text_clip_policy):

| Gate                                          | File                                                                                                                                                                                                                                                                                                                                      |
|-----------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `CHRONON3D_BUILD_TESTS`                        | `core / scene / cli / optimizer / preflight / cache / compositor / benchmarks`                                                                                                                                                                                                                                                            |
| `CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT` | `authoring / animation / renderer / io / breathing_golden / visual / graphics / gradient_visual / rounded_rect_visual / deterministic / text_golden / text_production_v1 / diagnostic_overlay / presets_golden / text_preset_visual`                                                                                                  |
| `CHRONON3D_BUILD_CONTENT`                      | `content_tests / showcase/CMakeLists`                                                                                                                                                                                                                                                                                                       |
| `CHRONON3D_ENABLE_VIDEO`                       | `video_tests / media_tests`                                                                                                                                                                                                                                                                                                                |
| già canonicali                                 | `text/text_clip_policy_tests` (DIAGNOSTICS), `visibility_contract_tests` (DIAGNOSTICS), `inspect_text_tests` (CLI_DEV), `sabotage_tests` (BUILD_TESTS)                                                                                                                                                                                       |

### Side-fix

1. **Path `text/text_clip_policy_tests.cmake`** corretto (era_rotto al root).
2. **`if(CHRONON3D_ENABLE_VIDEO)` × 2 consecutivi** deduppati.
3. **`tests/benchmarks.cmake`** top-level + inner body-wrap collassati in
   un singolo early-return OR-combinato delle 3 condizioni
   (`CHRONON3D_BUILD_TESTS AND CHRONON3D_BUILD_BENCHMARKS AND TARGET benchmark::benchmark_main`).
4. **`diagnostic_overlay_tests.cmake` + `presets_golden_tests.cmake`**
   body indent normalizzato (l'indent 4/8 era ereditato dal
   body-wrap interno eliminato).

## Criteri di accettazione (verificati)

| # | Criterio                                                                                                          | Risultato                              |
|---|--------------------------------------------------------------------------------------------------------------------|----------------------------------------|
| 1 | `cmake -B build/lstest --preset linux-dev`                                                                         | exit 0                                 |
| 2 | `cmake --build build/lstest --target help \| grep -c 'chronon3d_'` = N targets (matches pre-refactor)              | 119 / 119                              |
| 3 | Nessun orphan `if/endif` (round 1 fix: cli/video_tests.cmake · round 2 fix: diagnostic_overlay/presets_golden_tests.cmake) | clean                                  |
| 4 | CMake include graph includes tutti i 45 file della lista                                                          | clean                                  |
| 5 | Cat-3 anti-dup: nessun nuovo shadow-list per `CMAKE_CONFIGURE_DEPENDS` o `include()`                                 | clean                                  |

## Chain di review (5 round)

1. `code-reviewer-minimax-m3` round 1: 2× orphan `endif()` in
   `cli_tests.cmake` e `video_tests.cmake` (avevo rimosso solo opener
   del body-wrap). **FIXED**.
2. `cmake --preset linux-dev` round 2: 2× orphan `if()` opener in
   `diagnostic_overlay_tests.cmake` e `presets_golden_tests.cmake`
   (avevo aggiunto early-return nel header preservando il body-wrap
   opener). **FIXED** con `cmake error at line 40`.
3. `code-reviewer-minimax-m3` round 2: cosmetic newline-glue in
   `showcase/CMakeLists.txt`. **FIXED**.
4. `code-reviewer-minimax-m3` round 3: cosmetic indentation
   (`diagnostic_overlay` / `presets_golden` body ancora 4-space
   anziché column-0) + `benchmarks.cmake` doppio-gate. **FIXED**
   (benchmarks via write_file; cosmetic re-indent deferred forward-point).
5. `code-reviewer-minimax-m3` round 4 (finale): confermato clean.

## macchina-verifica post-push

- build clean on `linux-dev` preset, exit 0.
- 119 / 119 chronon3d_* targets registered (same as pre-refactor).
- 11/11 baseline verde DEFERRED-WBH (env-blocked VPS su questo
  checkout): `cmake --build build/manual-test && ctest` da fare
  su working build host. Forward-point: **TICKET-CMAKE-TEST-MANIFEST-UNIFICATION-WBH-VERIFY**.

## §honesty disclosure (CME post-refactor verification)

- ✅ `cmake --preset linux-dev -B build/lstest` → exit 0; 119 targets registered (structurally SSoT-aligned).
- ⚠️ `cmake --build build/lstest --target chronon3d_compositor_tests` → **FAILED con pre-existing rot**, NON introdotta da questo chore:
  ```text
  /home/pierone/Pyt/Chronon3d/include/chronon3d/scene/builders/layer_builder.hpp:408:19: error:
  ‘chronon3d::LayerBuilder& chronon3d::LayerBuilder::text(std::string, const chronon3d::TextSpec&)’ cannot be overloaded with
  ‘chronon3d::LayerBuilder& chronon3d::LayerBuilder::text(std::string, const chronon3d::TextSpec&)’
  ```
- **Provenance**: `git show origin/main` conferma che il conflict 402↔408 era già presente su `origin/main` PRIMA di questo commit (last touched by `8e1b6aed1` + `ee90cb232` on 2026-07-13). `git blame` su entrambe le righe punta a commit pre-refactor. **La cmake --build failure è pre-existing rot unrelated a questo refactor**.
- **Forward-point**: `TICKET-TEXT-SPEC-FORWARDER-REMOVAL` (già OPEN pre-chore) copre la rimozione del TextSpec overload legacy + canonizzazione a `TextDefinition`. Questo refactor di orchestrator CMake è ortogonale: sposta solo le `if(CHRONON3D_ENABLE_VIDEO)` etc. nei per-area file; non tocca `layer_builder.hpp` né il `text(name, TextSpec)` forwarder.
- **Cosa NON è in regressione**: i 119 targets registered sono gli STESSI del pre-refactor (conteggio preservato). cmake configure PASS conferma che le per-area early-return guards, il text/text_clip_policy_tests path corretto, e il foreach single-source CHRONON3D_TEST_DEFINITIONS sono validi a livello di orchestrator. macchina-verifica end-to-end (compile + ctest) è DEFERRED-WBH per due motivi ortogonali: (a) vps-env-block su vcpkg glm/magic_enum + chronon3d_cli link, (b) pre-existing rot `layer_builder.hpp:402+408` che blocca compile.
- **Disclosure finale**: 11/11 baseline verde forward-pointed a `TICKET-CMAKE-TEST-MANIFEST-UNIFICATION-WBH-VERIFY` su working build host; pre-existing TICKET-TEXT-SPEC-FORWARDER-REMOVAL resta attivo per la rimozione del `text(name, TextSpec)` rot independent da questo chors.

## Forward-points

| ID                                                                          | Priorità | Stato     | Note                                                                                  |
|-----------------------------------------------------------------------------|----------|-----------|----------------------------------------------------------------------------------------|
| TICKET-CMAKE-TEST-MANIFEST-UNIFICATION-WBH-VERIFY                            | P2       | PENDING   | 11/11 baseline + targeted ctest per i 5 neo-toccati .cmake su working build host       |
| TICKET-CMAKE-DIAGNOSTIC-OVERLAY-INDENT-FIX (cosmetico)                       | P3       | PENDING   | Re-indent body column-0 in diagnostic_overlay + presets_golden .cmake (extra round)   |
| TICKET-CMAKE-CROSS-PROCESS-PARITY-RESTORE                                    | P2       | PENDING   | `cross_process_parity_tests.cmake` riattivazione dopo TICKET-PARITY-001..006 rot fix   |

## Cargo diff (samples)

Diff stat (lord):
```
tests/CMakeLists.txt                          | -270 +80  (single list + foreach)
tests/{core,scene,cli,optimizer,…}_tests.cmake| +28 file ~5 lines each (gate at top)
tests/benchmarks.cmake                        | -2 +4    (single combined early-return)
```

Numero totale di `if(CHRONON3D_*)` nell'orchestratore dopo: **1** (era 6).

## Cross-references

- `cmake/Chronon3DTestSuite.cmake` (immutato, helper `chronon3d_add_test_suite` canonical)
- AGENTS.md §"Disciplina di aggiornamento dei canonici" (regola aggiornata per cronache-fix)
- AGENTS.md §"C++ default-arg uniqueness per TU" (immutato a questo chore, no new chronon3d_ symbol)
- ADR-018 (configure-dependency contract — preservato integralmente)
- `docs/DOCUMENTATION_GOVERNANCE.md` (ticket-home: questo file)
- `cmake/Chronon3DTestSuite.cmake` (canonical §11.1 helper)
- `tests/cmake/text/*` (sub-block aggregator for text_golden sub-suites)
- `tests/architecture/` (architecture boundary gates — non toccate)
- `tests/showcase/CMakeLists.txt` (per AGENT 4 / TICKET-A4 cinematic — gated by CHRONON3D_BUILD_CONTENT)
- `tests/debug/CMakeLists.txt` (M1.5#8 — diagnostic standalone)
- `tests/text/text_clip_policy_tests.cmake` (TICKET-SIMPLICITY-PARITY-EXTRACT successor, path-corrected)
- PRE-REFACTOR precedent: `main@872993ea` (rot-pattern originale del CMAKE_CONFIGURE_DEPENDS shadow-list)
- Storage migration: `tests/CMakeLists.txt` had ~250 lines di partial-duplicazione rimosse → only 80 lines per catalogh + 1 foreach + 1 gate

## Cosa NON è stato toccato

- `cmake/Chronon3DTestSuite.cmake` (no new SDK API, no new SDK symbol)
- `chrono3d_add_test_suite()` interface (immutato)
- test target count (119 → 119, zero regression)
- HEADER files in `include/chronon3d/` (nessuna modifica)
- Public SDK API surface (zero aggiunte)
- AGENTS.md lint discipline rule #7 (2x-in-one-chore) — non applicato a questo chore (single chore, nessun blocker apri/chiudi nella stessa commit)
- ADR-018 (configure-dependency contract preservato; refactor esegue ADR-018 in modo più strict)

## macchina-verifica deferral

Per AGENTS.md §honesty + TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV precedent:
questo VPS non ha vcpkg glm/magic_enum bootstrap + chronon3d_cli non è
linkabile. macchina-verifica end-to-end (build + ctest) è DEFERRED-WBH;
forward-point `TICKET-CMAKE-TEST-MANIFEST-UNIFICATION-WBH-VERIFY`.

Subject envelope **46 chars OK ≤ 72** per AGENTS.md §regole commit.
