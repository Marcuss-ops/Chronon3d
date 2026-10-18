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

1. **Annotazione** — il loader SVG annota ogni elemento con l'id del gruppo
   isolato più interno che lo avvolge e con l'opacità del gruppo
   (`SvgStyleGroupRef{ group_id, group_opacity }` su `SvgStyledElement`).
   Nested groups restano fail-closed in V1 (un solo livello,fq invariato);
   Q2 li sblocca sullo stesso pass.
2. **Lowering** — il render-plan compiler smette di moltiplicare
   `group_opacity` nell'alpha di fill/stroke dei figli; l'opacità
   **propria** dell'elemento (`element.opacity`, `animate opacity`) resta
   piegata nel paint perché è correttamente fonte di alpha del singolo
   paint. Il gruppo viaggia come `ImportedSubpathPlan::svg_group`.
3. **Builder** — gli item `MultiSourceItem` dell'aggregato del layer
   trasportano `std::optional<GroupIsolationSpec>{ group_id, opacity }`
   (default `nullopt` → percorso attuale, zero regressione su testo e
   path nativi).
4. **Esecuzione** — dentro `MultiSourceNode::execute()`, gli item di un
   gruppo isolato sono disegnati **a alpha di paint piena** su una
   framebuffer intermedia (pooled, `FramebufferPool`, famiglia
   `ResidencyCache` per la tassonomia cache), e la superficie
   intermedia è compositata sulla fb del layer **una sola volta** con
   `alpha = group_opacity`. La fb intermedia è ripulita a inizio gruppo.
5. **Backend** — il percorso software usa l'oracolo pixel esistente. Quando
   `supports_native_surfaces()` è vero, il gruppo usa un framebuffer GPU
   trasparente e `composite_surface_weighted()` per comporre l'intermedio
   sulla superficie del layer. In modalità `require_gpu_native`, superfici
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

**Owner**: render-plan/compiler. **Reason**: la piega del loader
(`style inheritance → element.opacity`) serve la V1 del gruppo annidato
fall-attuale e resta finché Q2 non migrerà la semantica su `svg_group`. 
**Exit condition**: tutti i fixture SVG del corpus usano `svg_group`; il
loader non eredita più l'opacità del gruppo. **Equivalence test/gate**:
`check_svg_motion_animations_gate.py` + nuovo `check_svg_group_isolation_gate.py`
verdi. **Removal scope**: rami fold in `svg_path_loader.cpp` +
`render_plan_compiler_shape.cpp` (moltiplicazioni `lottie_group_opacity`).
**Status**: `ACTIVE`.

## Conseguenze

- Positive: resa letterale dell'alpha SVG; sblocca Q2 (nested groups) e Q3
  (clip per elemento su sub-layer) sulla stessa meccanica; costo runtime
  limitato a layer con gruppi isolati (zero su tutte le altre).
- Negative/note: un framebuffer intermedio GPU per gruppo per frame dal pool
  (residency, nessuna nuova cache family) e un passaggio pesato per gruppo;
  la misurazione a 1080p con gruppi multipli resta da completare.
