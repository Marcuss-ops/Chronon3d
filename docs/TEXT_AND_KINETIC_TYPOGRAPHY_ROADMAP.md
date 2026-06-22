# Chronon3D — Production Text & Kinetic Typography Roadmap

> **Fonte canonica della strategia** per portare il sottosistema testo di Chronon3D a livello After Effects dopo la chiusura del P0 baseline.
>
> Regole di ownership documentale: [`docs/stabilization-plan/09-document-canonicalization.md`](stabilization-plan/09-document-canonicalization.md).
> Punto di ingresso agente + ordine obbligatorio: [`AGENTS.md`](../AGENTS.md).
> Stato corrente: [`docs/STATUS.md`](STATUS.md).
> Piano operativo attivo: [`docs/NEXT_STEPS.md`](NEXT_STEPS.md).
> Roadmap P0: [`docs/ROADMAP.md`](ROADMAP.md).
> Ticket esistenti: [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md).
>
> Questo documento OWNS la strategia testuale a 13 fasi. Per evitare duplicazione, ogni fase che ha già un documento canonico di dettaglio elenca il cross-ref invece di riscrivere la prosa completa. Niente è "segnato verde" prima della verifica macchina (regola AGENTS.md).

---

## Missione

Portare il sottosistema testuale di Chronon3D da una base tecnicamente avanzata a una piattaforma produttiva per:

- kinetic typography;
- titoli cinematici;
- sottotitoli animati;
- evidenziazione parola per parola;
- typewriter;
- text-on-path;
- variable fonts;
- animazioni per glifo, parola e riga;
- rendering multilingua;
- template riutilizzabili per video automatizzati.

Il sistema deve restare:

- headless;
- CPU-first;
- deterministico;
- utilizzabile da CLI e backend;
- senza GUI;
- senza browser;
- senza dipendenze GPU obbligatorie;
- basato su una sola pipeline canonica.

---

## Cross-reference alle fonti canoniche esistenti (no duplicazione)

Quando una fase ha già dettaglio canonico altrove, il contenuto vive là. Questa roadmap possiede solo la sequenza strategica, i gate di uscita e la mappa AE-gap; il dettaglio operativo resta dove era già authoritative.

| Fase | Questa roadmap | Documento canonico di dettaglio |
|---|---|---|
| Phase 0 — Baseline | strategia + gate di uscita | [`docs/01-baseline-green.md`](01-baseline-green.md) · [`docs/stabilization-plan/01-baseline-green.md`](stabilization-plan/01-baseline-green.md) |
| Phase 1 — Contratto canonico del testo | strategia + elenco helper da deprecare | [`docs/MIGRATION_TEXT_SPEC.md`](MIGRATION_TEXT_SPEC.md) (in progress, vedi `FOLLOWUP_TICKETS.md` TICKET-002/006) |
| Phase 2 — Visual regression harness | strategia scenari + metriche | [`docs/01-baseline-green.md`](01-baseline-green.md) §2.4–2.5 (test_determinism_harness + scheduler-level già esistenti) |
| Phase 3 — Selector Engine V2 | catalogo selector + test gate | [`docs/EXPRESSIONS_V2_PROMOTION.md`](EXPRESSIONS_V2_PROMOTION.md) (Gate 4+ post Gate 3) |
| Phase 4 — Styling per parola / rich text | API desiderata + struttura span | (nessun canon esistente — primo PR spec-driven) |
| Phase 5 — Text on Path | componenti PathMeasure / TextPathLayout | `src/text/text_path_composer.cpp` (esistente, consolidare), `include/chronon3d/text/text_run_geometry.hpp` (riferimento per `PlacedGlyphRun`) |
| Phase 6 — Variable Fonts | cache-key contract | (nessun canon esistente; costruire registry speculare a `glyph_atlas`) |
| Phase 7 — Subtitle e word-timing | API JSON + registry | `tests/text/test_text_run_builder.cpp` (gate esistente, vedi `FOLLOWUP_TICKETS.md` TICKET-007.m/n/o) |
| Phase 8 — Motion blur testuale | supersampling + ottimizzazioni | `tests/scene/camera/test_motion_blur_torture_pr1.cpp` (fixture camera esistente, estendere a testo) |
| Phase 9 — ICU / global text | adapter pattern BuiltinBoundaryResolver vs IcuBoundaryResolver | [`docs/TEXT_BOTTLENECKS.md`](TEXT_BOTTLENECKS.md) §Fase 3 (CJK line-breaking già documentato come gap) |
| Phase 10 — Preset library | catalogo Reveal/Emphasis/Cinematic/Subtitle + DoD preset | (nessun canon — primo PR spec-driven) |
| Phase 11 — Text 3D opzionale | pipeline GlyphOutlineProvider → libtess2 → mesh | (nessun canon — primo PR spec-driven) |
| Phase 12 — MSDF opzionale | GlyphRasterStrategy multi-strategy | [`docs/TEXT_BOTTLENECKS.md`](TEXT_BOTTLENECKS.md) §Fase 3 (MSDF atlas già candidato a lungo termine) |
| Phase 13 — Expression Selector | variabili disponibili + esempi | [`docs/EXPRESSIONS_V2_PROMOTION.md`](EXPRESSIONS_V2_PROMOTION.md) Gate 3 + TICKET-EXP2-G3 (VEDERE Gate 3 chiusura prima di Phase 13) |

---

## Regole architetturali obbligatorie

## Una sola pipeline testuale

La pipeline canonica deve essere:

```text
TextDocument
    ↓
TextResolver
    ↓
FontEngine
    ↓
HarfBuzz shaping
    ↓
TextLayoutEngine
    ↓
TextRunLayout
    ↓
GlyphInstanceState
    ↓
TextAnimatorStack
    ↓
TextRunNode
    ↓
RenderBackend
```

Non introdurre pipeline parallele basate su:

- Skia;
- Pango;
- Cairo;
- renderer testuali alternativi;
- helper specifici per singola composizione.

## Registry comuni

Ogni nuova funzione deve entrare in uno dei sistemi comuni:

```text
TextStyleRegistry
TextMotionRegistry
TextSelectorRegistry
TextMaterialRegistry
TextLayoutResolver
FontResolver
```

Non creare registri locali dentro i content pack.

> **Denominazione provvisoria**: i nomi dei 6 registry (`TextStyleRegistry` / `TextMotionRegistry` / `TextSelectorRegistry` / `TextMaterialRegistry` / `TextLayoutResolver` / `FontResolver`) sono proposte da allineare alla tassonomia registry produttiva già operante in [`src/registry/`](src/registry/) (`shape_registry.cpp`, `sampler_registry.cpp`, `source_registry.cpp`, `effect_catalog.cpp`) e agli enum/types promossi in [`docs/MIGRATION_TEXT_SPEC.md`](MIGRATION_TEXT_SPEC.md) §3. Vedi anche Fase 3 §Wiggly/Random/Wave Selector per il vincolo omonimo.

(Vedi: [`docs/CORE_OWNERSHIP.md`](CORE_OWNERSHIP.md) §6 anti-duplicazione registry/resolver/cache.)

## Feature opzionali

Le dipendenze più pesanti devono essere collegate attraverso profili:

```text
core:
FreeType
HarfBuzz
FriBidi
Blend2D

motion:
core + preset e animator avanzati

global-text:
motion + ICU

extended:
global-text + libtess2 + msdfgen
```

(Mappa profili vcpkg: [`docs/stabilization-plan/08-dependency-profiles.md`](stabilization-plan/08-dependency-profiles.md).)

---

# Fase 0 — Stabilizzare la baseline

Questa fase precede ogni nuova feature testuale. **Non avviare le altre fasi finché i gate di uscita di Fase 0 non sono verdi macchina.**

## Piano operativo

Piano canonico di dettaglio: [`docs/01-baseline-green.md`](01-baseline-green.md) (fonti di stato rapido: [`docs/stabilization-plan/01-baseline-green.md`](stabilization-plan/01-baseline-green.md), [`docs/stabilization-plan/README.md`](stabilization-plan/README.md)).

Qui Fase 0 fissa **solo l'inquadramento strategico** — il dettaglio operativo è deliberatamente demandato al source-of-truth canon (per evitare due checklist divergenti sullo stesso work package, vedi [`docs/stabilization-plan/09-document-canonicalization.md`](stabilization-plan/09-document-canonicalization.md)):

- Nessuna delle fasi 1–13 può essere avviata finché [`docs/01-baseline-green.md`](01-baseline-green.md) §1 baseline verde non è verificato macchina (exit-code sui gate elencati lì).
- Regole di chiusura per stato/checkbox: [`docs/DEFINITION_OF_DONE.md`](DEFINITION_OF_DONE.md) e la regola "Un errore pre-esistente resta un errore" ([`docs/stabilization-plan/README.md`](stabilization-plan/README.md) chiusura).
- Comandi di riproduzione del baseline verde: [`docs/01-baseline-green.md`](01-baseline-green.md) §4.

Chiusura di Fase 0 = chiusura di P0 in [`docs/NEXT_STEPS.md`](NEXT_STEPS.md) "Ordine obbligatorio" punti 1–9. Nessuna checkbox operativa vive in questo documento.

---

# Fase 1 — Contratto canonico del testo

## Obiettivo

Rendere chiaro quale struttura rappresenta: contenuto, layout, stile, animazione, stato per glifo, risultato rasterizzato.

## Piano operativo

Piano canonico di dettaglio: [`docs/MIGRATION_TEXT_SPEC.md`](MIGRATION_TEXT_SPEC.md) — il lavoro di normalizzazione helper/resolver/range è in corso e referenziato da [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) TICKET-002 + TICKET-006 + TICKET-EXP2-G3.

Qui Fase 1 fissa **solo tre regole strategiche** — il dettaglio operativo è demandato a `MIGRATION_TEXT_SPEC.md` per non duplicare checklist (vedi [`docs/stabilization-plan/09-document-canonicalization.md`](stabilization-plan/09-document-canonicalization.md)):

1. **Una sola forma canonica di rappresentazione del testo** — tipi già promossi: `TextDocument`, `TextRunLayout`, `GlyphInstanceState`, `TextAnimatorStack`. Dettaglio in `MIGRATION_TEXT_SPEC.md` §3 e riferimento codice in `include/chronon3d/text/*.hpp`.
2. **Una sola pipeline di rasterizzazione** — vedi sezione [Pipeline canonica](#regole-architetturali-obbligatorie) di questo documento + [`docs/TEXT_BOTTLENECKS.md`](TEXT_BOTTLENECKS.md) per lo stato corrente.
3. **Niente helper locali nei content pack** — vedi [`docs/MIGRATION_TEXT_SPEC.md`](MIGRATION_TEXT_SPEC.md) §3 e TICKET-002 in [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) per lo stato corrente.

Chiusura di Fase 1 = [`docs/MIGRATION_TEXT_SPEC.md`](MIGRATION_TEXT_SPEC.md) segnata come completata + TICKET-006 chiuso (linkage `chronon3d_backend_text`) + nessun helper locale sopravvissuto in `content/text/`.

---

# Fase 2 — Visual Regression Harness

## Obiettivo

Creare una suite di rendering di riferimento che impedisca preset che compilano ma sono visivamente sbagliati.

## Scenari minimi

- [ ] Testo statico.
- [ ] Multilinea.
- [ ] Tracking.
- [ ] Stroke.
- [ ] Gradient fill.
- [ ] Shadow.
- [ ] Glow.
- [ ] Blur.
- [ ] Typewriter.
- [ ] Animazione per glifo.
- [ ] Animazione per parola.
- [ ] RTL.
- [ ] CJK.
- [ ] Emoji fallback.
- [ ] Scale molto piccole e molto grandi.

## Output e metriche

Cross-ref canon: [`docs/01-baseline-green.md`](01-baseline-green.md) §2.4-2.5 (fixture `tests/deterministic/test_determinism_harness.cpp` + scheduler determinism) definisce formatter di output (`frame_NNN.png`, `metrics.json`) e l'insieme di metriche canon (hash, ink-bbox, ink-pixel, luminanza media, alpha coverage, centro visivo, overflow box, tempo di rendering). Fase 2 eredita questo formatter per ogni scenario dei 15 scenari minimi sopra elencati.

## Gate di uscita

Per i 4 criteri di stabilità di un preset testuale (golden frame · readability · multi-resolution · ≥3 timestamp), usare i gate canon elencati in [`docs/01-baseline-green.md`](01-baseline-green.md) §2.3.

**Estensione dell'harness esistente**: le fixture scheduler-level (`tests/deterministic/test_determinism_harness.cpp`, `tests/render_graph/executor/test_scheduler_determinism.cpp`) sono la base; questa fase aggiunge un livello scenario-specific senza sostituire le fixture scheduler-level.

---

# Fase 3 — Selector Engine V2

## Obiettivo

Colmare uno dei principali gap rispetto ad After Effects.

## Nuovi selector

### Wiggly Selector

- [ ] Seed deterministico.
- [ ] Frequenza temporale.
- [ ] Frequenza spaziale.
- [ ] Ampiezza.
- [ ] Correlazione tra glifi.
- [ ] Correlazione tra parole.
- [ ] Phase offset.
- [ ] Noise 1D e 2D.
- [ ] Loop deterministico.

> I nomi simbolici del catalogo selector (`WigglySelector`, `RandomSelector`, `WaveSelector`) sono **denominazione provvisoria** da allineare alla tassonomia registry produttiva fissata in [`docs/MIGRATION_TEXT_SPEC.md`](MIGRATION_TEXT_SPEC.md) §3 e nei registri già operativi in `src/registry/*` (vedi sezione [Pipeline canonica](#regole-architetturali-obbligatorie) in cima al presente doc).

### Random Selector

- [ ] Ordine casuale deterministico.
- [ ] Seed.
- [ ] Shuffle per glifo.
- [ ] Shuffle per parola.
- [ ] Shuffle per riga.

### Wave Selector

- [ ] Sine.
- [ ] Triangle.
- [ ] Saw.
- [ ] Pulse.
- [ ] Frequenza.
- [ ] Phase.
- [ ] Propagazione per indice o posizione.

## Architettura

```text
SelectorSpec
    ↓
TextSelectorRegistry
    ↓
SelectorEvaluator
    ↓
weight per glyph
```

Tutte le proprietà animator devono consumare lo stesso valore `weight`.

## Test

- [ ] Stesso seed produce lo stesso output.
- [ ] Scheduler differenti producono lo stesso risultato.
- [ ] Lo selector non dipende dall'ordine dei thread.
- [ ] Spaces excluded correttamente.
- [ ] Supporto grapheme, word e line.

**Cross-ref Gate**: questa fase realizza le Gate 4+ di [`docs/EXPRESSIONS_V2_PROMOTION.md`](EXPRESSIONS_V2_PROMOTION.md) (Gate 3 prima).

---

# Fase 4 — Styling per parola e rich text produttivo

## Obiettivo

Permettere frasi con styling differente senza creare un layer per ogni parola.

## API desiderata

```cpp
text.document()
    .span("QUESTA ")
    .span("PAROLA").color(yellow).weight(800).scale(1.25f)
    .span(" È IMPORTANTE");
```

## Funzioni

- [ ] Più font nello stesso paragrafo.
- [ ] Più colori.
- [ ] Più pesi.
- [ ] Più dimensioni.
- [ ] Stroke per span.
- [ ] Materiale per span.
- [ ] Animatore per span.
- [ ] ID semantico per parola.
- [ ] Highlight background adattivo.
- [ ] Inline icon opzionale.
- [ ] Stile ereditato dal parent.

## Strutture

```text
TextDocument
Paragraph
TextSpan
TextSpanStyle
TextSpanMotion
TextSpanIdentity
```

## Test

- [ ] Gli span non rompono il shaping contestuale.
- [ ] Legature e script complessi rimangono corretti.
- [ ] RTL mantiene l'ordine visuale corretto.
- [ ] Il layout rimane deterministico.

---

# Fase 5 — Text on Path

## Obiettivo

Supportare testo su curve senza introdurre un secondo motore di layout.

## Componenti

### PathMeasure

- [ ] Lunghezza totale.
- [ ] Lookup distanza → posizione.
- [ ] Tangente.
- [ ] Normale.
- [ ] Curvatura.
- [ ] Arc-length table.

### TextPathLayout

- [ ] First margin.
- [ ] Last margin.
- [ ] Reverse path.
- [ ] Perpendicular to path.
- [ ] Force alignment.
- [ ] Overflow clip.
- [ ] Loop su path chiusi.
- [ ] Animazione della posizione lungo il path.
- [ ] Animazione del path.

## Pipeline

```text
PlacedGlyphRun
    ↓
glyph advances
    ↓
PathMeasure
    ↓
position + tangent per glyph
    ↓
GlyphInstanceState
```

## Test

- [ ] Linea.
- [ ] Bézier.
- [ ] Cerchio.
- [ ] Path chiuso.
- [ ] Path invertito.
- [ ] RTL.
- [ ] Path animato.

## Piano operativo

**Stato corrente** (audit 2026-06-22): `src/text/text_path_composer.cpp` è già production-canonical (178 LOC) con `include/chronon3d/text/path_sampler.hpp` (`PathSampler` con arc-length table, De Casteljau subdivision) e `include/chronon3d/text/text_path_spec.hpp`. Il lavoro di Fase 5 è dunque **consolidamento-intelligente**, non creazione: renominare/riprodurre i tipi canon in `PathMeasure` + `TextPathLayout` facendo leva sulle primitive esistenti, senza introdurre una pipeline parallela.

> **Cross-link con Fase 11**: la `Camera 2.5D-text-only` di Fase 11 (libtess2 free-standing) è distinta dalla transform path-based di questa Fase 5. Vedi Fase 11 §Camera 2.5D per il vincolo anti-aliasing con la pipeline produttiva camera di [`docs/CAMERA_REGIA_AE_PLAN.md`](CAMERA_REGIA_AE_PLAN.md).

---

# Fase 6 — Variable Fonts

Non serve una nuova libreria: usare FreeType e HarfBuzz già presenti.

## Obiettivo

Animare gli assi del font: `wght`, `wdth`, `slnt`, `opsz`, assi personalizzati.

## Piano operativo

**Stato corrente** (audit 2026-06-22): nessuna infrastruttura variable-font presente nel tree (zero match per `fvar`, `OT_VAR`, `HarfBuzz.*variable`, `FT_PARAM_TAG_VARIATIONS` in `src/`+`include/`). Il design cache-key è da fare ex-novo, ancorato al canon `FontEngine` in `include/chronon3d/text/font_engine.hpp` (Fase 0 — [`docs/01-baseline-green.md`](01-baseline-green.md) §2.5 + `[docs/TEXT_BOTTLENECKS.md](TEXT_BOTTLENECKS.md) §1 HarfBuzz shaper). Plan di esecuzione:

1. Aggiungere `VariableAxisProperty` + `VariableAxisMap` POD canonici in `include/chronon3d/text/variable_axis.hpp`.
2. Estendere la cache-key canon di `FontEngine` (vedi `src/text/font_engine.cpp` shared_mutex + LruCache pattern con sharding) per includere `axis_coords` su 5 livelli: `font_id + axis → shaping → glyph → layout → render`.
3. Adottare il path HarfBuzz `hb_font_set_var_named_instance` + `hb_font_set_var_axis_design_coords` come modificatore di shaper (single-source-of-truth); test determinismo-ordered per ri-abilitare AVX2 quando servirà (cross-ref `docs/01-baseline-green.md` §3.1 ordered-reduction ticket separato).

## Nuove proprietà

```cpp
VariableAxisProperty{"wght", 100.0f, 900.0f}
VariableAxisProperty{"wdth", 75.0f, 125.0f}
VariableAxisProperty{"slnt", 0.0f, -12.0f}
```

## Livelli di applicazione

- [ ] Intero documento.
- [ ] Span.
- [ ] Parola.
- [ ] Glifo.
- [ ] Selector weight.
- [ ] Audio amplitude.

## Problema cache

Le coordinate degli assi devono entrare in:

- font cache key;
- shaping cache key;
- glyph cache key;
- layout hash;
- render hash.

## Test

- [ ] Stesso axis set produce stesso shaping.
- [ ] Cambiare `wght` invalida la cache corretta.
- [ ] Nessuna collisione tra istanze della stessa variable font.
- [ ] Animazione deterministica.

---

# Fase 7 — Subtitle e word-timing pipeline

## Obiettivo

Trasformare transcript, SRT o JSON in testo animato sincronizzato.

## Input canonico

```json
{
  "segments": [
    {
      "text": "Questo è un esempio",
      "start": 1.2,
      "end": 2.8,
      "words": [
        {"text": "Questo", "start": 1.2, "end": 1.6},
        {"text": "è", "start": 1.6, "end": 1.8},
        {"text": "un", "start": 1.8, "end": 2.0},
        {"text": "esempio", "start": 2.0, "end": 2.8}
      ]
    }
  ]
}
```

## Componenti

```text
TimedTextDocument
TimedParagraph
TimedWord
TimedTextCompiler
SubtitleLayoutPolicy
EmphasisResolver
```

## Funzioni

- [ ] Evidenziazione parola corrente.
- [ ] Scale punch.
- [ ] Cambio colore.
- [ ] Background highlight.
- [ ] Karaoke fill.
- [ ] Word pop.
- [ ] Word slide.
- [ ] Riga attiva e riga precedente.
- [ ] Safe area verticale.
- [ ] Layout 16:9.
- [ ] Layout 9:16.
- [ ] Layout 1:1.
- [ ] Durata minima di leggibilità.
- [ ] Gestione pause.
- [ ] Gestione parole troppo lunghe.
- [ ] Segmentazione automatica delle righe.

## Registry

```text
SubtitlePresetRegistry
EmphasisPresetRegistry
SubtitleLayoutRegistry
```

**Cross-ref gate**: garantire che `tests/text/test_text_run_builder.cpp` e `tests/text/test_text_unit_map.cpp` smettano di essere `doctest::skip()` (vedi `FOLLOWUP_TICKETS.md` TICKET-007.m/n/o/p) PRIMA di promuovere i nuovi preset subtitle.

---

# Fase 8 — Motion Blur testuale

## Obiettivo

Rendere più naturali position, rotation e scale animation.

## Implementazione CPU-first

Temporal supersampling:

```text
sample time - shutter/2
sample time
sample time + shutter/2
```

Versione configurabile:

```text
samples: 2, 4, 8
shutter_angle
shutter_phase
weighting
```

## Ottimizzazioni

- [ ] Riutilizzare il layout statico.
- [ ] Rivalutare solo `GlyphInstanceState`.
- [ ] Accumulare in framebuffer temporaneo.
- [ ] Limitare il sampling alla bounding box del testo.
- [ ] Saltare il motion blur su testo statico.

## Test

- [ ] Output deterministico.
- [ ] Nessun cambiamento quando motion blur è disabilitato.
- [ ] Nessun ghosting fuori dalla bounding box.
- [ ] Stesso risultato tra scheduler.

**Riuso fixture**: `tests/scene/camera/test_motion_blur_torture_pr1.cpp` ha una fixture camera-motion-blur già deterministica; questa fase la estende a layer di testo fissati su un percorso animato.

---

# Fase 9 — ICU e global text

ICU deve essere opzionale.

## Obiettivo

Correggere segmentazione e line breaking globali.

## Adapter canonico

```text
TextBoundaryResolver
    ├── BuiltinBoundaryResolver
    └── IcuBoundaryResolver
```

## Funzioni ICU

- [ ] Grapheme boundaries.
- [ ] Word boundaries.
- [ ] Line breaking.
- [ ] Sentence boundaries.
- [ ] CJK line breaking.
- [ ] Thai.
- [ ] Lao.
- [ ] Khmer.
- [ ] Burmese.

## Regola

Il resto del motore non deve dipendere direttamente da ICU.

## Piano operativo

**Stato corrente** (audit 2026-06-22): nessuna ICU installata in `vcpkg_installed/*` (to-be-introduced compile-time opt-in flag `CHRONON3D_USE_ICU=ON`; flag is **currently undefined** in `CMakeLists.txt`/`cmake/`/`vcpkg.json` and will be wired in during Fase 9); riferimenti condizionali esistenti in `src/text/glyph_selector.cpp`, `src/backends/text/bidi_segmenter.cpp`, `include/chronon3d/text/text_layout_engine.hpp`. Boundary-resolver-like references in `src/text/text_resolver.cpp` + `src/text/text_run_driver.cpp`; nessun `TextBoundaryResolver` canon ancora esposto.

**Piano di esecuzione**:

1. **Adapter pattern canon** in `include/chronon3d/text/boundary_resolver.hpp`:
   * `TextBoundaryResolver` interface con metodi virtuali `grapheme_clusters(text) -> vector<Range>`, `word_bounds(text)`, `line_breaks(text)`, `sentence_bounds(text)`.
   * `BuiltinBoundaryResolver` (zero ICU dep): ASCII word + newlines + char-class segmentation baseline.
   * `IcuBoundaryResolver` (opt-in): wrappa `icu::BreakIterator` per grapheme/word/line/sentence + CJK line-breaking (Thai/Lao/Khmer/Burmese).
2. **Resoluzione runtime** via `std::unique_ptr<TextBoundaryResolver>` istanziato da `src/text/text_resolver.cpp` in base al flag `CHRONON3D_USE_ICU` (compile-time, **currently undefined** in `CMakeLists.txt`/`cmake/`/`vcpkg.json` — to-be-introduced in Fase 9) + `CHRONON3D_FORCE_BUILTIN_BOUNDARY` (runtime override, to-be-introduced alongside the compile-time flag); mai istanziato dentro i nodi downstream.
3. **Regola di isolation** (hard): `#include <unicode/*>` consentito solo in `src/text/boundary_resolver/` + `src/backends/text/icu_resolver.cpp`. Nessun altra TU può includere ICU headers. test_architectural.sh §5 (gate check) deve essere extended a verificare questa invariante.
4. **`GlyphRasterStrategy` color emoji**: dipende da Fase 12 che ha canonicalizzato la multi-strategy (Bitmap | Vector | Msdf). Aggiungere `ColorGlyphRasterizer` come quarta strategia per COLR/CPAL tables (OpenType color-font extension) + emoji fallback via `sil`/`twemoji` provider. `GlyphRasterStrategy` enum è keyed by font-table-capability detection.
5. **Profilo vcpkg**: la feature `text` esistente (vedi [`docs/stabilization-plan/08-dependency-profiles.md`](stabilization-plan/08-dependency-profiles.md)) introduce `harfbuzz` indirettamente; per Fase 9 si richiede l'aggiunta `icu` opzionale a uno dei profili (es. motion + icu = motion+icu). Estensione del `vcpkg.json`.
6. **Testing harness multilingua**: 10 lingue target (vedi sotto) come integration-test su scene Compositor. Cross-ref [`docs/01-baseline-green.md`](01-baseline-green.md) §2.4-2.5 per il formatter `metrics.json` + §4 per i comandi di riproduzione.

## Test linguistici

- [ ] Inglese.
- [ ] Italiano.
- [ ] Portoghese.
- [ ] Arabo.
- [ ] Ebraico.
- [ ] Hindi.
- [ ] Cinese.
- [ ] Giapponese.
- [ ] Coreano.
- [ ] Thai.

**Cross-ref**: gap già documentato in [`docs/TEXT_BOTTLENECKS.md`](TEXT_BOTTLENECKS.md) §Fase 3 (CJK).

---

# Fase 10 — Preset library produttiva

## Obiettivo

Creare una libreria piccola ma realmente affidabile.

## Reveal

- [ ] FadeIn.
- [ ] BlurIn.
- [ ] SlideUp.
- [ ] SlideDown.
- [ ] ScaleIn.
- [ ] TrackingClose.
- [ ] MaskedLineReveal.
- [ ] WordCascade.
- [ ] CharacterCascade.

## Emphasis

- [ ] WordPop.
- [ ] ScalePunch.
- [ ] ColorAccent.
- [ ] HighlightBox.
- [ ] UnderlineSweep.
- [ ] StrokeFlash.
- [ ] GlowPulse.
- [ ] ShakeImpact.

## Cinematic

- [ ] WideTrackingTitle.
- [ ] SoftDollyTitle.
- [ ] GradientReveal.
- [ ] OutlineToFill.
- [ ] LightSweep.
- [ ] NeonTitle.
- [ ] GlassTitle.
- [ ] BevelTitle.

## Subtitle

- [ ] MinimalWhite.
- [ ] YellowKeyword.
- [ ] ActiveWordPop.
- [ ] KaraokeFill.
- [ ] RoundedBox.
- [ ] TwoLineCenter.
- [ ] VerticalShorts.
- [ ] DocumentaryLowerThird.

## Criterio di stabilità preset

Ogni preset deve avere:

- golden frame;
- video demo;
- test 16:9;
- test 9:16;
- test con testo corto;
- test con testo lungo;
- test multilingua;
- test a 24, 30 e 60 fps;
- valori modificabili tramite parametri;
- nessun valore hardcoded specifico della demo.

---

# Fase 11 — Text 3D opzionale

Dipendenza opzionale: `libtess2`.

## Piano operativo

**Stato corrente** (audit 2026-06-22): zero match per `libtess2` (e per `tess.h` / `TESStesselator` / `Tessellator`) nel tree (`src/`, `include/`, `tools/`) e in `vcpkg.json` (presente solo `meshoptimizer` tramite feature `mesh`). La pipeline `FreeType → GlyphOutlineProvider → libtess2 adapter → GlyphMeshBuilder → Mesh cache → CPU rasterizer` non è ancora installata. Da aggiungere a `vcpkg.json` come nuova feature opzionale `text-3d` (opt-in; non abilitata dai profili `linux-profile-{core,motion,video,extended}` correnti in [`docs/stabilization-plan/08-dependency-profiles.md`](stabilization-plan/08-dependency-profiles.md)).

**Anti-collisione con Fase 5**: questa Fase 11 NON sostituisce `src/text/text_path_composer.cpp` (178 LOC, canon produttivo) né `include/chronon3d/text/path_sampler.hpp` (`PathSampler` con arc-length table, De Casteljau). La composizione 2D su path resta su Fase 5; libtess2 introduce *solo* la mesh extrusion 3D dei glifi. I tipi Fase 11 seguono la naming canon qui sotto, separata da Fase 5:

```text
GlyphOutlineProvider   (FreeType → outline 2D del glifo)
        ↓
libtess2 adapter        (tessellazione triangolare)
        ↓
GlyphMeshBuilder       (front/back/side faces + bevel + extrusion depth)
        ↓
Mesh cache              (cache-key: font_id + glyph_id + outline_hash + extrusion_depth)
        ↓
Text3DNode / CPU rasterizer  (rasterizza la mesh proiettata)
```

**Vincolo Camera 2.5D-text-only** (free-standing, distinto da `chronon3d::camera::CameraRig`): la Camera 2.5D di Fase 11 è un **code-path separato** che produce un `chronon3d::scene::model::camera::Camera2_5D` (`include/chronon3d/scene/model/camera/camera_2_5d.hpp`) senza passare per `CameraRig::evaluate(...)` né per `AnimatedCamera2_5D::evaluate(...)` (vedi [`docs/CAMERA_REGIA_AE_PLAN.md`](CAMERA_REGIA_AE_PLAN.md) §3 sul canon CameraRig). Il traguardo è: Fase 11 istanzia localmente un `Camera2_5D{position, rotation, zoom, dof, motion_blur}`; questo evita aliasing con la pipeline produttiva camera di scena che ha già i suoi preset `orbit_reveal`/`premium_push_in`/`hero_title_push` in [`src/scene/camera/camera_rig_presets.cpp`](src/scene/camera/camera_rig_presets.cpp).

## Pipeline

```text
FreeType glyph outline
    ↓
GlyphOutlineProvider
    ↓
libtess2 adapter
    ↓
GlyphMeshBuilder
    ↓
front/back/side faces
    ↓
Mesh cache
    ↓
CPU rasterizer
```

## Funzioni

- [ ] Extrusion.
- [ ] Bevel.
- [ ] Front material.
- [ ] Side material.
- [ ] Light direction.
- [ ] Per-character depth.
- [ ] Camera 2.5D.
  > **Collision-warning**: la Camera 2.5D di Fase 11 è **text-only free-standing**, definita dalla spec libtess2. NON passa attraverso `chronon3d::camera::CameraRig` (vedi [`docs/CAMERA_REGIA_AE_PLAN.md`](CAMERA_REGIA_AE_PLAN.md)) — questo per evitare aliasing con la pipeline produttiva camera di scena.
- [ ] Per-character rotation.
  > **Anti-aliasing con Fase 5**: la rotazione per-character testuale in Fase 11 è distinta dal transform path-based di Fase 5 (text-on-path). Le due fasi non condividono identificatori per-glyph rotation.
- [ ] Mesh cache.

## Non-goal iniziale

- illuminazione fisicamente corretta;
- ray tracing;
- shadows 3D complete;
- renderer GPU.

---

# Fase 12 — MSDF opzionale

Dipendenza opzionale: `msdfgen`.

## Piano operativo

**Stato corrente** (audit 2026-06-22): zero match per `msdfgen` (o per `msdfgen::`, `MSDFGenerator`, `MultiSignedDistanceField`) nel tree (`src/`, `include/`, `docs/`, `tests/`, `tools/`) e in `vcpkg.json`. `GlyphRasterStrategy` non è ancora esposta come tipo canon nel codice: l'ASCII-tree in questa Fase 12 è design, nomenclatura pre-canonizzata in [`docs/TEXT_BOTTLENECKS.md`](TEXT_BOTTLENECKS.md) §Fase 3 (MSDF atlas) ma senza implementazione.

Plan di esecuzione:

1. **Resolver canon** in `include/chronon3d/text/glyph_raster_strategy.hpp`:
   * `enum class GlyphRasterStrategy { Bitmap, Vector, Msdf }`.
   * `IGlyphRasterizer` interface con `rasterize(font_face_handle, glyph_id, target) -> RasterizedGlyph` che le 3 strategie implementano.
   * `GlyphRasterStrategyResolver::select(font_table_capability) -> GlyphRasterStrategy` in base alla presenza di `glyf`/`CFF`/`COLR`+`CPAL` e (per MSDF) presenza del atlas pre-generato associato al font.
2. **Vincolo anti-disclosure** (hard): i nodi downstream (`TextRunNode`, `TextAnimatorStack`, il rendering pipeline) ricevono un `IGlyphRasterizer*` dal resolver e non istanziano `msdfgen::Shape` né `msdfgen::generateMSDF` direttamente. test_architectural.sh §5 (gate check, già esteso per Fase 9 ICU) deve negare `#include <msdfgen*>` al di fuori di `src/text/glyph_raster/`. Stesso vincolo per `#include <libtess2/*>` (Fase 11).
3. **vcpkg feature opzionale**: aggiungere `msdfgen` come dipendenza di una nuova feature `text-msdf` (opt-in, NON parte di `default-features` in `vcpkg.json`). Solo `linux-profile-extended` la abilita, cross-ref [`docs/stabilization-plan/08-dependency-profiles.md`](stabilization-plan/08-dependency-profiles.md). La feature `text-msdf` richiede indirettamente `freetype[harfbuzz]` + `glm` (già presenti).
4. **Estensioni Morph/Displacement** — vedi sezione dedicata dopo questa Fase 12.

## Casi d'uso

- [ ] Scale estreme.
- [ ] Outline animato.
- [ ] Glow controllato.
- [ ] Dissolve basato sulla distanza.
- [ ] Edge reveal.
- [ ] Distortion.
- [ ] Text morph.
- [ ] Prospettiva.

## Architettura

```text
GlyphRasterStrategy
    ├── BitmapGlyphRasterizer
    ├── VectorGlyphRasterizer
    └── MsdfGlyphRasterizer
```

Il resolver deve scegliere la strategia. I nodi non devono conoscere `msdfgen`.

**Cross-ref**: gap già candidato in [`docs/TEXT_BOTTLENECKS.md`](TEXT_BOTTLENECKS.md) §Fase 3 (MSDF font atlas).

## Estensioni della Fase 12: Morph e displacement avanzati

Queste estensioni **non introducono nuove dipendenze esterne** — si appoggiano sulla stessa pipeline `msdfgen → GlyphRasterStrategy::Msdf` promossa in questa Fase 12, aggiungendo due POD canon e due Effect canon (registrati nel catalogo esistente [`src/effects/effect_catalog.cpp`](src/effects/effect_catalog.cpp) + [`effect_catalog_data.cpp`](src/effects/effect_catalog_data.cpp), NON creando un catalogo locale).

### Morph

Passaggio continuo tra due glyph runs (cifre che mutano in altre cifre, lettere che confluiscono in logo, A↔B cross-fade per transizioni di titolo).

- POD `TextMorphSpec` (`include/chronon3d/text/text_morph_spec.hpp`): `source_glyph_id`, `target_glyph_id`, `progress` (0..1), `easing` (enum `Easing` canonico da [`include/chronon3d/animation/core/easing.hpp`](include/chronon3d/animation/core/easing.hpp) cross-ref Fase 10).
- POD `MorphField` (`include/chronon3d/text/morph_field.hpp`): `std::vector<TextMorphSpec>` ordinati per priorità di interpolazione.
- Effect `MorphEffectNode` (`src/render_graph/nodes/morph_effect_node.hpp`): interpola la signed-distance-field del sample source e target via approssimazione barycentrica sui pixel MSDF, gated da `MotionBlurSettings::jitter_seed`-style seed per il determinismo (cross-ref `Morph::jitter_seed{u64, default=0x4B2E8F91}` come nel canon `Camera2_5D::Camera2_5D` motion_blur).

### Displacement

Deformazione per-glyph su base d'onda / noise / audio amplitude.

- POD `TextDisplacementSpec` (`include/chronon3d/text/text_displacement_spec.hpp`): `amplitude` (em), `wavelength` (em), `phase_offset` (rad), `mode = { Sin | Noise | Audio }`, `axis = { X | Y | Both }`.
- Effect `DisplacementEffectNode` (`src/render_graph/nodes/displacement_effect_node.hpp`): applica un campo di deformazione al MSDF sample pre-rasterizzazione, estendendo la cache-key canon di Fase 6 (`font_id + axis → shaping → glyph → layout → render`) con `displacement_mode + axis`. NON introduce cache locale: riusa `chronon3d::text::font_engine::LruCache` sharded.
- Audio coupling: quando `mode = Audio`, legge `audioAmplitude` dal context (cross-ref [`docs/EXPRESSIONS_V2_PROMOTION.md`](docs/EXPRESSIONS_V2_PROMOTION.md) Gate 4+; il POD espone già il campo per essere wired successivamente, anche se Gate 3 non è ancora `🟢 Done`).

### Cross-ref canon (riuso, no duplicazione)

Le estensioni Fase 12 ereditano:
- Registri canon: effect canon in [`src/effects/`](src/effects/) + AnimationPack canon in [`src/registry/`](src/registry/) (vedi [`docs/CORE_OWNERSHIP.md`](CORE_OWNERSHIP.md) §6 anti-duplicazione).
- Cache canon: `shared_mutex` + `LruCache` sharded in [`src/text/font_engine.cpp`](src/text/font_engine.cpp) (Fase 0/Fase 6); morph/displacement estendono la cache-key, NON creano cache locali.
- Determinismo seed: pattern canon `MotionBlurSettings::jitter_seed` in [`include/chronon3d/scene/model/camera/camera_2_5d.hpp`](include/chronon3d/scene/model/camera/camera_2_5d.hpp) — Fixture di test riusano lo stesso schema (default `0x4B2E8F91`).

---

# Fase 13 — Expression Selector

Questa fase deve usare Expressions V2 soltanto dopo la sua promozione produttiva.

## Variabili disponibili

```text
textIndex
textTotal
wordIndex
wordTotal
lineIndex
lineTotal
selectorValue
time
frame
audioAmplitude
wordStart
wordEnd
seed
```

## Esempi

```text
sin(textIndex * 0.5 + time * 4.0)
```

```text
audioAmplitude * 100
```

```text
textIndex == activeWord ? 100 : 0
```

## Regola

Non introdurre un parser separato per il testo.

**Cross-ref Gate (precondizione forward-looking, verificato)**: [`docs/EXPRESSIONS_V2_PROMOTION.md`](EXPRESSIONS_V2_PROMOTION.md) Gate 3 + [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) TICKET-EXP2-G3 — stato corrente `🔵 Planned`; design catturato in TICKET-EXP2-G3 §Audit findings, Gate 3 attualmente in `Kickoff` (no PR ancora green). Plan: Gate 3a (Path B feature parity PR, wiggle/linear/random/seedRandom/loopOut/loopIn port in `experimental/expressions/v2/vm.cpp`) → Gate 3b (AnimatedValue swap PR). Fase 13 non inizia finché Gate 3 è `🟢 Done`.

---

# Ordine operativo consigliato

## Blocco A — rendere affidabile ciò che esiste

1. Baseline verde (Fase 0).
2. Contratto testuale canonico (Fase 1, vedi `MIGRATION_TEXT_SPEC.md`).
3. Visual regression harness (Fase 2).
4. Riparazione e validazione preset esistenti (coda delle fasi 1+2).

## Blocco B — ottenere subito video migliori

5. Selector Engine V2 (Fase 3, previa chiusura Gate 3 in `EXPRESSIONS_V2_PROMOTION.md`).
6. Rich text e styling per parola (Fase 4).
7. Subtitle e word-timing pipeline (Fase 7).
8. Preset library produttiva (Fase 10).
9. Motion blur testuale (Fase 8).

## Blocco C — colmare i gap principali con After Effects

10. Text on Path (Fase 5).
11. Variable Fonts (Fase 6).
12. Expression Selector (Fase 13, dopo Gate 3).

## Blocco D — internazionalizzazione

13. ICU opzionale (Fase 9).
14. Test multilingua completi (parte di Fase 9).
15. Color glyph e fallback emoji (parte di Fase 9).

## Blocco E — effetti premium

16. Text 3D con libtess2 (Fase 11).
17. MSDF con msdfgen (Fase 12).
18. Morph e displacement avanzati (Fase 12 estensioni).

---

# Priorità per impatto

| Priorità | Feature                     | Impatto | Complessità |
| -------: | --------------------------- | ------: | ----------: |
|        1 | Visual regression           | Altissimo | Media |
|        2 | Preset kinetic typography   | Altissimo | Media |
|        3 | Word timing e subtitle      | Altissimo | Media |
|        4 | Styling per parola          | Altissimo | Media |
|        5 | Wiggly/Random/Wave selector |     Alto | Media |
|        6 | Text on Path                |     Alto | Media |
|        7 | Variable Fonts              |     Alto | Media |
|        8 | Motion Blur                 |     Alto | Medio-alta |
|        9 | ICU                         | Alto per globalizzazione | Media |
|       10 | Expression Selector         |     Alto | Alta |
|       11 | Text 3D                     | Medio-alto | Alta |
|       12 | MSDF                        |    Medio | Alta |

---

# Primo milestone produttivo

Deve consentire di generare automaticamente:

- titoli;
- citazioni;
- sottotitoli parola per parola;
- keyword evidenziate;
- typewriter;
- word pop;
- blur reveal;
- tracking reveal;
- slide reveal;
- titoli cinematici;
- versioni 16:9 e 9:16.

## Definition of Done

- [ ] Almeno 20 preset stabili.
- [ ] Almeno 8 preset subtitle.
- [ ] Input word-timing JSON.
- [ ] Styling per parola.
- [ ] Golden test per ogni preset.
- [ ] Rendering deterministico.
- [ ] Nessuna dipendenza GPU.
- [ ] Nessuna pipeline testuale parallela.
- [ ] Consumer SDK in grado di usare tutti i preset pubblici.
- [ ] Documentazione con esempi completi.

---

# Regola di chiusura per fase

Una fase è chiusa quando il dettaglio canonico (cross-ref in cima a questo documento) è in stato verificato macchina, secondo [`docs/DEFINITION_OF_DONE.md`](DEFINITION_OF_DONE.md) e [`docs/stabilization-plan/09-document-canonicalization.md`](stabilization-plan/09-document-canonicalization.md). Regole generali di chiusura documento/stato: [`AGENTS.md`](../AGENTS.md) e [`docs/stabilization-plan/README.md`](stabilization-plan/README.md).

L'ordine dei Blocchi A–E nel prossimo paragrafo è **per impatto produttivo, non per numero di fase** — Fase 13 può precedere Fase 11/12 in esecuzione benché numericamente venga dopo.
