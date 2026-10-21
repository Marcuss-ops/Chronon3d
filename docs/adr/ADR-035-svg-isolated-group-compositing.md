# ADR-035 — SVG isolated group compositing

## Stato
ACTIVE (2026-10-18). Owner: render-plan/compiler + source-pass.

## Contesto

SVG defines `<g opacity="a">` as an **isolated group**: the children render
onto their own offscreen surface at full paint alpha and the surface is then
composited **once** with the group alpha. Chronon3D today folds the group
opacity into each child's paint alpha (`svg_path_loader.cpp` style
inheritance → `element.opacity` → `render_plan_compiler_shape.cpp` fills the
folded alpha into `stroke/fill`), and the layer surface receives every child
node through the per-item alpha fold of `MultiSourceNode::execute()`. Where
children of one group overlap, each child darkens the shared surface
independently, so a 0.5-opacity group with two overlapping siblings renders
`0.5·top + 0.5·bottom + …` while SVG/AE/After Effects compositing expects the
union to fade by `0.5` **after** stacking.

TICKET-SVG-RENDER-PLAN records this as bounded-out with the note that a fix
"needs an offscreen group node in the render graph, not an importer change".

## Decisione

**Estendere `MultiSourceNode` con un ramo "gruppo isolato", senza nuovi node
kind, executor o registry.**

1. **Annotazione** — il loader SVG annota ogni elemento con lo stack dei gruppi
   isolati che lo avvolgono, outermost-first, e l'opacità di ciascun livello
   (`SvgStyledElement::groups`).
2. **Lowering** — il render-plan compiler smette di moltiplicare
   `group_opacity` nell'alpha di fill/stroke dei figli; l'opacità
   **propria** dell'elemento (`element.opacity`, `animate opacity`) resta
   piegata nel paint perché è correttamente fonte di alpha del singolo
   paint. I gruppi viaggiano come `ImportedSubpathPlan::svg_groups`.
3. **Builder** — gli item `MultiSourceItem` dell'aggregato del layer
   trasportano un `std::vector<GroupIsolationSpec>` outermost-first.
4. **Esecuzione** — dentro `MultiSourceNode::execute()`, gli item sono
   disegnati **a alpha di paint piena** sulla superficie del gruppo più
   interno. Alla chiusura, ogni gruppo è compositato una volta nella superficie
   del genitore; il gruppo radice viene compositato sulla fb del layer. Ogni
   superficie è ripulita all'apertura.
5. **Backend** — il percorso software usa l'oracolo pixel esistente. Quando
   `supports_native_surfaces()` è vero, il gruppo usa un framebuffer GPU
   trasparente e `composite_surface_weighted()` per comporre l'intermedio
   sul framebuffer del genitore o del layer. In modalità `require_gpu_native`, superfici
   mancanti o un errore del compositor falliscono esplicitamente; non esiste
   fallback CPU nascosto. La parità hardware è certificata dal gate Vulkan
   quando `CHRONON_GPU_CLI` punta a una build SVG+Vulkan.

## Perché non un nuovo GroupNode

- `AGENTS.md` / `tools/architecture_rules.toml`: un nuovo kind di nodo
  richiederebbe rami in ogni pass del builder e nell'executor-map; il punto
  canonico dove "più source di uno stesso layer" vive è già
  `MultiSourceNode`, con precedente interno (ramo `text_run`).
- Le argomentazioni sopra dipendono dallo stack del layer: un nuovo nodo
  parallelo frantumerebbe il bbox/clip pass già condiviso.

## Oracle

Per un pixel `p` nella sovrapposizione di due figli di stesso gruppo
(opacità del gruppo `g`, colore del figlio superiore `c_top`, della
destinazione con base `d`):

    atteso = lerp(d, c_top, g)        # il figlio inferiore NON compare
    errato (fold attuale) = lerp(d, lerp(bottom, top, α_child…), g)

Il gate (`scripts/ci/check_svg_group_isolation_gate.py`) certifica: il pixel
di sovrapposizione non dipende dal colore del figlio inferiore e l'alpha
globale scala con `g`; endpoint frame hash differenti (animazione), repeat
render indentico (determinismo).

## Demolition debt

**Owner**: render-plan/compiler. **Reason**: i path SVG usano ora lo stack
isolato per l'opacità, ma la gerarchia animata dei transform e i run di testo
non sono ancora integrati. **Exit condition**: fixture del corpus con gruppi
path, testo e transform annidati verificati; nessun fallback che pieghi
l'opacità di gruppo nel paint. **Equivalence test/gate**:
`check_svg_motion_animations_gate.py` e `check_svg_group_isolation_gate.py`
verdi su fixture annidati. **Status**: `ACTIVE`.

## Conseguenze

- Positive: resa letterale dell'alpha SVG; completa i gruppi annidati di path e sblocca Q3
  (clip per elemento su sub-layer) sulla stessa meccanica; costo runtime
  limitato a layer con gruppi isolati (zero su tutte le altre).
- Negative/note: un framebuffer intermedio GPU per gruppo per frame dal pool
  (residency, nessuna nuova cache family) e un passaggio pesato per gruppo;
  la misurazione a 1080p con gruppi multipli resta da completare.
