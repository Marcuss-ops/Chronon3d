# Chronon3D — Current Status

> **Snapshot:** `main@21103265` — 2026-06-30. Linux-only.
>
> Ultima baseline macchina-verificata: `main@21103265` (9/11 — NON VERDE per freeze; vedi [`baselines/main-21103265-baseline.md`](baselines/main-21103265-baseline.md)).
> **Baseline sull'HEAD corrente: 9/11 PASS @ `21103265`** (registrata in [`baselines/main-21103265-baseline.md`](baselines/main-21103265-baseline.md)); feature freeze ancora attivo — AGENTS.md richiede 11/11 PASS sullo stesso commit per promuovere a `CERTIFICATA @ 21103265`.
>
> Questo è l'unico documento canonico per lo stato presente del progetto.
> Per il futuro vedi [`ROADMAP.md`](ROADMAP.md).
> Per i requisiti di release vedi [`RELEASE_GATE.md`](RELEASE_GATE.md).
> Per i ticket vedi [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md).

> **🚧 SPRINT SUSPENDED (`main@30f6c876`, 2026-06-30)** — il piano migrazione canonico
> [`docs/TEXT_SELECTOR_SINGLE_PIPELINE_PLAN.md`](TEXT_SELECTOR_SINGLE_PIPELINE_PLAN.md) (12 ATOMI) è
> sospeso per `AGENTS.md` v0.1 feature-freeze (VIETATO: nuovi `#include` in `include/chronon3d/`,
> nuove classi pubbliche, qualsiasi modifica che espanda la superficie API). TICKET-066 in
> [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) traccia la sospensione. Ripresa solo
> dopo 11/11 gate baseline verde certificata su un singolo commit.

## Preset CMake attivi

Configurazione osservata in [`CMakePresets.json`](../CMakePresets.json) al commit corrente:

- `linux-dev` — preset di sviluppo locale (default).
- `linux-asan` — instrumentazione ASAN + UBSAN.
- `linux-release-validation` — preset consumato da `.github/workflows/gates-full-validation.yml`.
- `linux-experimental-validation` — preset per la catena `experimental/expressions/` (opt-in).
- `linux-ci` — preset consumato da `tools/install_consumer_test.sh`.
- Varianti di profiling (`true`/`false`) — vedi file per la lista completa.

Build/test preset default: `jobs: 16`.

## CI gate osservati (lettura `.github/workflows/*.yml`)

### Gate bloccanti (failure = PR bloccato)

- `.github/workflows/gates.yml` — 6 cheap gates (`core-build`, `sdk-build`, `public-header-check`, `install+consumer`, arch-boundary, sw-renderer-boundary). Trigger: push + pull_request su `main`.
- `.github/workflows/ci.yml` — matrix Release + Debug + lean + scene/camera. Trigger: push + PR su `main`.
- `.github/workflows/gates-full-validation.yml` — full-validation con `paths:` filter (SDK/src/cmake). Trigger: push + PR.
- `.github/workflows/validation.yml` — validate secondario. Trigger: push + PR su `main`.

### Gate advisory (osservazione; non bloccante)

- `.github/workflows/visual_ci.yml` — visual regression con `continue-on-error: true` su più job (`diff-engine-test`, `visual-regression-test`).
- `.github/workflows/nightly-visual.yml` — schedule cron nightly + `workflow_dispatch`.
- `.github/workflows/profile-envelope.yml` — schedule weekly (lunedì 04:00 UTC) + dispatch.
- Step `renderer-boundary-observation` dentro `.github/workflows/gates.yml` — osservazione esplicita non bloccante (annotata come tale nel workflow) del confine renderer/software.

## Packaging SDK

[`tools/install_consumer_test.sh`](../tools/install_consumer_test.sh) — script end-to-end (7339 bytes, eseguibile). Configura + build + installa SDK in prefix temporaneo + richiede preset `linux-ci` + `cmake >= 3.25`.

- **Stato corrente**: `NOT RUN` per questo run di Step 5 (richiede macchina-verifica dedicata sul commit candidato). Syntax check (`bash -n tools/install_consumer_test.sh`) deferred a macchina-verifica.
- **Cosa serve per elevare a `PASS`**: run macchina-verificato di `tools/install_consumer_test.sh` su HEAD + exit `0` + log di configure + build su preset `linux-ci`.

## Install consumer

[`tests/install_consumer/`](../tests/install_consumer/) progetto consumer esterno che linka `Chronon3D::SDK` contro il prefix installato da `tools/install_consumer_test.sh`. Coperto dallo stesso script (vedi `## Packaging SDK` sopra).

- **Stato corrente**: `NOT RUN` (stessa giustificazione: macchina-verifica richiesta su commit candidato).
- **Cosa serve per elevare a `PASS`**: log del build del consumer in step separato + link riuscito + render di una composizione reale (testo + camera → PNG) fuori-tree.

## Come leggere gli stati

| Stato | Significato |
|---|---|
| ✅ Verificato | Implementato e coperto da una prova eseguibile osservata. |
| 🟡 Implementato/parziale | Codice presente, ma con limiti, migrazione incompleta o test bloccati. |
| 🔴 Bloccante | Impedisce di dichiarare stabile il sottosistema o la release. |
| 🔵 Pianificato | Design o roadmap presenti, implementazione non completata. |

## Stato generale

Chronon3D è un motore C++20 headless, CPU-first e code-first per motion graphics.
È avanzato ma **pre-stabile**.

| Area | Stato (osservato) | Note |
|---|---|---|
| Render graph compilato | 🟡 NOT RUN | Baseline e determinismo scheduler da verificare sullo stesso commit. |
| Software backend | 🟡 NOT RUN | Confine rifattorizzato (Agent-1 #49); gate e full validation da osservare insieme. |
| Precomp / execution scope | 🔴 NOT RUN | Nested execution, lease, child arena e concorrenza da chiudere. |
| Text Production V1 | 🟡 NOT RUN | word timing, rich text produttivo, preset e golden da chiudere. |
| Camera Production V1 | 🟡 NOT RUN | TICKET-029 risolto (link sbloccato); migrazione legacy e feature path/framing/ottica aperte. |
| SDK C++ installabile | 🟡 NOT RUN | consumer di rendering reale con testo+camera→PNG in fase di certificazione. |
| SDK cross-language | 🔵 NOT RUN | C ABI e formato `.chronon` da progettare. |
| Expressions V2 | 🧪 Sperimentale | OFF di default, non installato, non in SDK. |
| V3 tile-first | 🔵 Pianificato | Non prima della chiusura baseline e percorsi V1. |

Lo stato è osservabile (`PASS` / `FAIL` / `PARTIAL` / `NOT RUN`): mai una
stima di copertura. Un valore `PASS` senza output simultaneo sul commit
corrente è un falso positivo e va corretto.

## Fondazioni da preservare

- `RenderGraph → FrameGraphCompiler → CompiledFrameGraph → GraphExecutor`.
- Executor su `RenderGraph` grezzo ritirati.
- Scheduler esplicito e ID/hash deterministici.
- `RenderRuntime` proprietario dell'infrastruttura engine-lifetime.
- Stato per-sessione per history, cache e camera.
- `AssetResolver` tipizzato e runtime-owned.
- Registrazione esplicita tramite `ExtensionModule` e `ExtensionContext`.
- Un solo registry CMake per build, aggregate archive, install ed export.
- `Chronon3D::SDK` come unico target pubblico documentato.
- `experimental/` escluso dallo SDK stabile.
- Camera canonica: `CameraDescriptor → compile_camera() → CameraProgram`.
- Testo canonico: `TextDocument/TextSpec → layout → animator stack → renderer`.

## Testo

### Presente

- FreeType, HarfBuzz e FriBidi.
- `TextSpec`, `TextRunSpec`, `TextDocument`, span e paragrafi.
- Layout, wrap, overflow, auto-fit, gradienti, stroke e shadow.
- Animator per glifo e selector per glifo/grapheme/parola/riga.
- Sample time sub-frame.
- Text-on-path da consolidare.
- Registry canonico dei preset e sentinel iniziali.

### Parziale o aperto

- 🟢 **FASE 1 pipeline text core stabilizzata** (chiusa per docs in [FOLLOWUP_TICKETS.md §TICKET-076](FOLLOWUP_TICKETS.md)): `compile_text_layout()` come unico entrypoint canonico (commit `5ab4be48`), `TextUnitMap` non-empty guarantee per ogni layout (incluso empty paragraph / early-exit `EmptySource` / `MalformedLayout` / `MissingFont`), `GlyphAtlasCache<LruCache<GlyphAtlasKey,GlyphAtlasEntry,GlyphAtlasKeyHash>>` con preservazione completa dei 5 metadata (commit `facf0453`), rifiuto strutturato paragraph multi-font via `TextLayoutErrorKind::UnsupportedMultiFontRun` (commit `e4341f5f`).
- 🟢 **FASE 3 std::call_once eliminato (Cat-3)** (chiusa per docs in [FOLLOWUP_TICKETS.md §TICKET-077](FOLLOWUP_TICKETS.md)): 6 atomic commit eliminano `std::call_once` dai 6 file di init-time config (`text_rasterizer_cache.cpp`, `glyph_atlas.cpp`, `pip.cpp`, `image_cache.cpp`, `persistent_framebuffer_store.cpp`, `cache_policy.cpp`) preservando la semantica first-call-wins (`std::atomic<bool> s_X_set` + `compare_exchange_strong(expected, true, acq_rel, relaxed)`; `pip.cpp` riusa flag bool `s_pip_mode_set` pre-esistente). Pattern AGENTS.md-aligned. Zero espansione `include/chronon3d/` API = Cat-3 "Rimozione percorsi legacy".
- 🟢 **FASE 2 unicode extraction — utf8_decoder + whitespace consolidati (Cat-3)** (chiusa per docs in [FOLLOWUP_TICKETS.md §TICKET-080](FOLLOWUP_TICKETS.md)): nuovi `src/text/unicode/{utf8_decoder,whitespace}.{hpp,cpp}` consolidano 4 inline impl duplicate (anonymous `decode_utf8` + 3 separate `is_unicode_whitespace` lists). I callers (`src/text/text_unit_map.cpp` byte→codepoint pass + WB5a glyph→word loop + cp_at_idx precompute; `src/text/internal/composer_helpers.hpp` `decode_codepoint_at`/`is_whitespace_codepoint` forwarders) re-routed al canonical. Behaviour change per composer `is_whitespace_codepoint(0x2028/0x2029/0xFEFF)` → superset (più corretto per UAX#29 WB5a). Zero espansione `include/chronon3d/` = Cat-3 freeze-aligned. FASE 2 residue (grapheme_break + word_break extraction, selector/animator migration a 8-level canonical, `Result<TextUnitMap,TextUnitMapError>`, shaped-glyph count fix, incremental RI) deferred post-baseline-verde per violare freeze v0.1 (vedi [FOLLOWUP_TICKETS.md §TICKET-081a/b/082/083/084/085](FOLLOWUP_TICKETS.md)).
- 🟢 **TICKET-100 — eliminate legacy materialize_text_run_shape pipeline (cat-3, atomic on `main`)** (chiusa per docs in [FOLLOWUP_TICKETS.md §TICKET-100](FOLLOWUP_TICKETS.md)): le 5 fasi inline del materializer legacy (cache lookup + `shape_text()` + `resolve_placed_glyph_run()` + manual `TextRunLayout` field-by-field assignment + cache store) sono ora consolidate in un helper anonimo `compile_or_cache_layout()` che chiama `compile_text_layout()` internamente. La cache_key legacy (9 campi, inclusi direction + language) è preservata BIT-IDENTICAL (cache hit/miss match pre-refactor contract); il cache dance interno di compile_text_layout è BYPASSATO via `services.cache = nullptr` per evitare che i suoi default hardcoded (`direction=Auto`, `language=""`) leakino nella cache key. L'helper post-override `text_layout->direction` + `text_layout->language` per matchare `params.direction` / `params.language`. ZERO espansione API pubblica (`include/chronon3d/` intatto) — cat-3 freeze-compliant per AGENTS.md v0.1. Side-benefit (chiude review P0 #6 gratis): `text_layout->font = primary_font` (FontSpec 5-campi pieno) sostituisce il legacy `shaped_font = {4 campi}` (che lasciava `font.font_size` al default 0.0f / 72.0f). 4 nuovi TEST_CASE identity in `tests/text/test_compile_text_layout_identity.cpp` (registrato in `tests/core_tests.cmake` `CORE_BLEND2D_TESTS`): (1) materialize ≡ compile_text_layout diretto su input equivalente (parity source_text / font_size / font.font_size / glyph_count / units.size); (2) cache-hit returns same shared_ptr (cache_key preservation); (3) direction+language override post-compile (RTL/`ar` + LTR/`en` round-trip); (4) `text_layout->font.font_size` mirrors `params.text.font.font_size` (review P0 #6 closure). Co-update rule rispettata: TICKET-100 row in open-blockers table + CURRENT_STATUS.md § Testo + CHANGELOG.md entry aggiornati nello stesso commit.
- 🔵 FASE 3 `text_rasterizer_render.cpp` routing via `TextRenderResources` deferred post-baseline-verde: rimozione del singleton `Blend2DResources` richiede firma `rasterize_text_to_bl_image` (in `include/chronon3d/backends/text/text_rasterizer_utils.hpp:52`) di aggiungere `TextRenderResources&` — VIOLA freeze v0.1. Vedi [FOLLOWUP_TICKETS.md §TICKET-078](FOLLOWUP_TICKETS.md).
- 🔵 FASE 3 umbrella `class TextServices` + `FontStore` + `TextRasterCache` + `RenderRuntime::owned<TextServices*>` deferred post-baseline-verde: nuove classi pubbliche in `include/chronon3d/` VIOLANO freeze v0.1. Infrastruttura di base già esiste in `TextRenderResources`. Vedi [FOLLOWUP_TICKETS.md §TICKET-079](FOLLOWUP_TICKETS.md).
- 🔵 `ShapedFontSpan` + `enum class TextLayoutUpdateStatus` deferred post-baseline-verde (Cat-Freeze AGENTS.md v0.1: vietato nuove classi pubbliche in `include/chronon3d/`); workaround corrente con `TextLayoutErrorKind::UnsupportedMultiFontRun` preserva integrità del renderer.
- Word timing/SRT/JSON.
- Styling per parola end-to-end.
- Subtitle/highlight/karaoke pipeline.
- Wiggly/Wave/Random selector completi.
- Golden multi-resolution e multi-fps per tutti i preset.
- Consumer SDK che usa il percorso testuale pubblico.

### Pianificato (non prima di Text Production V1)

- Variable fonts, ICU, Text 3D, MSDF, expression selector produttivi.

## Camera

### Percorso canonico

```text
CameraDescriptor
    → compile_camera()
    → CameraProgram
    → CameraProgram::evaluate(CameraEvalContext, CameraSession)
    → Camera2_5D
    → CameraProjectionSource / projection contract
    → renderer
```

Sono presenti: descriptor variant-based, programma compilato immutabile, sessione camera per render job, Zoom/FOV/Physical Lens, Pose/Orbit/Trajectory, constraint, shot timeline e transizioni, checkpoint/pre-roll, fingerprint deterministico.

### Gap principali

- Completare `OrientAlongPath` con tangent provider, look-ahead, keep-horizon e banking.
- Chiudere trajectory/base-state preservation e diagnostica shot timeline.
- Rimuovere i percorsi legacy dopo adapter e regression parity.
- Framing solver con multi-target, rule-of-thirds, dead-zone.
- DOF con Circle of Confusion e bokeh più fisico.
- Golden camera e parity test bloccanti.

## SDK

### Presente

- `libchronon3d_sdk_impl.a` aggregato.
- Header pubblici installati.
- Package config/version/targets CMake.
- Target pubblico `Chronon3D::SDK`.
- Consumer esterno basato su `find_package`.

### Da completare

- Consumer fuori-tree che renderizza una composizione reale (testo + camera → PNG).
- C ABI stabile con handle opachi.
- Formato dichiarativo versionato `.chronon`.

## Baseline osservata (`main@446a60e2`, 2026-06-23)

L'ultima baseline macchina-verificata ha prodotto:

| # | Controllo | Esito | Note |
|---|---|---|---|
| BG-1 | Software renderer boundary gate | ✅ PASS | `tools/check_software_renderer_boundary.sh` exit 0 |
| BG-2 | `linux-full-validation` configure | ✅ PASS | Configure completato clean |
| BG-3 | `linux-lean-dev` configure | ✅ PASS | Configure completato clean |
| BG-4 | Build `chronon3d_text_preset_visual_tests` | ❌ FAIL | TICKET-039: `SoftwareRenderer::settings()` regression |

**Verdetto**: 3/4 ✅ — baseline non verde (registrata al commit `446a60e2`). I blocker noti sulla
compilazione del testo (TICKET-038, TICKET-039) sono chiusi nel grafo dei commit dell'HEAD corrente,
ma una nuova baseline macchina-verificata sull'HEAD non è ancora stata registrata.

### Progressi dalla baseline

- **🟢 TICKET-039** — `SoftwareRenderer::settings()` access regression risolta (commit `68c3e0f0`).
  TXT-00 build/link verde; `chronon3d_text_preset_visual_tests` cablato nel render aggregator (`44def204`).
- **🟢 TICKET-038** — lambda capture / auto deduction rot in `tests/text/test_text_preset_visual.cpp`
  risolta (commit `91debc36`).
- **🟢 TICKET-029** — link di `chronon3d_scene_tests` sbloccato (commit `fb1b7e97`). I fix camera atterrati
  sul codice possono ora procedere verso la verifica macchina.
- **🟢 TICKET-040** — Taskflow completamente rimosso (non più required, rot chiusa).
- **🟢 TICKET-006** — fix statico per il linkage `chronon3d_backend_text` (gen-exp guard in
  `tests/renderer_tests.cmake` + if-endif in `tests/scene_tests.cmake`). Verifica macchina deferred.
- **🟢 Content restructuring** — riorganizzazione `content/` in showcases/examples/experimental, staging completato.
- **🟢 Docs** — archivio `stabilization-plan/` e `refactor-roadmap/` in `ARCHIVE/`, single source of truth
  (`CURRENT_STATUS.md`, `RELEASE_GATE.md`), nuovo `AGENTS.md`.
- **🟢 Windows/MSVC** — supporto rimosso, progetto Linux-only.  - **🟢 Phase-2 — mechanical P0 split of tests + content compositions.** Tre famiglie di file grandi sono stati suddivisi secondo il P0 meccanico del piano (1-commit-per-sotto-step + sub-commit-frequenti per i sotto-passi atomici):
    - **`tests/text/test_text_preset_visual.cpp` (898 righe)** decomposto in 8 file:
      `tests/text/visual/text_visual_fixture.hpp` (per-creazione-composizione + render-frame + calcolo-metriche + confronto-aspettative), `tests/text/visual/text_visual_metrics.cpp` (kUncapturedSentinel + RectF POD + ScenarioMetrics POD + compute_metrics `inline`), `tests/text/visual/text_visual_expectations.hpp` (VisualExpectation enum + VR_TEXT_PRESET_GATE macro), `tests/text/visual/text_visual_sentinels.hpp` (128 sentinel esadecimali dei 16 preset × 4 frame × 2 ratio, in forma di tabella dati).
      I 4 test TUs (reveal × 10 preset, emphasis × 4, subtitle × 2, cinematic × 0 — esclusi da A4 scope-lock) sostituiscono il file monolitico in `tests/text_preset_visual_tests.cmake`.
    - **`tests/showcase/test_cinematic_camera_showcase.cpp` (771 righe)** decomposto in 7 file sotto `tests/showcase/cinematic/`:
      `cinematic_showcase_config.hpp` (env-var driven CinematicConfig + read_cinematic_config lambda + `inline const` g_runtime + perf_opt_in_from_env strict 4-value accept-list), `cinematic_showcase_fixture.hpp` (kKeyFrames + kCompW/kCompH + runtime_kf lazy cached + FrameMetrics + hash_framebuffer FNV-1a 64-bit + compute_metrics + hash_to_hex + stamped + render_frames DECL), `cinematic_showcase_fixture.cpp` (render_frames definition FUORI classe), `test_cinematic_smoke.cpp` (A4.1 + A4.2), `test_cinematic_presets.cpp` (A4.3 + A4.3-strict 5/5 contract), `test_cinematic_determinism.cpp` (A4.4), `test_cinematic_artifacts.cpp` (A4.5 contact-sheet + A4.6 perf telemetry TICKET-053). `tests/showcase/CMakeLists.txt` aggiornato per singolo eseguibile (single-exec constraint), nessun target separato per-gate. Bug-fix associato: `cinematic_text_camera.hpp` include path corretto da `content/anims/compositions/` (post-24388800 restructuring) a `content/showcases/cinematic/` su tutti e 4 i test TUs.
    - **`content/showcases/cinematic/cinematic_text_camera.cpp` (667 righe)** decomposto in 10 file: 5 `.hpp` forwarder (1-factory-decl ciascuno: deep_parallax_cascade, whip_pan_hero_reveal, orbit_handheld_glow, rack_focus_title_swap, abyss_freefall_stagger) + 5 verbatim `.cpp` extractions + `cinematic_showcase_helpers.hpp` (shared per user spec) + `cinematic_text_camera.hpp` declassato a umbrella forwarder a 5 #include. `cinematic_text_camera.cpp` (667 LOC) cancellato atomicamente con il `content/CMakeLists.txt` update (1-line replace: cinematic_text_camera.cpp → 5 split .cpp). Le registrazioni canoniche in `content/animation_compositions.cpp::register_anim_compositions()` restano intatte (forward-declares satisfied at link time), MAI 5 registry locali.
  - CI non ancora ri-verificato dopo Phase-2 (richiede macchina-verifica dedicata sui 11 gate per promuovere la baseline a `CERTIFICATA @ 25b63730`).

  - **🟢 Phase-3 — mechanical P1 split of public headers.** Quattro header pubblici di grandi dimensioni sono stati suddivisi secondo il P1 meccanico del piano:
  - `include/chronon3d/animation/core/animated_value.hpp` (842 → 498 righe) decompresso in `detail/{animated_value_expressions.hpp, animated_value_roving.inl, animated_value_bezier.inl, animated_value_evaluation.inl}` (class declaration-only + bottom `#include` dei file `detail/`).
  - `include/chronon3d/registry/animator_resolver.hpp` (381 → 72 righe) dichiarazione-only; l'implementazione completa (22-branch `composer_for` table + `spec_is_rich` + `rich_paint_anchor`) è migrata in `src/registry/animator_resolver.cpp`, registrata in `src/registry/CMakeLists.txt`.
  - `include/chronon3d/scene/builders/scene_builder.hpp` (395 → ~190 righe) ha sostituito i corpi template in-line di `layer<> / screen_layer<> / adjustment_layer<> / precomp_layer<> / video_layer<> (2 overloads) / null_layer<> / sequence<> / camera_rig<>` con dichiarazioni + bottom-include di `detail/scene_builder_{layers,sequences,camera}.inl` (deportati verbatim, comportamento bit-identico).
  - `include/chronon3d/scene/builders/layer_builder.hpp` (415 → ~190 righe) — `pending_text_runs()` rimosso dalla superficie pubblica (restituiva `std::vector<const PendingTextRun*>` faceva leaking dello storage interno `m_text_runs`); il metodo è stato promosso nell'adattatore test-only `chronon3d::builders::testing::LayerBuilderInspector::pending_runs(lb)` (snapshot value-typed `PendingRunSnapshot{ name, animators }`) con accesso chiuso da `friend class`. I sette accessori out-of-class (`screen_dimensions` setter + getter, `name()`, `resource()`, `extension_context` setter/getter) vivono ora in `detail/layer_builder_inline.inl` (esplicitamente `inline` per ODR safety). Il file `detail/layer_builder_text.hpp` (forward-decl di `PendingTextRun`, `TextRunParams`, `TextRunSpec`, `TextRunBuilder`) è la superficie di accoppiamento minima per consumatori downstream del pipeline text-run senza dover includere l'intera catena `text_run_builder.hpp`.
  - I test (`tests/test_text_preset_registry.cpp`) sono stati aggiornati per usare `LayerBuilderInspector::pending_runs(lb)` invece di `lb.pending_text_runs()`: 4 callsites migrati, pattern `pre[i]->name`/`pre[i]->params.animators` convertiti in `pre[i].name`/`pre[i].animators` (snapshot value-typed), check di null-pointer rimossi.
  - **API delta**: aggiunto `chronon3d::builders::testing::LayerBuilderInspector::pending_runs(...)` (test-only); rimosso `LayerBuilder::pending_text_runs()`. Nessun altro cambio di superficie pubblica.
  - CI non ancora ri-verificato dopo Phase-3 (richiede macchina-verifica dedicata sui 11 gate  per promuovere la baseline a `CERTIFICATA @ 62c71e55`).
  - **🟢 Phase-1.3 — CMakePresets v6 + split + CI migration.** Schema CMakePresets `version: 6` (cmake `>= 3.27`, vedi FASE 1.3.B). 12 legacy configurePresets rimossi atomicamente (linux-core-dev, linux-lean-dev, linux-artist-dev, linux-full-validation, linux-release, linux-debug, linux-turbo, linux-fast-dev, linux-fast-dev-release, linux-lean, linux-dev-video, linux-release-full). 18 canonical configurePresets mantenuti, splittati in 6 file sotto `cmake/presets/`: `base.json` (base + base-linux), `development.json` (linux-dev, linux-asan), `ci.json` (linux-ci, linux-ci-nocontent + 4 gate linux-ci-{core-gate,lean-gate,release-build,full-validation}), `release.json` (__release-base + linux-release-validation + linux-experimental-validation), `experimental.json` (__experimental-base hidden helper), `profiling.json` (linux-profile-{core,motion,video,extended}). ROOT `CMakePresets.json` ridotto a index-only `{"version":6,"include":[6 paths]}` (915 → 7 righe). 3 workflow CI migrati atomicamente (FASE 1.3.C/D/E): `.github/workflows/gates.yml`, `.github/workflows/ci.yml`, `.github/workflows/gates-full-validation.yml` → riferimenti canonici `linux-ci-{core-gate,lean-gate,release-build,full-validation}`. 9 buildPresets + 8 testPresets orfani (referenziavano cp legacy rimossi) rimossi. ZERO nuovi registry locali, ZERO nuova API pubblica introdotta: solo build-infra (AGENTS.md Feature-Freeze Category 1). ZERO SHA hardcoded nei file di config post-fix.
  - CI non ancora ri-verificato dopo Phase-1.3 (richiede macchina-verifica dedicata sui 11 gate per promuovere la baseline a `CERTIFICATA @ d0d9a782`). AGENTS.md **co-update rule** rispettata: stesso commit che cambia lo stato aggiorna `CURRENT_STATUS.md` + snapshot SHA.

## Blocker per baseline verde

Servono tutte le prove seguenti sullo stesso commit:

- build core verde;
- test core verdi;
- build lean verde;
- test lean verdi;
- architecture gate verde e bloccante;
- renderer-boundary gate verde e bloccante;
- no-content build/test verde;
- scene/camera test link + run verde;
- install consumer esterno reale verde;
- full-validation build/test verde;
- documenti aggiornati con commit, comandi e risultati.

L'assenza di workflow fallito non equivale a una baseline verde.

> **Certificazione corrente (2026-06-30, `21103265`)**: 9/11 PASS — NON VERDE (2 failure: gate #3 `SoftwareRenderer&` accessor env-stable da `88d2deec`; gate #10 install-consumer CMake 3.27 env mismatch — env, non code).
> AGENTS.md "baseline verde" richiede **11/11 PASS sullo stesso commit** → feature freeze ancora attivo, freeze-revoca bloccata fino al commit che registra 11/11.
> Vedi [`baselines/main-21103265-baseline.md`](baselines/main-21103265-baseline.md) per log per-gate completi.

- [`ROADMAP.md`](ROADMAP.md): milestone prodotto.
- [`RELEASE_GATE.md`](RELEASE_GATE.md): criteri di release.
- [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md): ticket e difetti tracciati.
- [`FEATURES.md`](FEATURES.md): inventario delle feature.
- [`CAMERA_FEATURE_MATRIX.md`](CAMERA_FEATURE_MATRIX.md): dettaglio camera.
- [`TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`](TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md): piano testo.
- [`baselines/main-446a60e2-baseline.md`](baselines/main-446a60e2-baseline.md): ultima baseline macchina-verificata.

Documenti archiviati: [`ARCHIVE/`](ARCHIVE/).
- **TICKET-101 -- compile_text_layout accepts (TextDocument, paragraph_index) + build_text_run preserves rich-text (cat-3, atomic on `main`)** (chiusa per docs in [FOLLOWUP_TICKETS.md §TICKET-101](FOLLOWUP_TICKETS.md)): refactor cat-3 freeze-compliant del compiler canonico + wrapper multi-paragrafo. Aggiunto campo `std::size_t paragraph_index{0}` a `TextLayoutRequest` (POD extension, zero nuove classi pubbliche); `compile_text_layout()` ora indicizza direttamente in `tree.paragraphs[request.paragraph_index]` con bounds check. Rimossa la sintesi di `TextDocument para_doc` dal wrapper `build_text_run()` -- spans / font-overrides / font-size-per-span / tracking-per-span / paragraph-style / direction / language ora preservati 1:1 (chiude review P0 #2). `build_text_run()` rifiuta paragrafi multi-font tramite pre-check su `tree.paragraphs[i].runs` con FontSpec divergenti. `direction` + `language` + `line_height` ora propagati da `comp_style` (paragraph style override) invece di hardcode. 3 nuovi TEST_CASEs in `tests/text/test_rich_text_paragraph_preservation.cpp`: (1) wrapper respinge multi-font; (2) span override (font_size) preservato; (3) paragraph style (line_height) preservato. Firma pubblica `build_text_run()` invariata. File modificati: `include/chronon3d/text/text_run_builder.hpp` + `src/text/text_run_builder.cpp` + `tests/text/test_rich_text_paragraph_preservation.cpp` (NEW) + `tests/core_tests.cmake` + `docs/FOLLOWUP_TICKETS.md` + `docs/CURRENT_STATUS.md` (questa entry) + `docs/CHANGELOG.md` (entry).
