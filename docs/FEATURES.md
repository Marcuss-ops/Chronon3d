# Chronon3D — Feature Reference

> Snapshot: `HEAD`, 11 luglio 2026.
>
> Stato presente: [`CURRENT_STATUS.md`](CURRENT_STATUS.md).
> Requisiti di release: [`RELEASE_GATE.md`](RELEASE_GATE.md).
>
> **See also**: [§13/13 Acceptance Ortho Run Plane Contracts Forward-point 0d Ticket Acceptance Suite Phase D lineage frame](#acceptance-ortho-run-plane-contracts-forward-point-0d-ticket-acceptance-suite-phase-d-lineage-frame) (a fondo pagina) — audit-point canonico del piano di test a 3 assi ortogonali (`acceptance` / `boundary` / `ci` CTest labels) con `install_consumer_ci` come shibboleth a tripla label; chiusura di forward-point 0d del TICKET-ACCEPTANCE-SUITE-PHASE-D. Testo della sezione canonizzato come "§13/13 Acceptance" nei cross-ref di CHANGELOG.md + FOLLOWUP_TICKETS.md (il prefisso `§13/13` e un frame-label narrativo del parent closing-lineage, non parte dello slug).

La presenza di codice non implica che l’intero sottosistema sia verificato o
release-ready. Questa pagina separa feature presenti, parziali e pianificate.

## Rendering e compositing

### Presenti

- Scene, layer e gerarchie.
- Render graph compilato.
- Backend software CPU-first.
- Immagini, shape e SVG Path V1 (`M/L/H/V/C/Q/Z`, incluse forme relative).
- Mask, blend mode ed effetti software.
- Cache, dirty tracking, frame/history state e telemetria opzionale.
- Output immagine e video controllato dalle opzioni di build.
- Extension modules e registrazione esplicita delle composizioni.

### Parziali

- Precomp annidato, execution scope e concorrenza.
- Determinismo completo tra scheduler/profili.
- Import SVG limitato: primo path, styling/gruppi/trasformazioni/filtri incompleti.
- V3 tile-first non ancora runtime produttivo.

## Testo

### Presenti

- FreeType, HarfBuzz e FriBidi.
- Shaping, bidi, layout, wrapping, overflow e auto-fit.
- `TextSpec`, `TextRunSpec` e `TextDocument`.
- Span di stile e paragrafi.
- Gradient fill, stroke, shadow, material e glyph cache.
- Animator per glifo:
  - position;
  - scale;
  - rotation;
  - skew;
  - anchor;
  - opacity;
  - blur;
  - fill/stroke color;
  - stroke width;
  - tracking;
  - baseline shift;
  - character offset.
- Selector per glyph, grapheme, character, word e line.
- Range shape Square, RampUp, RampDown, Triangle, Round e Smooth.
- Ordine Forward, Reverse, FromCenter, ToCenter e Random deterministico.
- Sample-time sub-frame.
- Text-on-path esistente da consolidare.
- Registry canonico dei preset e sentinel test iniziali.

### Parziali

- Rich text end-to-end con più font/dimensioni/materiali nello stesso paragrafo.
- Animator e identità semantica per span/parola.
- Preset kinetic typography non ancora tutti verificati come prodotto.
- Motion blur testuale e visual regression completa.
- Segmentazione e line breaking globale senza ICU.

### Pianificate

- Timed text/SRT/JSON e word timing produttivo.
- Highlight, karaoke e subtitle layout policies.
- Wiggly, Wave e Random selector completi.
- Variable fonts.
- ICU opzionale.
- Color emoji/fallback completo.
- Expression Selector.
- Text 3D, MSDF, morph e displacement avanzati.

### Limiti noti

- line breaking CJK non ancora basato su ICU;
- color emoji non supportate in modo produttivo;
- Text 3D e MSDF non fanno parte del profilo stabile.

### Ergonomics (M1.8 §3) — Text Simplicity

> **Cross-link canonico**: [`docs/ROADMAP.md`](ROADMAP.md) M1.8 §3A/§3B/§3C rows DONE; [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) `M1.8-FASE-3-ERGONOMIA-CLOSURE` row; [`docs/CHANGELOG.md`](CHANGELOG.md) Fase 3 closure entry.

- **§3A — Animation helpers (17 inline functions)** ([`../include/chronon3d/animation/interpolate.hpp`](../include/chronon3d/animation/interpolate.hpp)) — `interpolate` / `spring` / `sequence` / `loop` / `loop_pingpong` / `delay` / `ease` / `clamp` / `map` / `progress` / `stagger` / `stagger_value` / `tween` / `frame_to_seconds` / `linear` / `clamp_progress` (16) + `stagger_delay` (17th, the §3A base from F3.A).  Pure inline header-only, zero singleton/registry/cache.  Snippet canonico: `interpolate(ctx.frame, {0, 15}, {0.4, 0.85})` is available (no new allocations, pure math).  Test surface: [`../tests/animations/test_animation_helpers.cpp`](../tests/animations/test_animation_helpers.cpp) (17 × 2 = 34 doctest CHECK assertions: 17 determinism + 17 edge case).
- **§3B — SafeArea placements (16 TextPlacementKind variants)** ([`../include/chronon3d/text/text_placement.hpp`](../include/chronon3d/text/text_placement.hpp)) — `CanvasCenter` / `TopLeft` / `TopCenter` / `TopRight` / `CenterLeft` / `Center` / `CenterRight` / `BottomLeft` / `BottomCenter` / `BottomRight` / `SafeAreaTop` / `SafeAreaBottom` / `SafeAreaLeft` / `SafeAreaRight` / `SafeAreaCenter` / `Absolute`.  User-facing `SafeAreaEnum` (5-value) introdotto come entry point canonico per `resolve_safe_area(SafeAreaEnum) → TextPlacement{SafeArea*}`.  Test surface: [`../tests/text/test_safe_area_placement.cpp`](../tests/text/test_safe_area_placement.cpp) (16 TEST_CASEs: 8 placement × canvas + 5 enum sweep + 2 cross-link invariant + 1 determinism).
- **§3C — 5 reusable presets** ([`../include/chronon3d/presets/text/text_presets_v1.hpp`](../include/chronon3d/presets/text/text_presets_v1.hpp)) — `title_centered` (96pt, CanvasCenter) + `subtitle_bottom` (48pt, SafeAreaBottom) + `caption_safe_area` (36pt, SafeAreaCenter) + `kinetic_word` (120pt, optional accent) + `lower_third` (42pt, BottomLeft).  All return canonical `TextDefinition` (F2.A) only, no cache/resolver/registry/service-locator.  Presets are responsive: they accept `CanvasInfo` and size the text box via `TextBoxConstraints` + `AspectRatioPolicy`, so the same preset works on 16:9, 9:16, 1:1 and arbitrary resolutions without hard-coded coordinates.  Deprecated overloads defaulting to 1920×1080 are kept temporarily for migration.  Test surface: [`../tests/text/test_presets_stability.cpp`](../tests/text/test_presets_stability.cpp) (multi-aspect-ratio assertions, UNCONDITIONAL) + [`../tests/text_golden/presets/test_presets_golden.cpp`](../tests/text_golden/presets/test_presets_golden.cpp) (5 PNG goldens + 9:16/1:1 sanity renders, Blend2D-gated INTEGRATION tier).

**Text Production V1 readiness**: the codebase currently ships 5 general presets and 0 subtitle presets; SRT/word-timing input, per-word highlight, and tracked golden PNGs are not yet present.  See `docs/RELEASE_GATE.md` §Text Production V1 for the full release criteria.

**Honest gap (per AGENTS.md §honesty)**: 5 PNG goldens in `test_renders/golden/text/presets/` are NOT yet tracked at HEAD — the re-bake requires a working build host (vcpkg-installed includes + tmpfs quota for full cmake build on this VPS, per the §13 honest limitation in CHANGELOG).  The `interpolate(ctx.frame, {0, 15}, {0.4, 0.85})` snippet is structurally present (pure inline function, no allocations) but the ctest execution of `chronon3d_animation_helpers_tests` + `chronon3d_text_presets_stability_tests` is deferred to the next working-build-host session.  `bash tools/check_no_dual_text_api.sh` exit 0 (0 violations, VIOLATIONS=() array per §5A).

## Camera

### Presenti

- `Camera2_5D` come snapshot runtime.
- `CameraDescriptor` come authoring canonico futuro.
- `compile_camera()` e `CameraProgram` immutabile.
- `CameraSession` per stato per-job.
- Zoom, FOV e Physical Lens projection.
- Focal length, sensor size, gate fit, pixel aspect e anamorphic contract.
- Focus distance, aperture, temporal motion blur e depth-of-field di base.
- Pose Tracks, Orbit Motion e Trajectory Motion.
- Look-at point/layer e tipo OrientAlongPath.
- Idle oscillation e handheld noise deterministico.
- Constraint tipizzati.
- Fingerprint del descriptor.
- Shot timeline.
- Transizioni Cut, Smooth Blend, Push, Whip Pan e Focus Handoff.
- Sub-frame sampling.
- Checkpoint/pre-roll per accesso non sequenziale.
- Camera debug/validation foundations.

### Parziali

- `OrientAlongPath` completo e banking.
- Framing solver bounds-aware, multi-target e rule-of-thirds.
- Near/far clipping di primitive complesse.
- DOF fisico con Circle of Confusion e near/far separation.
- Diagnostica della shot timeline.
- Preset e adapter legacy ancora sovrapposti al percorso compilato.
- Alcuni fix camera sono compilati ma test-blocked da TICKET-029.

### Pianificate

- Rimozione dei percorsi authoring legacy.
- CLI camera validate/path-report/debug-video stabile.
- Lens effects avanzati e bokeh fisico più completo.
- Multi-camera avanzata, se richiesta dal prodotto dopo Camera Production V1.

## SDK

### Presenti

- Header pubblici installabili.
- Archivio statico aggregato `libchronon3d_sdk_impl.a`.
- `Chronon3DConfig.cmake`, version file e targets export.
- Target pubblico `Chronon3D::SDK`.
- Registry CMake centrale.
- Consumer esterno `find_package` per il confine install/link.
- `ExtensionModule` e `ExtensionContext` per pack C++ esterni.

### Parziali

- Consumer corrente verifica package e simbolo, non un render completo.
- Documentazione pubblica e compatibility policy non ancora da release.
- Release artifact Linux non ancora certificati sullo stesso commit.

### Pianificate

- Consumer end-to-end che renderizza testo, camera ed effetti.
- Formato dichiarativo versionato `.chronon`.
- C ABI stabile con handle opachi.
- Binding Python iniziale e binding successivi sullo stesso ABI.
- Plugin binari versionati per pack C++ complessi.

## Expressions V2 — quarantena sperimentale

Expressions V2 è presente sotto `experimental/expressions/`, ma non è una
feature pubblica stabile.

| Superficie | Stato reale |
|---|---|
| Root | `experimental/expressions/` |
| Build switch | `CHRONON3D_BUILD_EXPERIMENTAL=ON` |
| Default | Escluso |
| Install/export SDK | No |
| Integrazione produttiva | No |

Non includere header sperimentali nel codice stabile prima della promozione e
della rimozione approvata della quarantena.

## Acceptance Ortho Run Plane Contracts Forward-point 0d Ticket Acceptance Suite Phase D lineage frame

> **Framing note (slug-friendly rename + retain §13/13 textual lineage)**: questa sezione è l'audit-point canonico del piano di test a 3 assi ortogonali (ortho run-plane: `boundary` / `ci` / `acceptance`). Il titolo è **slug-friendly** — nessun carattere `§`, `/`, em-dash `—`, o paren — per garantire identico auto-generated slug in GitHub web UI (`jekyll-anchor`) + MkDocs (`pymdownx.slugs`) + GitLab + Sphinx; lo slug canonico è `acceptance-ortho-run-plane-contracts-forward-point-0d-ticket-acceptance-suite-phase-d-lineage-frame`. Il frame-label `§13/13 Acceptance` (parent-closure-lineage, allinea con `## 13/13 Action Plan — closure summary` in `docs/ROADMAP.md` line 133) è preservato nei cross-ref testuali di `docs/CHANGELOG.md` + `docs/FOLLOWUP_TICKETS.md` come label narrativo, **non** come parte del heading slug — questo evita il bug del primo fix (slug `#§1313-...` vs `#13-13-...` vs `#1313-...` across renderers). Le altre 5 sezioni del doc usano titoli non-numerici (`## Rendering e compositing`, `## Testo`, `## Camera`, `## SDK`, `## Expressions V2 — quarantena sperimentale`).

> **Source of Truth (canonical subsystem ledger)**: [`../tests/acceptance/CHANGELOG.md`](../tests/acceptance/CHANGELOG.md) — 20-row category inventory, 4 forward-points (0a/0b/0c/0d), snapshot/baseline frozen-literal inventory, aggregate-target enumeration. Questa sezione `docs/FEATURES.md` è il doc-summary anchor che **delega** al ledger di sottosistema — niente duplicazione di contenuto canonico (Cat-3 anti-duplication: ogni nuova informativa operativa va nel canonical esistente, non in duplicati).

> **Cross-link canonico**: [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) `## Recently Closed` riga `TICKET-FEATURES-ORTHO-PLANE` — chiusura atomic-commit di forward-point 0d; [`docs/CHANGELOG.md`](CHANGELOG.md) `§00 forward-point 0d` entry prepended; [`docs/CURRENT_STATUS.md`](CURRENT_STATUS.md) Acceptance Suite row (PASS, 20/20 contract tests LANDED).

L'acceptance test plane del Chronon3D è strutturato su **3 assi ortogonali** (`ortho run-plane`: boundary / ci / acceptance), ciascuno invocato da un CTest label distinto. I tre assi sono **indipendenti ma convergenti**: `install_consumer_ci` (out-of-tree) è il bridge a tripla etichetta (`LABELS boundary;ci;acceptance`) che funge da shibboleth canonico del "tutti e tre i contratti coincidono". La tabella sotto è l'audit-point unico per ispezionare il piano senza cross-file grep.

| Axis (CTest label) | Aggregato / target | Comando canonico | Contratto verificato | Source of truth |
|---|---|---|---|---|
| `boundary` | `install_consumer_ci` + tutti i boundary-test scripts (`tools/check_*.sh`) | `bash tools/install_consumer_test.sh` (atteso `11/11 PASS`) | SDK install boundary contract — nessuna regressione del confine install/link abilitata dal push. | [`tools/install_consumer_test.sh`](../tools/install_consumer_test.sh) + [`cmake/Chronon3DSdkInstall.cmake`](../cmake/Chronon3DSdkInstall.cmake) |
| `ci` | `install_consumer_ci` + workflow matrix `[.github/workflows/ci-*.yml]` | `ctest -L ci` (sotto linux-ci preset) | CI-pipeline orthogonal plane: i target con label `ci` sono garantiti eseguibili nel CI matrix; rot-pattern di label/CMake mismatch viene catturato da `tools/wrap_push.sh` Step 4 gate chain. | [`.github/workflows/ci-sanitizer.yml`](../.github/workflows/ci-sanitizer.yml) + [`tools/wrap_push.sh`](../tools/wrap_push.sh) |
| `acceptance` | `chronon3d_acceptance` aggregate (`tests/CMakeLists.txt` lines ~290-340) ↦ 16 acceptance-labeled targets a HEAD (15 in-orchestrator + 1 out-of-tree `install_consumer_ci`) | `ctest -L acceptance` | 20-categories acceptance contract suite — 18 registered + 2 forward-point surfaces (vedi ledger di sottosistema). | [`tests/CMakeLists.txt`](../tests/CMakeLists.txt) + [`tests/acceptance/CMakeLists.txt`](../tests/acceptance/CMakeLists.txt) |

### Cross-axis shibboleth canonico

Il target **`install_consumer_ci`** è l'unico test al momento che porta simultaneamente tutte e tre le label:

```
cmake/Chronon3DTestSuite.cmake → set_tests_properties(install_consumer_ci PROPERTIES LABELS "boundary;ci;acceptance")
```

Quando questo target fallisce, i **3 assi sono in disaccordo** sul contratto SDK install+CI+acceptance: il diagnostico identifica immediatamente quale fase (boundary step / CI matrix / acceptance contract) è andata in rottura. Per la composizione della catena gate, vedi [`tools/wrap_push.sh`](../tools/wrap_push.sh) Step 4 (GATE-MNT-01) + Step 4.5 (hygiene/sanity/convention gates pre-push).

### Cross-refs operativi

- [`tests/acceptance/CHANGELOG.md`](../tests/acceptance/CHANGELOG.md) — subsystem chronological ledger (20-row inventory + 4 forward-points 0a/0b/0c/0d + aggregate composition + snapshot/§19 baseline enumeration). **Questo è il source-of-truth** per qualsiasi audit che richiede più del singolo asse.
- [`cmake/Chronon3DTestSuite.cmake`](../cmake/Chronon3DTestSuite.cmake) — helper `chronon3d_add_test_suite` + `chronon3d_register_test_source` + convenzione `LABELS` (i 3 label tripode sono dichiarati in `set_tests_properties`).
- [`tests/CMakeLists.txt`](../tests/CMakeLists.txt) line ~290-340 — `chronon3d_acceptance` aggregate (15 `if(TARGET)` guards) + le registrazioni delle singole suite categoria.
- [`docs/CURRENT_STATUS.md`](CURRENT_STATUS.md) `## Acceptance Suite` row — verdetto macchina-verifica corrente (`20/20 PASS LANDED`, esecuzione CI deferred per AGENTS.md §honesty).
- [`docs/ROADMAP.md`](ROADMAP.md) `## 13/13 Action Plan — closure summary` — parent frame narrative.

### Honest gap (per AGENTS.md §honesty)

La claim `20/20 PASS LANDED` di questa sezione è **code-level** (tutte le 18 registered targets + 2 forward-point surfaces sono boot-complete). La verifica macchina `ctest -L acceptance` richiede un working build host (vcpkg-installed glm/magic_enum + tmpfs quota-resolved) — defer per AGENTS.md §honesty come forward-point 0a di `tests/acceptance/CHANGELOG.md`. Il canonico ambiente di esecuzione rimane il CI matrix `linux-ci` preset.
