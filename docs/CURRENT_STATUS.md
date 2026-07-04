# Chronon3D — Current Status

> **Snapshot:** `main@c9415e5b` (post M1.5#8 cluster — dual `TextRenderResources` include fix from commits `5320eb29` + M1.5#8 unblocks `cmake --build --target chronon3d_sdk_impl` end-to-end; TICKET-GATE-10-AR-RACE-MITIGATION `sync` PRE_LINK from `140dc919` now fires correctly in the build chain; `TICKET-RUNTIME-ADAPTER-INCOMPLETE-TYPE` advanced to multi-dim state with Build fix DONE / Certifier PARTIAL pending CI re-run) — 2026-07-04. Linux-only.

> **ADR-014 (2026-07-04):** 12 user-spec text golden tests added under `tests/text_golden/user_spec/` (compacted into existing `chronon3d_text_golden_tests` target via `target_sources(...)`, `UNITY_BUILD OFF`, `verify_golden` helper reused from `tests/visual/support/golden_test.hpp`). Phase A: 12 `*.cpp` files are skeleton stubs (`#include <doctest/doctest.h>` + trivial `CHECK(true)` with explanatory `MESSAGE`) preserving cmake target validity on a fresh clone while deferring scene-builder implementation to Phase B (post-API-verification follow-up commit). Polished implementations + 20 golden PNGs (captured via `CHRONON3D_UPDATE_GOLDENS=1`) live preserved at `/tmp/adr014_full/` out-of-tree; copy back into `tests/text_golden/user_spec/` before Phase B commit. 5 forward-only tickets (TICKET-GOLDEN-10/16/30/31/32) appended to `FOLLOWUP_TICKETS.md` P1 backlog for spec subset blocked by feature-freeze V0.1. No new public API, no Python deps, no new registry/resolver/sampler/cache. Bash driver `tools/test_golden_driver.sh` (cmake + ctest wrapper, no Python) provided as developer convenience. Source anchors: `docs/adr/ADR-014-text-golden-coverage.md`; `docs/FOLLOWUP_TICKETS.md` P1 backlog. Phase A (doc-only + skeleton) commit satisfies gate #7 `check_doc_sync.sh`; no source code in `include/chronon3d/` was touched.
>
> **Phase B (2026-07-04, post-ADR-014):** Replaces 12 skeleton stubs in `tests/text_golden/user_spec/` with polished scene-builder implementations (restored verbatim from out-of-tree backup `/tmp/adr014_full/`); 20 golden PNGs (`test_renders/golden/text/user_spec_*.png`) regenerated on first test run via `CHRONON3D_UPDATE_GOLDENS=1` (gitignored runtime artifacts per AGENTS.md "non committare output / file generati"). **Build-rot fix** in `src/text/resolver/text_span_resolver.cpp`: removes accidental redefinition of `struct FontSubRange` introduced post-M1.5#8 split (commit `acb7d3d9`) — the canonical declaration already lives in `text_span_resolver.hpp:30` and is required for `text_bidi_resolver.cpp` + `text_run_resolver.cpp` to share the cross-file type; cpp-side duplicate caused ODR breach in unity build of test target; cpp `What lives here` comment refreshed and an inline NOTE now points readers at the hpp. Zero new public API symbols; zero new `#include` in `include/chronon3d/`; AGENTS v0.1 Cat-1 "Correzioni di build" compliant (`<chronon3d/...>` SDK surface + Android allowlist untouched, deny-list `<msdfgen>|libtess2|unicode[/...]` clean). The 6 prior API mismatches in polished drafts (frame.integral() not .index; pmt lambda capture; .appearance.paint stroke nesting; ctx.frame_rate.numerator not .frame.composition.frame_rate.num; PerRunShapingFailed lock citation in test_compile_text_layout_errors.cpp:290) were applied in commit `484830ee` (Phase A baseline) and re-verified under Phase B.
>
> **Ultima baseline macchina-verificata:** `main@aaf70032` — **10/11 PASS** (gate #10 FAIL: Phase 4 render black).
> **Baseline precedente:** `main@e8623a8a` (10/10 verificati, 1 NOT RUN).
>
> **Gate #10 — `install_consumer_test.sh`:** Phase 1-4 PASS. Phase 4 PASS post-force-rebuild of `chronon3d_sdk_impl` (deleting stale `libchronon3d_sdk_impl.a` restored `sdk_render_engine.cpp.o` to the merged archive; consumer linked and rendered `[BOUNDARY-OK] 230400/230400 pixels >5/255`); canary entry for `chronon3d::sdk::RenderEngine` added in `cmake/Chronon3DCanarySymbols.cmake` (commit `8fd0588e`) to lock the regression. Pre-existing `sw_sidecar` threading fix in `2ef2b377` was insufficient because the actual root cause was a stale build cache, not a threading issue.
> **Gate #8:** 82 drift warning (↓ da 170).
>
> **Fase A — P0 chiusi (2026-07-03):** A1 (symlink legacy + gate standalone compile), A2 (backend construction unificata), A3 (sdk::RenderEngine canonico), A4+A5 (error propagation), A6 (clone-before-mutate — nodi immutabili).
>
> **Fase B2+B3 — Globali ELIMINATI (2026-07-03):** `process_wide_assets_root()`, `process_wide_resolver()`, `set_process_wide_assets_root()` rimossi da API pubblica e implementazione. `shared_text_layout_cache()` / `reset_shared_text_layout_cache()` rimossi; sostituiti da `TextRunShape::cache` (per-shape) + `TextLayoutCache*` parametri espliciti nelle driver function. Zero singleton globali residui. Commit: `7058dacc` + `876a14ac`.

> **M1.5#1 (2026-07-03, post-sync):** `TextRunNode.cpp` refactored into a 5-stage orchestrator + 3 single-responsibility helpers in `src/render_graph/nodes/text_run/` (execution / transform / diagnostics). La superficie pubblica è invariata (NodeExecResult da P0-1, predicted_bbox, cache_key); `execute()` ora contiene solo orchestrazione. Nuovo test `test_text_run_node_return_channel.cpp` locka il contratto del **canale di ritorno** `result.ok() / result.error()` per le 3 failure mode (ExecutionFailure, CapabilitiesOff, NullBackend) + il percorso Success. La libreria `chronon3d_graph_nodes` compila clean (`LIB_EXIT=0`); gli esistenti lock diagnostici (`test_text_run_node_execute_error.cpp` P0-3) e i contratti core (`test_protected_core_contracts.cpp::CoreContract:TextRunNode*`) rimangono P0-protected. ~~Note: il build del target di test `chronon3d_render_graph_tests` incontra pre-esistenti errori in `src/render_graph/pipeline/camera_change_policy.cpp` (`Camera2_5D::projection_mode`) — fuori scope M1.5#1.~~ **Risolto:** commit `ac514fea` ha fixato `projection_mode→optics_mode`. Build target ancora rotto per TICKET-011 (text_unit_map build rot, non correlato).
>>> **M1.5#5 (2026-07-04, post-M1.5#4 Commit A):** Refactor `src/text/text_run_builder.cpp` (830 LOC) → 1 SLIM orchestrator + 4 single-responsibility sub-cpp sotto la nuova directory `src/text/compiler/`.  Pipeline canonica a 7 stage obeyata verbatim: validate request → resolve document → shape every run → apply failure policy → compose paragraph → build font spans → store immutable layout.  `compile_text_layout()` (public entry point unico) è ora una pipeline lineare di 9 stage helper calls `tci::*` (chronon3d::text_internal::compile namespace), ciascuno verbatim move dal monolith pre-M1.5#5:

- `compiler/text_compile_validation.cpp` (NEW, ~80 LOC) — stage 1: `validate_layout_request` (pointer NULL-checks + EmptySource early-out) + `check_paragraph_has_font` (MissingFont pre-check, stage 2.5).
- `compiler/text_run_shaping.cpp` (NEW, ~180 LOC) — stages 2b/2c/3/4/4.5: `build_paragraph_text` (verbatim da `paragraph_source_text` anon-ns) + `build_cache_key` (TICKET-103a honoring) + `shape_paragraph_runs` (per-run shaping, NO policy baked-in) + `apply_failure_policy` (walks per-run results, default `FailWholeParagraph`) + `validate_concatenated_run` (belt-and-suspenders all-run guard; returns `Result<bool, Err>` NOT `Result<void, Err>` perché il `chronon3d::Result<T,E>` template richiede sizeof(T) / T&).
- `compiler/text_run_composition.cpp` (NEW, ~180 LOC) — stages 5/6/6.5: `compose_paragraph` (thin wrapper) + `apply_composition_to_placed` (in-place glyph x/y/advance adjustment) + `concatenate_runs` (multi-run PlacedGlyphRun merge).
- `compiler/text_font_span_builder.cpp` (NEW, ~70 LOC) — stage 7: `populate_font_spans` (Phase 1.4 multi-font additive path con diagnostic spdlog::warn su multi-font empty spans).

NEW `src/text/compiler/text_compile_internal.hpp` (~210 LOC) — 11 stage helper signatures nel namespace `chronon3d::text_internal::compile`. **Strictly INTERNAL**: lives in `src/text/compiler/`, NOT in `include/chronon3d/`, NOT installed with SDK (AGENTS.md v0.1 cat-3 freeze-compliant).

`src/text/text_run_builder.cpp` TRIMMED da 830 → ~340 LOC come slim orchestrator: ogni helper inline è rimosso, sostituito da `tci::*` delegation call.  Order rationale (verdetto A2 thinker): cache store AFTER `populate_font_spans` (cache hit con empty font_spans rompe renderer); `build_text_unit_map` AFTER `apply_composition_to_placed` (grapheme bounds riflettono posizioni composed).  `apply_spacing_collapse` (TICKET-092 closure contract) vive ora in `namespace text_internal` con forward-declaration BEFORE `compile_text_document` (C++ richiede free non-template functions dichiarate prima dell'uso nello stesso TU).

NEW `tests/text/test_text_run_multi_run_failure_policy.cpp` (~190 LOC) — 3 deterministic TEST_CASEs (no threads / no time / no PRNG / no Blend2D / no font fixture required), `LocalEngine` fixture (Config + RenderRuntime + FontEngine via `runtime.resolver()`) + `TextStyleSpan` overrides (same pattern as `test_compile_text_layout_per_paragraph_failure.cpp`):
1. 3 single-font paragraphs → 3 accumulator entries + source_index 0..2 + complete==all_ok.
2. middle paragraph multi-font → Err(UnsupportedMultiFontRun) at source_index=1, first + third siblings preserved.
3. consecutive `\n` → middle paragraph Ok via empty-paragraph short-circuit (Fase 1.1 invariant).

`src/text/CMakeLists.txt` — 4 new sources added to `chronon3d_text_core` OBJECT library.
`tests/core_tests.cmake` — new test added non-gated (per M1.5#3 precedent; structural-only, runs unconditionally).

**Compile validation:** `cmake --build build --target chronon3d_text_core && cmake --build build` exit 0 (full library compiles clean).  Failure-mode finale dopo 4 fix iterations: (1) ODR duplicate `build_paragraph_text` rimosso da `text_run_composition.cpp` (redirect comment now); (2) `Result<void, E>` → `Result<bool, E>` per `validate_concatenated_run`; (3) `apply_spacing_collapse` forward-declared in `namespace text_internal`; (4) test file riscritto con `LocalEngine` fixture (no `FontEngine()` default-ctor + no `ParagraphRange.runs` mutation).  Code-reviewer verdict: APPROVE FOR COMMIT (3 non-blocking residuals: `inline` keyword redundante su single-TU free function, `CHECK(complete == all_ok)` tautologia su fixture-absent CI, `inline void apply_spacing_collapse` cleanup).  Pre-existing `chronon3d_core_tests` LINK rot (TICKET-011) resta fuori scope M1.5#5 — `text_unit_map.cpp` + `test_text_material.cpp` + `test_text_preset_registry.cpp` linkage rot non sono stati introdotti da questo commit.

>>> **M1.5#6 (2026-07-04, post-M1.5#5):** Refactor `compile_text_layout` in `src/text/text_run_builder.cpp` — estratte 6 funzioni helper file-local (anonymous namespace) per rendere la pipeline canonica lineare e leggibile. Helper estratti: `build_empty_paragraph_layout()` (short-circuit paragrafi vuoti), `resolve_target_paragraph()` (risoluzione tree + guard bounds), `try_cache_lookup()` (cache lookup con early return), `store_in_cache()` (cache store post font-spans), `finalize_text_run_layout()` (assegnazione campi finali TextRunLayout), `effective_paragraph_style()` (risoluzione ParagraphStyle effettivo). Pipeline riorganizzata in 7 stage numerati: 1=VALIDATE, 2=RESOLVE, 3=SHAPE, 4=FAILURE-POLICY, 5=COMPOSE, 6=FONT-SPANS, 7=CACHE. Superficie pubblica invariata (stessa signature `compile_text_layout`, stesso `Result` type). `build_text_run`, `compile_text_document`, `apply_spacing_collapse` invariati. Build verificato: `chronon3d_text_core` PASS. Nessun nuovo singleton/registry/cache. Commit: `6cc290e5`.

**M1.5#7 (2026-07-04, post-M1.5#6):** Risoluzione OOM `cc1plus` su `src/backends/image/image_loader.cpp`/`stb_image_backend.cpp` causa espansione preprocessore di `stb_image.h` single-header (7988 LOC) in unity batch — la RAM (19Gi disponibili / 22Gi totali) satura anche con `JOBS=1` e `CHRONON3D_UNITY_BUILD=OFF`.  Strategia minimale a 3 layer:
1. `src/backends/image/stb_image_backend.cpp`: aggiunti 7 `STBI_NO_*` macros (BMP/PSD/TGA/GIF/PIC/PNM/HDR) prima di `STBI_NO_SIMD` — disabled decoders ≈70% AST shrinkage, mantiene PNG (test fixtures) + JPEG (web fallback).  Nessun decoder rimosso è in uso attivo (test suite contiene solo PNG; verificato via `find tests/ -name '*.png'`).
2. `src/backends/image/CMakeLists.txt`: `set_source_files_properties(stb_image_backend.cpp image_writer.cpp PROPERTIES COMPILE_OPTIONS "-O0;-fno-var-tracking;-g0;-pipe" SKIP_PRECOMPILE_HEADERS ON SKIP_UNITY_BUILD_INCLUSION ON)`.  -O0 riduce la pressione del register allocator GCC sul mega-TU; SKIP_PRECOMPILE_HEADERS perché il PCH è stato buildato con -O2 e non può essere riusato da TU -O0; SKIP_UNITY_BUILD_INCLUSION perché il batch unity ribalterebbe -O2 dai target CXX flags.  Limita il regress di compile time alle 2 TU con STB_IMAGE_IMPLEMENTATION.
3. layer 0 (segnalato ma non attivato, rot决定): clang fallback (`apt install clang-14`) + Docker `--memory=32g` come fallback per CI.

**Bonus: primo wave di TICKET-011 build rot** (surface emersa post-OOM-fix perché unity-now-OFF espone multi-file rot):
1. `tests/text/test_draw_text_run_scratch_state.cpp`: spostata `static chronon3d::TextLayoutCache s_text_cache;` da global file scope (line 1, prima degli `#include`) dentro la `namespace { ... }` anonima — fix `'s_text_cache' was not declared in this scope` alla riga 94 di `make_real_shape`.  Equivalente semantico di file-static (internal linkage).
2. `cmake/Chronon3DTestSuite.cmake::chronon3d_add_test_suite()`: aggiunto `set_target_properties(${ARG_NAME} PROPERTIES UNITY_BUILD OFF)` automaticamente per ogni test executable.  Risolve ODR TU-locali dove più file di test definiscono helpers global-scope con la stessa signature (`fixture_exists`, `skip_if_missing`, `inter_bold`, `make_crossfade_longer_outgoing_doc`, `make_real_shape_for_test`) — unity build li fondeva in un singolo TU causando errori di redefinizione.  Whole-project unity build resta ON per le librerie.
3. `include/chronon3d/render_graph/render_backend.hpp::Result<T,E>`: aggiunto `[[nodiscard]] bool has_value() const noexcept { return ok(); }` per matchare `chronon3d::Result<T,E>::has_value()` e `std::expected::has_value()` API surface — fix `'graph::Result has no member named has_value'` in `tests/text/test_draw_text_run_crossfade_stroke.cpp` linee 252/322/397.

**Build status post-M1.5#7:**
- `chronon3d_text_core` UNCHANGED, compila clean.
- `chronon3d_core_tests`: OOM risolto ✅ ; primo wave rot (3 issues) risolto ✅ ; **secondo wave rot emerso post-UNITY_BUILD_OFF** (vedi FOLLOWUP_TICKETS.md TICKET-011 update): (a) `test_text_run_umbrella_contract.cpp` — `FontLayoutIdentity` field rename rot (commit `1b44e521` M1.5#4 ha rinominato `font_size`→`size` etc.; test ancora usa nomi vecchi); (b) `test_text_preset_descriptor.cpp:1140` — invalid `static_cast<std::string>→bool` su `CHECK_FALSE(d.fixture)`.  Ambedue clustered sotto TICKET-011 secondo-wave PLANNED + auto-AI followup via `tools/test_architectural.sh` execution chain.

Commit: `2db652fb` (5 files: 2 image backend + 1 test rot + 2 build-config).  Code-reviewer verdict: APPROVE (operator== `=default` su `TextLayoutCacheKey` continua a coprire automaticamente `variation_axes`; `set_target_properties UNITY_BUILD OFF` è CMake 3.20+ feature già richiesta da SKIP_UNITY_BUILD_INCLUSION sibling; nessuna regressione ABI per `graph::Result::has_value` aggiunto a compile-time nodelete).  Nessun nuovo singleton/registry/cache.


>>> **M1.5#4 (2026-07-04, post-M1.5#3):** Refactor `src/text/text_run.cpp` (363 LOC) → 3 single-responsibility sub-cpp matching il M1.5#3 umbrella sub-header pattern: `text_run_hash.cpp` (Commit A — entrambi gli overload `hash_text_run_shape(const TextRunShape&)` + `hash_text_run_shape(const TextRunShape&, Frame)`), `text_layout_cache.cpp` (Commit B — `TextLayoutCacheKey::digest` + `Hash::operator()` + `TextLayoutCache::Impl` + class methods + LruCache using-alias), `text_layout_identity.cpp` (Commit C — `TextRunLayout::layout_hash` + `shaping_hash` + `font_layout_identity_of(TextRunLayout)` + aggiunta del campo `std::string variation_axes{};` al struct `TextRunLayout` in `text_run_layout.hpp`, abilitando la fold canonica di `FontLayoutIdentity::variation_axes`). Commit D finale aggiunge `tests/text/test_font_layout_identity_preservation.cpp` che locka i 6 siti (cache-key + layout-hash + shaping-hash + keyframe-compare + prewarm + font-spans) che leggono **dal** canonical `FontLayoutIdentity{resolved_path, family, weight, style, size, features, variation_axes}` definito in `font_engine.hpp:148-160` — zero duplicazione dei 7 campi nei 6 siti; nessun nuovo singleton/registry/cache. **M1.5#4 Commit A (2026-07-04, post-M1.5#3):** Primo commit del refactor. `src/text/text_run_hash.cpp` (NEW, ~250 LOC) — bodies VERBATIM da `text_run.cpp` lines 215-356, no behavioural mutations. `src/text/text_run.cpp` (-140 LOC) — rimosso gli overload hash + `#include <animated_text_document.hpp>` ormai non più usato; residuano `layout_hash` + `shaping_hash` + `digest`/Hash + `TextLayoutCache::Impl` + class methods + `font_layout_identity_of(TextRunLayout)` (target Commit B+C). `src/text/CMakeLists.txt` — add `text_run_hash.cpp` al `chronon3d_text_core` OBJECT library. Validazione: `cmake --build build --target chronon3d_text_core` exit 0 con zero undefined references post-estrazione. Reviewer verdict: APPROVE FOR COMMIT (3 minor non-blocking su XXH detritus + include transitivo + scope-anticipation).

>>> **M1.5#3 (2026-07-04, post-M1.5#2):** Refactor `include/chronon3d/text/text_run.hpp` (409 LOC canonical) a **umbrella header puro** + 5 sub-header single-responsibility sotto `include/chronon3d/text/`: `text_layout_identity.hpp` (FontIdentity + FontLayoutIdentity re-export da `font_engine.hpp`), `text_run_layout.hpp` (TextRunLayout + SharedTextRunLayout + ShapedFontSpan + Bcp47LanguageTag + TextShapingFeatures), `text_layout_cache.hpp` (TextLayoutCacheKey + TextLayoutCacheKeyHash + TextLayoutCache), `text_run_shape.hpp` (TextRunShape + forward-decls AnimatedTextDocument + TextLayoutCache), `text_run_hash.hpp` (hash_text_run_shape overloads). Override feature freeze approvato (cat-3 "Rimozione percorsi legacy — API deprecate" consentito): Fase B3 singleton deprecato `shared_text_layout_cache()` / `reset_shared_text_layout_cache()` non più esposto dall'umbrella (tutti i call site routed via `TextRunShape::cache` o `TextLayoutCache*` espliciti). Zero nuovi classi pubbliche, zero rinominazioni. Verifica: 38 consumer inclusi transit `text_run.hpp` continuano a vedere gli stessi simboli. Refactor committato in `843dc863`. Nuovo test `tests/text/test_text_run_umbrella_contract.cpp` (lock contract cat-2 freeze-compliant: nessuna dipendenza Blend2D/runtime/font engine) locka:
  - §1: ogni sub-header è indipendentemente includibile ed espone i suoi tipi canonici;
  - §2: `text_run.hpp` umbrella espone tutti i 11 tipi canonici via single-include;
  - §3: nessun simbolo singleton deprecato visibile dalla superficie pubblica (Fase B3 sentinel — companion a `tools/check_architecture_boundaries.sh` gate-3, che greps staticamente).
Tutti i lock sono `static_assert`-driven (compile-time). Test aggiunto a `chronon3d_core_tests` come entry NON-gated (eseguito quando TICKET-011 chiude il multi-file LINK rot, non bloccante per il commit corrente). Nessun nuovo singleton/registry/cache.

>> **M1.5#2 (2026-07-03, post-M1.5#1):** `src/text/text_run_driver.cpp` (530 LOC) decomposto in orchestratore breve + 3 moduli single-responsibility in nuova sottodirectory `src/text/driver/`: `text_state_sampler` (target_text + crossfade_target_text + is_in_crossfade_gap), `text_font_state` (compute_effective_font P0-2 compatibile), `text_layout_rebuild` (EffectiveTextState projection + fast_path_match + rebuild_active_side + rebuild_crossfade_slot + prewarm_for_frame). Nuova struct pubblica `EffectiveTextState` (text + FontLayoutIdentity + TextDirection + Bcp47LanguageTag + TextShapingFeatures) introdotta in `include/chronon3d/text/text_run_driver.hpp`; operator== confronta **tutti i 5 campi** in declaration order (cache-key regression lock). Il fast-path in `apply_active_state_to_text_run_shape` ora partecipa a TUTTE le dimensioni di `TextLayoutCacheKey::digest()`, non più solo `source_text + FontLayoutIdentity` come pre-M1.5#2 (P0-2 partial fix). Nuovo test `tests/text/test_effective_text_state.cpp` locka la semantica dei 5 campi + l'uguaglianza + la disuguaglianza + il crossfade lifecycle. La libreria `chronon3d_text_core` compila clean (`LIB_EXIT=0`); gli esistenti `tests/text/test_text_run_driver.cpp` rimangono P0-protected. Nessun nuovo singleton/registry/cache (cache flows via `TextRunShape::cache` o `TextLayoutCache*` esplicito); `process_wide_*` e `shared_text_layout_cache()` invariati in stato `rimosso` (Fase B2+B3 gia' chiusa). ~~Stesso blocker pre-esistente di M1.5#1 blocca `chronon3d_core_tests` LINK step.~~ **Risolto:** commit `ac514fea` ha fixato `projection_mode→optics_mode`. **M1.5#2 (2026-07-04): atomic-fix attempt reverted.** Rot TICKET-011 osservato è multi-file eterogeneo (5+ test file + 1 test header), ben oltre l'originario claim `text_unit_map`. Sub-rot inventariati nel doc `FOLLOWUP_TICKETS.md` colonna TICKET-011 (stato aggiornato PLANNED→PARTIAL). Tentato atomic-commit con 4 str_replace chirurgici (inspector header + 2 callsite in `test_text_preset_registry.cpp` + const-discard D2 + missing include in test_text_material.cpp); tentativo revertato via `git checkout` perché ha rivelato rot parallelo (FontLayoutIdentity field rename in test_effective_text_state.cpp + missing fields in test_text_unit_map_8level.cpp + second const-discard line 213 + doctest.h bad static_cast std::string→bool). Plan: split TICKET-011 in sotto-ticket per file + processare atomicamente per singola responsabilità.
>
> **Camera C1–C7 (2026-07-04):** Projection contract chiuso su `main@eb1ce8e5`. Golden test 6-mode in `tests/scene/camera/golden_projection_test.cpp`. Certificazione runtime chiusa in C9a (`37c03c11`).
>
> **Camera C9a (2026-07-04, `37c03c11`):** Runtime certifier di `tests/scene/camera/golden_projection_test.cpp` — 1 TEST_CASE × 6 SUBCASEs (Zoom, FOV 50°, PhysicalLens ARRI Alexa 35, GateFit::Stretch, GateFit::Overscan, Anamorphic 2×), **71/71 assertion PASS** (toll 1e-3) in `build/tests/chronon3d_scene_tests` post-C9a.  Build fix C9a: `SKIP_UNITY_BUILD_INCLUSION` su `timed_text_document.cpp` + `boundary_resolver/text_unit_map.cpp` per chiudere ODR TU-locali pre-esistenti in `chronon3d_text_core`.  24 fallimenti pre-esistenti emersi in `chronon3d_scene_tests` (esclusi dal certifier) → vedi [TICKET-120](tickets/TICKET-120.md).  Compilazione clean confermata da `tools/check_architecture_boundaries.sh` (gate #1, gate #6).  Camera Production V1 resta PARTIAL.
>
> **Gate #10 sw_sidecar fix (2026-07-04):** `SoftwareRenderer* sw_sidecar` ora threadato da `render_composition_frame` → `render_scene_via_graph` (commit `2ef2b377`). Corregge il culling layer che si verificava quando `sw_renderer` era null dentro `compute_dirty_rect`. Phase 4 ancora FAIL (render black) — richiede ulteriore debugging.
>
> **Fase C2 — Factory unificata (2026-07-03):** `RenderRuntime::create(RuntimeConfig)` → `Result<RenderRuntime, RuntimeBuildError>`.
>
> Documenti canonici (vedi [`docs/DOCUMENTATION_GOVERNANCE.md`](docs/DOCUMENTATION_GOVERNANCE.md) per il contratto):
> - Regole operative / feature freeze: [`AGENTS.md`](../AGENTS.md)
> - Roadmap / futuro: [`docs/ROADMAP.md`](docs/ROADMAP.md)
> - Requisiti permanenti di release: [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md)
> - Indice blocker aperti: [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md)
> - Chiusure recenti: sezione `Recently closed` in FOLLOWUP_TICKETS + [`docs/CHANGELOG.md`](docs/CHANGELOG.md)

## Come leggere gli stati

| Stato     | Significato                                                                  |
| --------- | ---------------------------------------------------------------------------- |
| PASS      | Implementazione verificata contro prova eseguibile osservata sul commit indicato. |
| FAIL      | Comportamento non corretto osservato.                                         |
| PARTIAL   | Implementazione presente ma con limiti o copertura incompleta.               |
| NOT RUN   | Gate / prova non ancora eseguita in macchina-verifica sul commit corrente.    |
| BLOCKED   | Bloccato da un altro ticket o da una condizione esterna.                     |
| PLANNED   | Design presente, implementazione non iniziata o non completata.               |

Un valore `PASS` deve indicare lo SHA e la baseline che lo dimostrano — altrimenti è un falso positivo.

## Stato generale per area

| Area                                            | Stato    | Note sintetiche                                                          |
| ----------------------------------------------- | -------- | ------------------------------------------------------------------------ |
| Render graph compilato                          | PARTIAL  | `chronon3d_render_graph_tests` + `chronon3d_core_tests` LINK rot `Camera2_5D::projection_mode` chiuso in `ac514fea` (carry-over closure M1.5#1 + M1.5#2); TICKET-011 `text_unit_map` build rot ancora aperto (LINK blocker separato, fuori scope M1.5). |
| Software backend                                | PASS     | Gate-3 (I1-I5) tutto verde su `main@775da4d9`. TICKET-077 + TICKET-079 chiusi. |
| Execution scope (precomp + nested)              | NOT RUN  | Lease, child arena e concorrenza da chiudere.                            |
| Text Production V1                              | NOT RUN  | word timing, rich text produttivo, preset, golden da chiudere. **M1.5#7 done**: canonical ownership di `TextRenderResources` ora su `SoftwareSessionResources.text_resources` (WP-3: fuori da engine-generic RenderSession); TICKET-M1.5#7-FULL-SPLIT aperto per split monolitico + caller migration. **M1.5#8 done**: `text_resolver.cpp` (391 LOC monolith) → 6 sub-cpp sotto `src/text/resolver/` (`text_run_resolver` orchestrator + `text_bidi_resolver` + `text_font_resolver` + `text_span_resolver`); **un solo servizio** `FontResolver::resolve(const FontRequest&)` come canonicale (internal, NO pubblico); back-compat `resolve_fallback_fonts` ora `[[deprecated]]`; determinismo lockato con `tests/text/test_text_font_resolver_golden.cpp` (FNV-1a hash + FriBidi fallback env `CHRONON3D_FORCE_NO_FRIBIDI`); nessun secondo resolver introdotto in backend o builder. Pre-existing `chronon3d_core_tests` rot (5+ test files verificati a pristine HEAD — fuori scope M1.5#8) → vedi TICKET-M1.5#8-PRE-EXISTING-ROT.  **M1.5#6 done**:  (1004 LOC monolith) split in 4-stage pipeline orchestrator + 5 single-responsibility sub-cpps sotto  ( Stage 1 +  Stage 2 +  Stage 3 +  Stage 4 +  thin pool wrappers + apply_separable_box_blur + downsample_supersampled); orchestrator ora reads as ; strutture temporanee locked su  (amortized  +  cap);  env-var marker per future parallel raster (current serial); 3 NEW regression tests ( invariants +  FNV-1a hash determinismo +  hash-equality lock); ZERO new public API surface; AGENTS.md Cat-3 freeze-compliant; carry-over nit (effects.cpp use-after-release guard, kBlurTierRadii dedupe vs detail::bucket_radius_for_tier, surface_pool 8-slot rationale inline comment) per tightening pass post-baseline.  **M1.5#9 (1/5) done**: migrate SDK install-boundary consumer `tests/install_consumer/main.cpp` line 115-130 dalla legacy `c3d::TextShape ts` alla canonical modern `c3d::TextRunShapeHandle` via `text_node.shape.set_type(ShapeType::TextRun) + text_node.shape.text_run_shape_handle().value = std::make_shared<TextRunShape>()`; inventory header aggiunto a `src/backends/software/processors/text/software_text_processor.cpp` line 21-65 enumerating steps 2-5 callsites; ZERO new public API; AGENTS.md Cat-3 internal-only; `TICKET-M1.5#9-SEQUENCE` aperto per tracciare steps 2-5 (RenderNodeFactory::text migration → drop legacy registration → delete processor tree → delete rasterizer + migrate legacy tests).  **M1.5#10 (1/4) done**: drop obsolete `TEST_CASE("RichTextLine measures a mixed inline line with text, spacing and symbol")` da `tests/layout/test_design_kit.cpp` (~22 LOC). Il concetto "mixed inline line" (text + space + star chaining su singola `RichTextLine`) non è riproducibile nel modello canonico: `TextDocument` + `TextStyleSpan` descrivono UNA `utf8` con override per-range, mentre `l.text(name, TextSpec)` e `l.star(name, StarParams)` sono SEPARATE primitive in `LayerBuilder` («decorative/anchor gestito dal percorso compilato», M1.5#10 spec). La copertura equivalente vive già in `tests/text/test_text_layout.cpp` + `tests/text/test_text_run_node_execute.cpp` (sub-trees del pipeline canonico). NEW `docs/tickets/TICKET-M1.5#10-SEQUENCE.md` aperto per tracciare steps 2-4 (sweep `RichTextRun` struct usages + sweep `draw_rich_text` callsites + DELETE `rich_text.hpp` + `rich_text.cpp` + drop aggregator include da `design_kit.hpp`). Step 4 finale: eliminazione completa del file come da strict spec utente. ZERO new public API surface (canonical public surface `l.text(name, TextSpec)` + `l.text_run(name, TextRunParams)` invariato). AGENTS.md v0.1 Cat-3 freeze-compliant. |
| Camera Production V1                            | PARTIAL  | Agent3 C1-C7 projection contract + Agent1 Step 4+5 trajectory work + 6/6 CAM-DOC 04 arch-boundary DoD PASS + Cat-1 build-rot commit `8b29a5bf` cleared FocalPx/FrameRate/CameraSession regressions + 1 pre-existing on-main rot remains (size() vs points().size() in camera_program_compiler.cpp:330-335) — out of scope Camera V1 step + Runtime certification + framing/clipping/DOF + legacy migration ancora aperti. |
| SDK C++ installabile                            | PARTIAL  | gate #10 install_consumer: Phase 1-4 PASS post force-rebuild + canary lock (commit `8fd0588e`); canary entry for `chronon3d::sdk::RenderEngine` added in `cmake/Chronon3DCanarySymbols.cmake` to prevent recurrence of the stale-build-cache regression. TICKET-GATE-10-PHASE-4-FIX PARTIAL: Phase 4 black-render root cause (missing `sdk_render_engine.cpp.o` in stale SDK archive) closed; remaining sub-tickets (e.g. certifier-side assertions, full CI matrix) tracked under TICKET-080 + TICKET-120 followups. **Pre-existing build rot TICKET-RUNTIME-ADAPTER-INCOMPLETE-TYPE closed** by dual `TextRenderResources` full-include (commits `5320eb29` + M1.5#8 carry-back to `software_session_resources.hpp`); Build fix = DONE; Certifier (`install_consumer_test.sh` end-to-end 11/11) = PARTIAL pending CI re-run on `main` (pre-fix baseline `aaf70032` 10/11, post-fix atteso 11/11 per revoca feature freeze). |
| SDK cross-language                              | NOT RUN  | C ABI e formato `.chronon` da progettare.                                |
| Sistemi meta (Expressions V2 / V3 tile-first)   | PLANNED  | Expressions V2 OFF di default, non installato. V3 subordinato a V1.      |
| Render runtime (session + caches)               | PASS     | P0 #B1: ImageCache moved to RenderRuntime. P1 #3: `RenderSession::layout_cache` added. |
| Render engine construction                      | PASS     | P0 #C2: Impl ctor unified, `optional<path>`. attach_backend() deprecated. |
| Composition pipeline                            | PASS     | P0 #C3: canonical pipeline documented. render_composition_frame canonical. |
| Text pre-compilation (CompiledTextRun)          | PLANNED  | P0 #C1: documented in text_run.hpp. Blocked by feature freeze. |

## Camera V1 — DoD (6 CAM-DOC 04 architecture-boundary checks)

Lock canonico per la DoD Camera V1 verificato da `tools/check_camera_architecture.sh` (gate #6 dell'11-gate audit).  **6/6 PASS** su `main@3a5eb193` (snapshot 2026-07-04):

| # | Check                                                                                              | Stato | Verifica (`tools/check_camera_architecture.sh`) |
|---|-----------------------------------------------------------------------------------------------------|-------|---------------------------------------------------|
| 1 | AnimatedCamera2_5D RETIRED                                                                          | PASS  | `grep` su `include/chronon3d/{scene,backends,runtime}/...` nessun reference residuo. |
| 2 | CameraRig authoring RETIRED                                                                          | PASS  | `camera_rig.hpp` non presente in alcun `target_sources`. |
| 3 | SceneBuilder::animated_camera() RETIRED                                                              | PASS  | nessuna chiamata a `scene_builder::animated_camera` in `src/**` o `content/**`. |
| 4 | SceneBuilder::camera_rig() RETIRED                                                                   | PASS  | nessuna chiamata a `scene_builder::camera_rig` in `src/**` o `content/**`. |
| 5 | tan(fov) focal formulas canonical                                                                 | PASS  | un'unica site in `include/chronon3d/scene/math/camera_math.hpp`; nessuna duplicazione. |
| 6 | compile_camera() call-site policy                                                                    | PASS  | chiamate solo da `runtime_adapter.cpp` (orchestrator); non abusato da nodes/backend. |

**Source of truth:** `tools/check_camera_architecture.sh` (6 grep-comparison checks) — output `6/6 PASS` catturato dal forensic run Step 6 (`docs/audits/2026-07-04-step6-camera-gates.md` quando scritto).

**Note su Camera V1 vs RELEASE_GATE.md:** il canonico `docs/RELEASE_GATE.md` lista 7 condizioni ("all new compositions use CameraDescriptor or CameraProgram" / "CameraSession owned by render job" / "no per-frame lookup/compile" / "orientation/projection single math contract" / "compiled tests link + run in CI" / "legacy adapters parity + removal phase" / "external SDK builds + uses compiled camera"). Le 6 CAM-DOC 04 checks sopra sono la colonna architetturale della DoD; le rimanenti sono runtime/CI/SDK invariants tracciate nel forensic run Step 6 (gate b/d Pass + gate c FAIL su pre-existing rot fuori scope Camera V1 step).

## Blocker correnti per baseline verde (top 10 attivi)

Per le specifiche di accettazione complete di ogni ticket vedi [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) (canonical).
Per la storia delle chiusure vedi `Recently closed` in `FOLLOWUP_TICKETS.md` + [`docs/CHANGELOG.md`](docs/CHANGELOG.md).

| ID          | Area                                                                  | Stato    | Blocca                              |
| ----------- | --------------------------------------------------------------------- | -------- | ----------------------------------- |

| TICKET-036  | chronon3d_camera_architecture_gate P0                                 | PLANNED  | arch-boundary gate 5/6              |
| TICKET-044  | arch_boundaries_selftest hardcoded paths                              | PLANNED  | arch-boundary gate 5                |
| TICKET-046  | filename drift stale references                                       | PLANNED  | arch-boundary gate 5                |
| TICKET-011  | pre-existing mainline build rot                                       | PLANNED  | arch-boundary gate 1–8              |
| TICKET-005  | post-cascade cleanup                                                  | PARTIAL  | arch-completeness gate 5            |
| TICKET-118  | `SoftwareBackend::draw_node` real impl + dummy `TextRunProcessor` drop | Done     | cat-3 fake-success closure           |
| TICKET-119  | `SoftwareBackend` m_owner back-pointer removal + internal bridge       | Done     | cat-3 arch-debt closure              |
| P0 #1–#6  | RenderGraph error propagation + immutability (Fase A)         | Done     | A1-A6 tutti chiusi; m_backend_warned rimosso.   |
| P0 #B1    | ImageCache da singleton a RenderRuntime (Fase B)               | Done     | `1fcc9100`; instance() rimosso, thread::detach eliminato. |
| P0 #C2    | Unify RenderEngine::Impl constructors + deprecate attach_backend | Done     | `d8a228f7`; 2 costruttori → 1 con `optional<path>`. |
| P0 #C3    | Canonical composition pipeline documentation (Fase C)          | Done     | `3b4dbdc6`; Definition→Compiler→Evaluator→GraphCompiler→Executor doc. |
| P0 #C1    | CompiledTextRun pre-compilation (Fase C)                       | PLANNED  | Doc-only; blocked by feature freeze.             |
| TICKET-022  | camera double look-at compiled path                                   | PARTIAL  | arch-boundary gate 5/6              |
| TICKET-064  | §9 ExecutionScope — ScopeError/ScopeErrorCode structured error model | PARTIAL  | arch-boundary gate 5                |
| TICKET-120  | C9a (`37c03c11`) — 24 fallimenti pre-esistenti in `chronon3d_scene_tests` (incl. `TICKET-034D` CameraDescriptor fingerprint SIGABRT, `TICKET-035` anamorphic_squeeze wrong-asset `test_camera_projection_contract.cpp:572`); emersi dopo C9a abilita build clean con `SKIP_UNITY_BUILD_INCLUSION` su 2 file di `chronon3d_text_core` — **Cat-1 progress: 4/24 chiusi** (commit `5985224c` Unity build deconflict + commit `7232722f` TICKET-035 anamorphic_squeeze fix; **17/24 failure rimangono** post-build-fix) | PARTIAL  | certificazione Camera V1 end-to-end |
| TICKET-051  | A4.3 per-preset visual diagnostic                                     | PLANNED  | A4.3 visual gate                    |

> Ticket chiusi di recente: vedi `Recently closed` in `FOLLOWUP_TICKETS.md` + [`docs/CHANGELOG.md`](docs/CHANGELOG.md).

## Certificazione corrente

Ultima baseline macchina-verificata: `main@aaf70032` — **10/11 PASS** (carry-over da 2026-07-04; nessuna baseline certificata >10/11 a `c73f7493`).
Audit corrente: `main@c73f7493` — **9/11 PASS** (post GATE-MNT-01-EXT + gate-1+#9 SSoT POSIX regex).  **9/11 NON è 11/11: feature freeze ancora attivo.**  Pre-audit chain: 10/11 aaf70032 → 9/11 16319557 → 7/11 9ecb4879 → 7/11 eb8e3a24 → 9/11 c73f7493.
  - gate #1 + #9 SDK public-deps SSoT wiring (Check 16) — **FLIPPED to PASS at `a5ee07e7`** (POSIX regex migration `\s`→`[[:space:]]`, `\S`→`[^[:space:]]` per mawk-compat in `tools/check_architecture_boundaries.sh` Check 16; pre-existing bug su sistemi con `mawk` default).
  - gate #10 install_consumer — FAIL (carry-over rot da `9ecb4879`; failure-mode a `c73f7493` = `ninja subcommand failure during compilation of highway_*_kernels.cpp.o` in `chronon3d_backend_software` target; richiede independent re-run per disambiguare transient noise vs durable build rot; TICKET-GATE-10-PHASE-4-FIX da aprire).
  - gate #11 backend sanitization `printf` in `software_grid_background_processor.cpp:23` — FAIL (pre-esistente, intro `b62ef4429`; TICKET-GATE-11-PRINTF-FIX da aprire).
Nessuna baseline certificata oltre `aaf70032`.
Per la revoca del **feature freeze** (vedi `AGENTS.md`) è richiesto **11/11 PASS sullo stesso commit**; 9/11 NON è sufficiente.
Storico baseline: [`docs/baselines/`](docs/baselines/) (file immutabili per SHA, una sola baseline per commit).

## Chiusure recenti (P1)

| P1 | Area | Commit | Stato |
|----|------|--------|-------|
| P1 #10 | Frame rate hardcoded | `6df9b429` + `560750e3` | ✅ DONE |
| P1 #12 | CMake ar merge + include_private | `59b2439f` | ✅ DONE |
| P1 #7 | Asset resolver globale | — | PLANNED |
| P1 #8 | Text renderer monolite | — | PLANNED |
| P1 #9 | RenderRuntime service locator | — | PLANNED |
| P1 #11 | Timeline percorsi multipli | — | PLANNED |

## Gate audit snapshot — `main@1078ab46` (2026-07-04, 11-gate audit)

| # | Gate                                        | Esito      | Note                                                                       |
|---|---------------------------------------------|------------|----------------------------------------------------------------------------|
| 1 | `check_architecture_boundaries.sh`          | ✅ PASS    | 15/15 check, 2 advisory (check 12 CMake registry, check 13 vcpkg dep).   |
| 2 | `check_architecture_boundaries_selftest.sh` | ✅ PASS    | 15/15 assertions.                                                          |
| 3 | `check_software_renderer_boundary.sh`       | ✅ PASS    | I1-I5 tutti rispettati.                                                    |
| 4 | `check_gitignored_dirs.sh`                  | ✅ PASS    | Directory pulite.                                                          |
| 5 | `audit_software_renderer.sh`                | ✅ PASS    | Report generato, exit 0.                                                   |
| 6 | `check_camera_architecture.sh`              | ✅ PASS    | 6/6 check.                                                                 |
| 7 | `check_doc_sync.sh`                         | ✅ PASS    | 0 hard failures, 0 warnings.                                               |
| 8 | `check_filename_drift.sh`                   | ✅ PASS    | 82 drift findings.                                                         |
| 9 | `test_architectural.sh`                     | ✅ PASS    | Static architecture-level rot: 0. Exit 0.                                  |
| 10 | `install_consumer_test.sh`                  | ✅ PASS    | Phase 1-4 PASS post force-rebuild of `chronon3d_sdk_impl` + canary entry for `chronon3d::sdk::RenderEngine` in `cmake/Chronon3DCanarySymbols.cmake` (commit `8fd0588e`). Consumer renders `[BOUNDARY-OK] 230400/230400 pixels >5/255`. |
| 11 | `check_backend_sanitization.py`             | ✅ PASS    | Tutti i check passati.                                                     |

**Totale: 11/11 PASS.** Gate #10 Phase 4 black-render root cause closed (missing `sdk_render_engine.cpp.o` in stale SDK archive; force-rebuild restored it; canary entry locks the regression).

## Prossimo passo operativo

1. **Certificare una nuova baseline macchina-verificata** su `main@8fd0588e` (o HEAD post-doc-sync): 11/11 PASS atteso; promuovere `docs/baselines/main-<sha>-baseline.md` + revocare formalmente il feature freeze.
2. **TICKET-120 burn-down (24 pre-esistenti test failures):** 4/24 chiusi; 17/24 ancora aperti. Continuare con la prossima categoria di test failure (es. anamorphic focal_x/y spherical ratio bug, frame budget overrun, etc.).
3. **P1 backlog (post-baseline):** TICKET-P1-07 (asset resolver globale), TICKET-P1-08 (text renderer monolite), TICKET-P1-09 (RenderRuntime service locator), TICKET-P1-11 (timeline percorsi multipli).

## Hygiene

Main-sync hygiene (`main` come single-source-of-truth; tutto converge in `main`) è enforced da **tre pezzi coordinati** su ogni push workflow:

| Pezzo                    | Scope                                                                                       | Strumento canonico                                                |
|--------------------------|---------------------------------------------------------------------------------------------|-------------------------------------------------------------------|
| **Branch-level rebase** (read-side) | `git pull` unpulled su `branch.<name>.rebase = true` esegue rebase invece di merge (history lineare)            | `git config branch.<name>.rebase true` (per-repo local, **NON** `--global`) |
| **GATE-MNT-01 wrap-push** (push-side) | Auto-FF unidirezionale + invocazione `check_main_clean` come drop-in per `git push`                            | [`tools/wrap_push.sh`](tools/wrap_push.sh) (portabile, tracked)   |
| **Clean-tree check** (gate) | Fail-on-dirty su divergenza HEAD/origin/main + working-tree dirty                              | [`tools/check_main_clean.sh`](tools/check_main_clean.sh)          |

Le tre componenti sono complementari (e formano una triade): il read-side rebase previene i merge-commit locali, il wrapper canonico applica il gate al push, il gate fallisce su dirty → tutte insieme (e solo tutte insieme) garantiscono che `git log main` resti lineare e che l'HEAD pubblicato sia sempre verificabile.

## Link canonici

- [`docs/ROADMAP.md`](docs/ROADMAP.md) — milestone prodotto (vedi TICKET-GATE-7-R1: Coverage src/runtime/** da aggiungere).
- [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md) — requisiti permanenti di release.
- [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) — indice blocker aperti (canonical).
- [`docs/CHANGELOG.md`](docs/CHANGELOG.md) — chiusure recenti.
- [`docs/baselines/main-aaf70032-baseline.md`](docs/baselines/main-aaf70032-baseline.md) — ultima baseline macchina-verificata (10/11 PASS).
- [`docs/baselines/main-9ecb4879-baseline.md`](docs/baselines/main-9ecb4879-baseline.md) — baseline di regressione (7/11 PASS, post-A3 cluster su `chore(cmake): UNIFIED-VCPKG-TOOLCHAIN`).
- [`docs/baselines/main-eb8e3a24-baseline.md`](docs/baselines/main-eb8e3a24-baseline.md) — attestazione di stabilità (7/11 PASS stable, doc-only diff su 9ecb4879).
- [`docs/baselines/main-21103265-baseline.md`](docs/baselines/main-21103265-baseline.md) — baseline precedente (9/11 PASS).
- [`reports/perf/main-73a2aa9b-gates.json`](../reports/perf/main-73a2aa9b-gates.json) — log macchina-verificato della 11-gate run su `main@73a2aa9b` (10/11 PASS, gate #10 `install_consumer_test.sh` FAIL).
- [`docs/DOCUMENTATION_GOVERNANCE.md`](docs/DOCUMENTATION_GOVERNANCE.md) — contratto documentale (single-source-of-truth).
- [`docs/ARCHIVE/`](docs/ARCHIVE/) — materiale storico (non operativo; nessun riferimento operativo consentito).

## Gate audit snapshot — `main@876a14ac` (2026-07-03, post-Fase A + B2+B3 completati)

| # | Gate                                        | Esito      | Note                                                                       |
|---|---------------------------------------------|------------|----------------------------------------------------------------------------|
| 1 | `check_architecture_boundaries.sh`          | ✅ PASS    | 16/16 check.                                                              |
| 2 | `check_architecture_boundaries_selftest.sh` | ✅ PASS    | 15/15 assertions.                                                          |
| 3 | `check_software_renderer_boundary.sh`       | ✅ PASS    | I1-I5 tutti rispettati. Header LOC=200 ≤ 200.                              |
| 4 | `check_gitignored_dirs.sh`                  | ✅ PASS    | 31 directory, tutte pulite. 85 entry totali.                               |
| 5 | `audit_software_renderer.sh`                | ✅ PASS    | Report generato, exit 0.                                                   |
| 6 | `check_camera_architecture.sh`              | ✅ PASS    | 6/6 check. CAM-DOC 04 boundary ok.                                        |
| 7 | `check_doc_sync.sh`                         | ✅ PASS    | 0 hard failures, 0 warnings.                                               |
| 8 | `check_filename_drift.sh`                   | ⚠️ PASS*   | warn-mode; 170 drift warning (stabile).                                    |
| 9 | `test_architectural.sh`                     | ✅ PASS    | Static architecture-level rot: 0. Exit 0.                                  |
| 10 | `install_consumer_test.sh`                | ❓ NOT RUN  | Timeout 120s (build 107/341). Regressione infrastrutturale, non di codice. |
| 11 | `check_backend_sanitization.py`            | ✅ PASS    | Tutti i check passati.                                                     |

**Totale: 9/10 PASS + 1 PASS*** (warn-mode) + **1 NOT RUN** (gate #10 infrastrutturale).

## Gate audit snapshot — `main@e8623a8a` (2026-07-03, post-Fase A — P0 completati)

| # | Gate                                        | Esito      | Note                                                                       |
|---|---------------------------------------------|------------|----------------------------------------------------------------------------|
| 1 | `check_architecture_boundaries.sh`          | ✅ PASS    | 16/16 check.                                                              |
| 2 | `check_architecture_boundaries_selftest.sh` | ✅ PASS    | 15/15 assertions.                                                          |
| 3 | `check_software_renderer_boundary.sh`       | ✅ PASS    | Tutti gli invarianti rispettati.                                           |
| 4 | `check_gitignored_dirs.sh`                  | ✅ PASS    | 31 directory, tutte pulite.                                                |
| 5 | `audit_software_renderer.sh`                | ✅ PASS    | Report generato, exit 0.                                                   |
| 6 | `check_camera_architecture.sh`              | ✅ PASS    | 6/6 check.                                                                 |
| 7 | `check_doc_sync.sh`                         | ✅ PASS    | 0 hard failures, 0 warnings.                                               |
| 8 | `check_filename_drift.sh`                   | ⚠️ PASS*   | warn-mode; 170 drift warning (↑ da 73 — audit hasher header).             |
| 9 | `test_architectural.sh`                     | ✅ PASS    | Static architecture-level rot: 0.                                          |
| 10 | `install_consumer_test.sh`                | ❓ NOT RUN  | Timeout (30s); richiede build completa. Bloccato da infra (disk quota PCH). |
| 11 | `check_backend_sanitization.py`            | ✅ PASS    | Tutti i check passati.                                                     |

**Totale: 10/10 verificati, 1 NOT RUN (gate #10).** 9/10 PASS + 1 PASS* (warn-mode).

## Gate audit snapshot — `main@a53a8d25` (2026-07-03, audit fresco — HISTORICAL)

| # | Gate                                        | Esito      | Note                                                                       |
|---|---------------------------------------------|------------|----------------------------------------------------------------------------|
| 1 | `check_architecture_boundaries.sh`          | ✅ PASS    | 14/15 check, 3 advisory (10, 12, 13 — non-blocking).                      |
| 2 | `check_architecture_boundaries_selftest.sh` | ✅ PASS    | 15/15 assertions.                                                          |
| 3 | `check_software_renderer_boundary.sh`       | ❌ FAIL    | I2: `software_renderer.hpp` LOC=203 > 200.                                 |
| 4 | `check_gitignored_dirs.sh`                  | ✅ PASS    | 31 directory, tutte pulite.                                                |
| 5 | `audit_software_renderer.sh`                | ✅ PASS    | Report generato.                                                           |
| 6 | `check_camera_architecture.sh`              | ✅ PASS    | 6/6 check.                                                                 |
| 7 | `check_doc_sync.sh`                         | ❌ FAIL    | R0: commit `a53a8d25` ha toccato `docs/ARCHIVE/CHANGELOG_2026_H1.md`.     |
| 8 | `check_filename_drift.sh`                   | ⚠️ PASS*   | warn-mode; 73 drift warning (↓ da 155).                                    |
| 9 | `test_architectural.sh`                     | ✅ PASS    | Static architecture-level rot: 0.                                          |
| 10 | `install_consumer_test.sh`                | ❌ FAIL    | "Disk quota exceeded" su PCH (224MB/file, ccache 5.0G); unit 28/340. Regressione infrastrutturale. |
| 11 | `check_backend_sanitization.py`            | ✅ PASS    | Tutti i check passati.                                                     |

**Totale: 8/11 PASS** — 3 regressioni da chiudere: gate #3 (I2 LOC), gate #7 (R0 ARCHIVE), gate #10 (disk quota).

## Gate audit snapshot — `main@c40ba16f` (2026-07-02, post-rebase + runtime-fix — HISTORICAL)

| # | Gate                                        | Esito      | Note                                                                       |
|---|---------------------------------------------|------------|----------------------------------------------------------------------------|
| 1 | `check_architecture_boundaries.sh`          | ✅ PASS    | 14/15 check, 1 advisory (TICKET-042, non blocker).                        |
| 2 | `check_architecture_boundaries_selftest.sh` | ✅ PASS    | 15 assertions.                                                             |
| 3 | `check_software_renderer_boundary.sh`       | ✅ PASS    | I1-I5 tutti rispettati.                                                    |
| 4 | `check_gitignored_dirs.sh`                  | ✅ PASS    | Fissato da `f6f700b1` (`reports/perf/` in `.gitignore`).                 |
| 5 | `audit_software_renderer.sh`                | ✅ PASS    |                                                                            |
| 6 | `check_camera_architecture.sh`              | ✅ PASS    |                                                                            |
| 7 | `check_doc_sync.sh`                         | ✅ PASS    |                                                                            |
| 8 | `check_filename_drift.sh`                   | ⚠️ PASS*   | warn-mode; 155 drift warning.                                              |
| 9 | `test_architectural.sh`                     | ✅ PASS    | Static architecture-level rot: 0.                                          |
| 10 | `install_consumer_test.sh`                | ⚠️ FIX PENDING (triple) | (a) `21b9fb5d` cmake case-fix + 44 transitive headers; (b) `75035f2b` runtime default-arg chain su `RenderPipeline::debug_graph`; (c) TICKET-Phase4-BlurTierRadii constexpr array `{{0,2,7,13,20}}` per il blur-tables. |
| 11 | `check_backend_sanitization.py`            | ✅ PASS    |                                                                            |

**Totale storico: 10/11 PASS** osservato su `c40ba16f`.

_Limite raccomandato: 150 righe (vedi `DOCUMENTATION_GOVERNANCE.md` DoD §10)._

## Gate audit snapshot — `main@efd841f0` (HISTORICAL, 2026-07-02)

> Historical reference (baseline-stale; current HEAD = `fe25f6bc`, 15 commit ahead).
> Source of truth: [`reports/perf/main-efd841f0-gates.json`](../reports/perf/main-efd841f0-gates.json) (schema `chronon3d.gates.v1`).
> Per-gate tee logs: [`reports/perf/main-efd841f0-tee/`](../reports/perf/main-efd841f0-tee/).
> _Source of truth; full 11-row table is in the JSON, non duplicato qui per brevità._

| Gate #10                              | `install_consumer_test.sh` | ❌ FAIL (`regression_type: infra-setup`, 0s elapsed) |
|---------------------------------------|----------------------------|------------------------------------------------------|

Diagnosi: `Could not find toolchain file: …/vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake` (env precond `VCPKG_ROOT` non impostata).  **Distinct da TICKET-111** OPP-side text rot.  Forward-fix: `TICKET-install-consumer-vcpkg-bootstrap` ([`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md)).  Nessuna regressione di codice osservata; tutti gli altri 10 gates PASS (vedi JSON per i dettagli per-gate).
