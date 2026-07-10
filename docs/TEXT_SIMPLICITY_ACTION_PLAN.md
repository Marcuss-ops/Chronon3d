# Chronon3D — Text Simplicity Action Plan

> **Obiettivo:** raggiungere l'ergonomia di Remotion per il testo in Chronon3D,
> mantenendo i vantaggi distintivi: headless, CPU-first, deterministico, server-side, senza browser.
>
> **Regola operativa:** ogni azione è un commit atomico su `main`. Nessuna branch.
> Frequente commit + push via `tools/wrap_push.sh origin main`.
>
> **Documento di supporto** — non fonte canonica di stato.
> Stato canonico: [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md).
> Requisiti canonici: [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md).
> Blocker canonici: [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md).
> Piano testo esistente: [`docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`](docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md).

---

## Stato attuale (2026-07-10)

| Componente | Stato | Note |
|---|---|---|
| TextDocument model | PASS | Spans, paragrafi, stili |
| FontEngine (FreeType + HarfBuzz + FriBidi) | PASS | Auto-creazione dal renderer |
| Layout multilinea | PASS | ParagraphComposer funzionante |
| TextAnimatorSpec + Selector | PASS | Range, Glyph, Grapheme, Word, Line |
| Authoring DSL | PASS | `authoring::Text`, `Animator`, `Selector` |
| Materiali (gradient, glow, bevel, stroke) | PASS | `TextMaterial` completo |
| TextPresetRegistry (22 preset) | PARTIAL | Registrati + invocabili, golden frame incompleti |
| Text on Path | PARTIAL | Funzionale, animazione/RTL/regressioni incomplete |
| Rich text | PARTIAL | Modello interno presente, API authoring incompleta |
| CLI `inspect-text` | PLANNED | Diagnostica visuale non esistente |
| Coordinate model ADR | PLANNED | Nessun contratto canonico Placement/Layout/Transform |
| `TextDefinition` unica | PLANN | Molteplici rappresentazioni equivalenti coesistono |
| Placement resolver unico | PLANNED | predicted_bbox può divergere dal compositor |
| Simple API builder | PLANNED | `layer.text("title").content(...).place(...)` non esiste |
| Animation helpers | PLANNED | `interpolate`, `spring`, `sequence` non esistono |
| Auto-fit | PLANNED | Feature gap (ADR-018 accepted, impl deferred) |
| Pipeline parity (render/video/CLI) | PLANNED | Nessun test cross-pipeline sistematico |

---

## Fase 1 — Correttezza (P0)

> **Principio:** niente nuova finché il contratto esistente non è verificato.

### F1.A — ADR Coordinate Model

**Ticket:** `TICKET-SIMPLICITY-COORDINATES`

Scrivere un ADR che definisce i 4 livelli di coordinate:

```text
Canvas  → coordinate globali (es. 1920×1080)
Layer   → coordinate locali del layer (parent transform applicata)
Box     → coordinate locali del text box (frame + placement)
Glyph   → coordinate dei singoli glifi (post-layout + animator)
```

**Deliverable:**
- `docs/adr/ADR-019-text-coordinate-model.md`
- Ogni trasformazione applicata una sola volta
- Esempi numerici per canvas 1920×1080
- Cross-link a `TICKET-TEXT-CLIP-PREDICTED-BBOX`

**Criterio di completamento:** ADR accepted + 3 test numerici che verificano le 4 coordinate.

**Commit:** `feat(adr): text coordinate model — 4-level Canvas/Layer/Box/Glyph`

---

### F1.B — Placement Resolver Unico

**Ticket:** `TICKET-SIMPLICITY-PLACEMENT-RESOLVER`

Creare una sola funzione responsabile della posizione:

```cpp
ResolvedTextPlacement resolve_text_placement(
    const TextDefinition& text,
    const CanvasDescriptor& canvas,
    const LayerTransform& layer
);
```

**Struct di output:**

```cpp
struct ResolvedTextPlacement {
    Rect local_frame;      // box nel canvas
    Mat4 layer_matrix;     // trasformazione layer
    Mat4 world_matrix;     // trasformazione world
    Vec2 layout_origin;    // origine per il layout testo
};
```

**Deliverable:**
- `include/chronon3d/text/text_placement_resolver.hpp`
- `src/text/text_placement_resolver.cpp`
- Test che verifica: stessa matrice usata da layout, predicted_bbox, tile pruning, rasterizzazione, compositing

**Criterio di completamento:** `TICKET-TEXT-CLIP-PREDICTED-BBOX` diagnostic PASS con lo stesso resolver.

**Commit:** `feat(text): unified placement resolver — single source of truth for text positioning`

---

### F1.C — Fallback Conservativo per BBox

**Ticket:** `TICKET-SIMPLICITY-CONSERVATIVE-BBOX`

Quando il bbox è sospetto, usare un fallback conservativo:

```cpp
if (!predicted_bbox.contains(world_ink_bbox)) {
    predicted_bbox = expand(world_ink_bbox, effect_padding);
    disable_tile_pruning = true;
}
```

**Deliverable:**
- Modifica in `src/backends/software/processors/text_run/` (text_run execution/transform)
- Test con bbox intenzionalmente stretto → verifica che pruning viene disabilitato
- Log strutturato della violazione

**Criterio di completamento:** nessuna striscia di testo visibile (19px sliver regression = 0).

**Commit:** `fix(text): conservative bbox fallback when predicted_bbox < world_ink_bbox`

---

### F1.D — FontEngine Automatico

**Ticket:** `TICKET-SIMPLICITY-AUTO-FONT`

L'utente non deve mai passare manualmente un `FontEngine*`. L'API pubblica deve dichiarare solo:

```cpp
.font("assets/fonts/Inter-Bold.ttf", 230.0f)
```

**Comportamento desiderato:**
- Font trovato → render
- Font non trovato → errore esplicito (non silenzioso)
- Fallback autorizzato → warning strutturato + render

**Deliverable:**
- Audit di tutti i messaggi `no FontEngine available` nel codebase
- Wrapper automatico nel `RenderEngine` bridge
- Test: composizione con font inesistente → errore catturato

**Criterio di completamento:** zero messaggi `no FontEngine available` in CLI, render, video e test.

**Commit:** `feat(text): automatic FontEngine creation — zero manual FontEngine* in public API`

---

### F1.E — Contratto Automatico di Visibilità

**Ticket:** `TICKET-SIMPLICITY-VISIBILITY-CONTRACT`

Per ogni `TextRun`, verificare:

```text
font valido
glyph_count > 0
layout bbox valido
predicted_bbox contiene world_bbox
clip_rect contiene l'ink visibile
alpha bbox finale non vuoto
```

**Deliverable:**
- Funzione `verify_text_visibility()` chiamata post-render
- Violazioni registrate come structured diagnostic
- Estendere `tests/text_golden/text_completeness/` con test per ogni condizione

**Criterio di completamento:** TextCompleteness suite PASS + zero false-negative su testo invisibile.

**Commit:** `feat(text): automatic visibility contract — verify font/glyph/bbox/clip post-render`

---

## Fase 2 — Semplificazione (P1)

> **Principio:** un solo modello dati, un solo percorso API.

### F2.A — TextDefinition Canonica

**Ticket:** `TICKET-SIMPLICITY-TEXTDEFINITION`

Tutte le API devono produrre la stessa struttura:

```cpp
struct TextDefinition {
    TextContent content;       // testo + spans
    TextStyle style;           // font, size, color, stroke
    TextFrame frame;           // size, placement, offset
    ParagraphStyle paragraph;  // align, overflow, wrap
    TextEffects effects;       // glow, shadow, bevel, blur
    TextAnimation animation;   // selectors, properties, timing
};
```

**Deliverable:**
- `include/chronon3d/text/text_definition.hpp` (o estensione di `text_document.hpp`)
- Adapter da `TextSpec`, `text_run()`, preset → `TextDefinition`
- Test: `text()`, `text_run()` e preset producono tutti `TextDefinition`

**Criterio di completamento:** nessuna rappresentazione equivalente duplicata per font size, position, anchor, alignment, box, overflow.

**Commit:** `feat(text): canonical TextDefinition — single internal representation for all text APIs`

---

### F2.B — Simple API Builder

**Ticket:** `TICKET-SIMPLICITY-BUILDER`

API semplice per l'80-90% dei casi:

```cpp
layer.text("title")
    .content("PULSE GLOW")
    .font("assets/fonts/Inter-Bold.ttf", 230.0f)
    .frame({1700.0f, 360.0f})
    .place(TextPlacement::CanvasCenter)
    .align(HorizontalAlign::Center, VerticalAlign::Middle)
    .color(Color::white())
    .commit();
```

**Posizionamenti leggibili:**

```cpp
.place(TextPlacement::CanvasCenter)
.place(TextPlacement::TopLeft)
.place(TextPlacement::BottomCenter)
.place(TextPlacement::Absolute({960, 540}))
.place(TextPlacement::SafeAreaBottom)
```

**Offset separato dal placement:**

```cpp
.place(TextPlacement::CanvasCenter)
.offset({0, -100})
```

**Deliverable:**
- Estensione di `include/chronon3d/authoring/text.hpp` (già esiste `authoring::Text`)
- `TextPlacement` enum in `include/chronon3d/text/text_placement.hpp`
- Test: titolo centrato < 10 righe leggibili

**Criterio di completamento:** un titolo centrato richiede meno di dieci righe.

**Commit:** `feat(text): simple builder API — .text().content().font().place().commit()`

---

### F2.C — text() e text_run() come Adapter

**Ticket:** `TICKET-SIMPLICITY-ADAPTERS`

`text_run()` e `centered_text()` diventano adapter verso il builder canonico:

```text
text_run()       → layer.text("name").content(...).commit()
centered_text()  → layer.text("name").content(...).place(CanvasCenter).commit()
glow_text()      → layer.text("name").content(...).effects(glow).commit()
```

**Deliverable:**
- Adapter interni (non eliminare ancora le vecchie API)
- Test di parità: stessa `TextDefinition` prodotta
- Deprecation warnings per `centered_text()` e `glow_text()`

**Criterio di completamento:** adapter producono `TextDefinition` identica al builder diretto.

**Commit:** `refactor(text): text_run/centered_text/glow_text as adapters to canonical builder`

---

### F2.D — Migrazione Composizioni

**Ticket:** `TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS`

Migrare le composizioni esistenti al nuovo builder:

```text
content/text_placement/text_placement_compositions.cpp → builder API
content/animation_compositions.cpp                     → builder API (testo)
tests/text_golden/                                     → builder API
```

**Deliverable:**
- Almeno 5 composizioni migrate
- Golden test identici pre/post migrazione
- Zero regressioni visive

**Criterio di completamento:** tutte le composizioni testuali usano il builder canonico.

**Commit:** `refactor(content): migrate text compositions to canonical builder API`

---

## Fase 3 — Ergonomia (P1)

> **Principio:** la complessità deve essere nascosta, non assente.

### F3.A — Animation Helpers

**Ticket:** `TICKET-SIMPLICITY-ANIMATION`

API frame-driven chiara:

```cpp
layer.text("title")
    .opacity(
        animate(0.0f, 1.0f)
            .from_frame(0)
            .to_frame(15)
            .ease(Easing::OutCubic)
    )
    .scale(
        spring(Vec2{0.8f}, Vec2{1.0f})
            .start_frame(0)
    );
```

**Funzioni fondamentali:**

```cpp
interpolate(ctx.frame, {0, 15}, {0.0f, 1.0f});
spring(from, to).start_frame(0);
sequence(clip_a, clip_b, clip_c);
stagger(elements, delay_per_element);
loop(animation, count);
delay(animation, frames);
ease(t, Easing::InOutCubic);
clamp(value, min, max);
```

**Deliverable:**
- `include/chronon3d/animation/interpolate.hpp`
- `include/chronon3d/animation/spring.hpp`
- `include/chronon3d/animation/sequence.hpp`
- Test: ogni funzione con frame 0, 15, 30

**Criterio di completamento:** gli esempi base di Remotion riproducibili con complessità simile.

**Commit:** `feat(anim): interpolate/spring/sequence/loop/delay helpers for frame-driven animation`

---

### F3.B — Placement Leggibili + Safe Areas

**Ticket:** `TICKET-SIMPLICITY-PLACEMENT`

```cpp
TextPlacement::CanvasCenter
TextPlacement::TopLeft / TopCenter / TopRight
TextPlacement::CenterLeft / Center / CenterRight
TextPlacement::BottomLeft / BottomCenter / BottomRight
TextPlacement::SafeAreaTop / SafeAreaBottom
TextPlacement::Absolute({x, y})
```

**Deliverable:**
- Enum `TextPlacement` con 12+ varianti
- `SafeArea` config (margini per 9:16, 16:9, 1:1)
- Test per ogni placement su 1920×1080 e 1080×1920

**Criterio di completamento:** `.place(TextPlacement::SafeAreaBottom)` funziona su tutti gli aspect ratio.

**Commit:** `feat(text): readable placement enum with safe area support`

---

### F3.C — Preset Riutilizzabili

**Ticket:** `TICKET-SIMPLICITY-PRESETS`

```cpp
title_centered("PULSE GLOW").font_size(230).max_width(1700);
subtitle_bottom("Subscribe").font_size(56);
caption_safe_area("© 2026").font_size(32);
kinetic_word("HELLO").selector(Word).animate(...);
lower_third("Name", "Title").style(subtitle_style);
```

**Ogni preset deve:**
- Produrre `TextDefinition`
- Non calcolare coordinate proprie
- Non avere cache propria
- Non usare un resolver proprio

**Deliverable:**
- `include/chronon3d/presets/text_presets.hpp`
- 5 preset base: `title_centered`, `subtitle_bottom`, `caption_safe_area`, `kinetic_word`, `lower_third`
- Test golden per ogni preset (3 timestamp × 2 AR)

**Criterio di completamento:** 5 preset stabili con golden frame verificati.

**Commit:** `feat(presets): 5 reusable text presets — title/subtitle/caption/kinetic/lower_third`

---

## Fase 4 — Affidabilità di Prodotto (P1-P2)

> **Principio:** lo stesso testo deve produrre lo stesso risultato ovunque.

### F4.A — Pipeline Parity

**Ticket:** `TICKET-SIMPLICITY-PIPELINE-PARITY`

Verificare parità tra:

```text
renderer diretto
CLI render
CLI video
warmup on/off
tile pruning on/off
seriale/parallelo
Debug/Release
```

**Metriche confrontate:**

```text
glyph count
layout bbox
world bbox
predicted bbox
alpha bbox
hash finale
```

**Deliverable:**
- `tests/text_golden/text_parity/pipeline_parity.cpp`
- Test che esegue lo stesso testo in 6+ configurazioni
- Max differenza geometrica: 1-2 pixel

**Criterio di completamento:** tutte le combinazioni passano con differenza ≤ 2px.

**Commit:** `test(text): pipeline parity — same text across render/video/CLI/warmup/pruning`

---

### F4.B — CLI `inspect-text`

**Ticket:** `TICKET-SIMPLICITY-INSPECT-TEXT`

Comando headless diagnostico:

```bash
chronon3d_cli inspect-text composition_id --frame 15
```

**Output JSON:**

```json
{
  "node": "title",
  "font": "Inter-Bold.ttf",
  "glyph_count": 10,
  "frame": [110, 360, 1700, 360],
  "local_bbox": [12, 42, 1280, 220],
  "world_bbox": [320, 430, 1600, 650],
  "predicted_bbox": [320, 430, 1600, 650],
  "alpha_bbox": [328, 441, 1592, 638],
  "status": "PASS"
}
```

**Deliverable:**
- `apps/chronon3d_cli/commands/inspect_text.cpp`
- JSON output machine-readable
- Opzionale: PNG diagnostico con box, baseline, ink bbox, predicted bbox, clip rect, anchor

**Criterio di completamento:** `inspect-text` restituisce status PASS per testo visibile, FAIL per testo clippato.

**Commit:** `feat(cli): inspect-text diagnostic — JSON output for text node analysis`

---

### F4.C — Diagnostic Overlay

**Ticket:** `TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY`

PNG diagnostico opzionale con overlay visuale:

```text
┌─────────────────────────┐
│ ┌─ text box ──────────┐ │
│ │  baseline ─────────  │ │
│ │  ink bbox ┌──────┐  │ │
│ │           │ GLYPH │  │ │
│ │           └──────┘  │ │
│ │  predicted ┌──────┐ │ │
│ │            │      │ │ │
│ │            └──────┘ │ │
│ │  anchor ●            │ │
│ └──────────────────────┘ │
└─────────────────────────┘
```

**Deliverable:**
- Funzione `render_text_diagnostic_overlay()` in `src/text/`
- CLI flag `--diagnostic-png` su `inspect-text`
- Test: verifica che il PNG contiene i bbox disegnati

**Criterio di completamento:** `inspect-text --diagnostic-png` produce un PNG leggibile con tutti i bbox.

**Commit:** `feat(text): diagnostic overlay — visual bbox/anchor/baseline debug PNG`

---

## Fase 5 — Pulizia (P2)

> **Principio:** deprecare gradualmente, non eliminare tutto subito.

### F5.A — Deprecazioni Graduali

**Ticket:** `TICKET-SIMPLICITY-DEPRECATION`

Ordine di deprecazione:

```text
1. introdurre la nuova API (Fase 2)
2. migrare content e test (F2.D)
3. rendere gli helper vecchi adapter (F2.C)
4. aggiungere warning di deprecazione
5. eliminare i vecchi percorsi
```

**Candidati:**

| API | Motivo | Azione |
|---|---|---|
| `centered_text()` | troppe decisioni implicite | adapter → deprecation → removal |
| `glow_text()` | pipeline separata | adapter → deprecation → removal |
| `TextSpec.position` | semantica non esplicita | sostituito da `.place()` + `.offset()` |
| `pin_to + centered` | combinazione ambigua | unificato in `TextPlacement` |
| `text_run()` per casi semplici | non necessario | adapter → deprecation warning |

**Deliverable:**
- Deprecation warnings con migration path in ogni caso
- `tools/check_deprecated_text_api.sh` gate
- Test: nessun utilizzo deprecato nelle composizioni canoniche

**Criterio di completamento:** zero utilizzi di API deprecate nelle composizioni produttive.

**Commit:** `refactor(text): deprecate legacy text helpers — migration path documented`

---

### F5.B — Documentazione Copy-Paste First

**Ticket:** `TICKET-SIMPLICITY-DOCS`

La documentazione deve iniziare con esempi immediati:

### Testo centrato

```cpp
layer.text("title")
    .content("HELLO")
    .font("Inter-Bold.ttf", 160)
    .place(TextPlacement::CanvasCenter)
    .commit();
```

### Testo in basso

```cpp
layer.text("caption")
    .content("Subscribe for more")
    .font("Inter-Regular.ttf", 56)
    .place(TextPlacement::SafeAreaBottom)
    .commit();
```

### Testo animato

```cpp
layer.text("title")
    .content("HELLO")
    .place(TextPlacement::CanvasCenter)
    .opacity(interpolate(ctx.frame, {0, 15}, {0, 1}))
    .commit();
```

**Deliverable:**
- `docs/TEXT_QUICKSTART.md` con 10 esempi copy-paste
- Aggiornamento `README.md` con link al quickstart
- Esempi funzionanti in `examples/text_simplicity/`

**Criterio di completamento:** un nuovo utente può centrare un titolo in < 5 minuti.

**Commit:** `docs(text): quickstart guide — 10 copy-paste examples for text rendering`

---

## Sequenza di implementazione consigliata

```text
FASE 1 (Correttezza)        → 5 commit (F1.A → F1.E)
FASE 2 (Semplificazione)    → 4 commit (F2.A → F2.D)
FASE 3 (Ergonomia)          → 3 commit (F3.A → F3.C)
FASE 4 (Affidabilità)       → 3 commit (F4.A → F4.C)
FASE 5 (Pulizia)            → 2 commit (F5.A → F5.B)
                             ─────────────────────
                             17 commit totali
```

**Regola:** un commit per azione. Ogni commit passa `tools/wrap_push.sh origin main`.
Nessuna branch. Main only. Push frequente.

---

## Obiettivo finale

```cpp
scene.layer("hero", [&](auto& layer) {
    layer.text("title")
        .content("PULSE GLOW")
        .font("assets/fonts/Inter-Bold.ttf", 230)
        .frame({1700, 360})
        .place(TextPlacement::CanvasCenter)
        .align(HorizontalAlign::Center, VerticalAlign::Middle)
        .opacity(interpolate(ctx.frame, {0, 15}, {0.4f, 0.85f}))
        .commit();
});
```

Internamente: **una definizione, un compiler, un resolver, una cache, un bbox, una pipeline.**

---

## Non-goal

- Text 3D
- MSDF
- Morph avanzato
- Supporto globale ICU completo
- Nuovo renderer testuale parallelo
- GUI o browser nel core
- GPU obbligatoria
- JSON come formato principale

---

## Cross-link canonici

- [`docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`](docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md) — piano testo esistente (10 blocchi)
- [`docs/ROADMAP.md`](docs/ROADMAP.md) §M1 Text Production V1 — milestone prodotto
- [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md) — stato corrente
- [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md) — requisiti di release
- [`docs/adr/ADR-018-auto-fit-text.md`](docs/adr/ADR-018-auto-fit-text.md) — auto-fit design
- [`docs/adr/ADR-014-text-golden-coverage.md`](docs/adr/ADR-014-text-golden-coverage.md) — golden coverage

---

_Limite raccomandato: 300 righe (vedi `DOCUMENTATION_GOVERNANCE.md` §10)._
