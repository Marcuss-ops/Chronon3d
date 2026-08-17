# ADR-029 — Chronon VisualPresetRegistry (single source of truth for overlay visuals)

**Status**: PROPOSED

## Contesto

Il contratto di rendering oggi distribuisce la conoscenza "come si
renderizza un overlay" su **tre** fonti che possono divergere:

1. **PipelineGen** (Go) — `template_registry.go` mappa ogni ruolo
   semantico (`IMPORTANT_PHRASE`, `PERSON`, ...) a un preset concreto
   (`title_centered`, `entity_card`, ...).
2. **RenderingGen** (Go) — `internal/overlay/compiler.go` mantiene una
   `semanticTemplateRegistry` **duplicata** (mirror) che ri-mappa gli
   stessi ruoli a preset/animation/card/box.
3. **Chronon** (C++) — `TextPresetRegistry` governa i preset di
   tipografia a livello text-run, ma **non esiste** una registry
   canonica per i preset visuali a livello overlay.

Il risultato è la duplicazione esatta che il progetto vuole evitare: la
stessa decisione editoriale/visuale vive in due registry Go separate e
può divergere (es. `IMPORTANT_PHRASE` → `caption_safe_area` in
RenderingGen vs `title_centered` in PipelineGen).

## Decisione

Una **sola** registry canonica in Chronon:

```text
chronon3d::registry::VisualPresetRegistry
```

con descriptor:

```text
VisualPresetDescriptor
├── id
├── version
├── supported_layer      (VisualLayerKind: image | video | text | color)
├── style                (VisualStyle — default paint recipe)
├── anchor               (AnchorSpec — layout INTENT, non coordinate)
├── animation            (AnimationSpec — preset + unit word/glyph/line)
├── fallback_anchors     (ordine preferred → fallback)
└── capabilities         (tag: card, local_background, collision_avoid, ...)
```

La separazione dei ruoli diventa:

```text
PipelineGen   decide COSA mostrare → semantic_role
Chronon       decide COME renderizzarlo → visual preset + layout + style
RenderingGen  trasporta ed esegue (nessuna registry visuale)
```

PipelineGen conserva solo il `SemanticOverlayResolver`
(semantic_role → preset_id), che è una decisione editoriale. RenderingGen
non deve sapere che `PERSON` significa `lower_third_safe`.

La risoluzione finale resta a valle:

```text
VisualPresetRegistry → StyleResolver → Anchor/LayoutResolver → renderer
```

Per AGENTS.md §regole "No nuovi singleton/registry/resolver/cache senza
ADR", questo ADR è l'ancora di decisione prima che la nuova surface
pubblica atterri in `include/chronon3d/registry/`.

## Alternative considerate

- **ALT-A — seconda registry visuale in RenderingGen**: rifiutata. Crea
  una terza fonte di verità (`PipelineGen registry semantica`,
  `RenderingGen registry preset`, `Chronon registry preset`) che può
  divergere — esattamente la duplicazione da evitare.
- **ALT-B — serializzare lo stile completo per ogni preset in ogni job**:
  rifiutata. Il RenderPlan trasporterebbe 25+ proprietà duplicate per
  ogni elemento; il contratto corretto è `preset_id` + override, con la
  registry che fornisce i default (`preset defaults + job overrides =
  ResolvedVisualStyle`).
- **ALT-C — riusare `TextPresetRegistry` per i preset visuali**:
  rifiutata. `TextPresetDescriptor` modella la ricetta di motion
  per-glyph di un text-run (`builder` + `animator_factory` + `fixture`),
  non lo stile/anchor/animation di un intero layer overlay. Sono due
  livelli di astrazione distinti; forzarli nello stesso tipo violerebbe
  la single-responsibility del descriptor.

## Conseguenze

POS:

- Una sola fonte di verità per "come si renderizza un preset overlay".
- `RenderingGen` torna un execution worker stupido; il mapping
  semantico→preset resta un'unica decisione editoriale in PipelineGen.
- La registry è il punto d'aggancio canonico per StyleResolver,
  LayoutResolver e per il `resolved_plan` diagnostico (forward-point).
- Il contract test "ogni preset → RenderPlan → preset senza perdita di
  campi" ha un catalogo unico su cui iterare.

NEG:

- Nuova surface pubblica in `include/chronon3d/registry/`
  (`visual_preset_descriptor.hpp` + `visual_preset_registry.hpp`):
  Cat-2 "no nuova SDK API" implicitamente violato, giustificato da
  questo ADR (l'alternativa — nessuna registry, tre tabelle divergenti —
  è peggiore su ogni dimensione).
- L'eliminazione delle registry duplicate Go richiede i forward-point
  sotto (RenderPlan esteso + resolver), non può atterrare in un singolo
  commit senza rompere la pipeline corrente.

## Forward-points

(a) **`TICKET-VISUAL-PRESET-RENDERPLAN`** — estendere
`chronon.render-plan.v1` con `semantic_role` + `preset_id` + style
override + anchor/layout intent + animation intent + font asset,
mantenendo compatibile la forma minima `{preset, text}`.

(b) **`TICKET-VISUAL-PRESET-STYLE-RESOLVER`** — StyleResolver che fonde
`preset defaults + job overrides = ResolvedVisualStyle`.

(c) **`TICKET-VISUAL-PRESET-LAYOUT-RESOLVER`** — Anchor/LayoutResolver
V1 con regioni occupate, safe area, collision check e fallback anchors.

(d) **`TICKET-VISUAL-PRESET-DEDUP`** — rimuovere `semanticTemplateRegistry`
(RenderingGen) e `template_registry.go` (PipelineGen) sostituendoli con
il SemanticOverlayResolver + questa registry.

(e) **`TICKET-VISUAL-PRESET-PARITY`** — contract test "ogni preset →
RenderPlan → preset senza perdita di campi" + golden/parity su più
risoluzioni e lunghezze.

(f) **`TICKET-VISUAL-PRESET-MACHINE-VERIFY`** — build + ctest del test di
registry su build host funzionante (vedi ADR-025: macchina-verifica
deferred quando l'ambiente vcpkg è bloccato).

## Cross-references

- AGENTS.md §regole "No nuovi singleton/registry/resolver/cache senza
  ADR" — questo ADR è il gate.
- AGENTS.md §regole "Non duplicare registry, resolver, sampler, cache,
  service locator o checklist".
- Canon registry esistente: `include/chronon3d/registry/`
  (`text_preset_registry.hpp`, `shape_registry.hpp`).
- `RenderingGen/renderinggen/internal/overlay/compiler.go`
  (`semanticTemplateRegistry` — da eliminare in (d)).
- `refactored/internal/capabilities/overlays/template_registry.go`
  (da ridurre a SemanticOverlayResolver in (d)).

## Owner / Date

- Owner: Buffy (Freebuff) — 2026-08-17.
- Date: 2026-08-17 (drafted).
