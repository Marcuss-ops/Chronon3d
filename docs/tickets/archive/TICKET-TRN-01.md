# TICKET-TRN-01 — Transition inventory and architecture gate

> **Nota:** questo ticket è raccolto nel master tracker
> [`TICKET-TRN-TRANSITION-CLEANUP`](TICKET-TRN-TRANSITION-CLEANUP.md).
> Le informazioni qui sotto restano valide per il solo TRN-01, ma lo stato
> d’insieme e i forward points vengono mantenuti nel ticket master.

## Stato

DONE (gate attivo e catalogo sincronizzato)

## Scopo

Mappare tutti i sistemi di transizione presenti in Chronon3D, classificarli per
dominio e introdurre un gate architetturale che blocchi l'aggiunta di nuovi
`if (transition_id == ...)` senza passare per il catalogo/ADR previsto.

Nessuna modifica alla logica di runtime in questo ticket.

---

## 1. Sistemi identificati

| Dominio | Entry point principale | Tipo attuale | Note |
|---|---|---|---|
| Layer reveal | `src/render_graph/nodes/transition_node.cpp` | `TransitionNode` | Transizioni in/out di un singolo layer |
| Clip | `src/render_graph/nodes/clip_transition_node.cpp` | `ClipTransitionNode` | Transizioni tra due sorgenti A e B (Cut, Dissolve, Push, ...) |
| Camera | `src/scene/camera/camera_v1/shot_timeline.cpp` | `CameraTransition` + catalogo | Transizioni tra shot |
| Text | `src/text/animated_text_document.cpp` | `SourceTextTransition` | Transizioni di contenuto testuale tra keyframe |

Questi quattro sistemi non devono fondersi, ma devono condividere timing/progress,
easing, validazione e determinismo.

---

## 2. Inventario LayerReveal (`TransitionNode`)

> **Nota di classificazione:** gli ID elencati qui sotto sono tutti **LayerReveal**
> transizioni, cioè effetti di entrata/uscita applicati a un singolo layer. Le
> vere **Clip transitions** (transizioni tra due sorgenti A e B come Cut/Dissolve)
> sono gestite da `ClipTransitionNode` e tracciate nella stessa riga sopra; non
> esistono ancora transizioni LayerReveal che ricevano due input.
>
> Lo string dispatch in `TransitionNode` è già stato rimosso: i programmi sono
> ora risolti attraverso `LayerTransitionCatalog`
> (`src/render_graph/transition/transition_catalog.cpp`).

### ID attuali nel catalogo LayerReveal

```text
none                  — pass-through, nessuna modifica al framebuffer
crossfade             — moltiplicazione alpha di un singolo layer
slide                 — spostamento del singolo layer
wipe_linear           — mascheramento lineare del singolo layer
smooth_wipe           — mascheramento con feather configurabile
circle_iris           — iride circolare dal centro del canvas
flash                 — flash configurabile sul layer singolo
procedural_remotion   — effetto procedurale con seed configurabile
remotion              — effetto procedurale con speed/direction/angle configurabili
```

### Parametri (ora descriptorizzati)

| ID | Parametro | Default | Struttura |
|---|---|---|---|
| smooth_wipe | feather | `0.1f` | `SmoothWipeParams` |
| circle_iris | center | `(0.5, 0.5)` | `CircleIrisParams` |
| circle_iris | feather | `0.1f` | `CircleIrisParams` |
| flash | color | bianco | `FlashParams` |
| procedural_remotion | seed, inner/mid/outer color | `1.2f`, arancio/bianco | `ProceduralRemotionParams` |
| remotion | speed, direction, angle | `1.35`, `3.0`, `0.0` | `RemotionParams` |
| slide | distance | `1.0` | `SlideParams` |

I parametri sono ora tipizzati in `LayerTransitionParameters` e selezionati dal
`LayerTransitionCatalog` invece di essere hardcoded nel renderer.

### Progress sampler locale

`TransitionNode::compute_progress()` implementa ancora il proprio campionamento
temporale. Sarà sostituito dal `TransitionProgressSampler` canonico in TRN-02.

### Problemi noti ancora aperti

* **Cache key incompleta**: non include durata, delay, easing e parametri specifici.
* **`predicted_bbox()` non affidabile**: restituisce sempre il bbox dell'input.
* Il graph builder supporta già in e out insieme, ma i test di certificazione
  devono ancora coprire esplicitamente il caso entrambe attive.

---

## 3. Inventario ClipTransition (`ClipTransitionNode`)

| Kind | Scopo | Stato |
|---|---|---|
| `Cut` | switch istantaneo tra A e B | Implementato e testato |
| `Dissolve` | blend lineare A→B | Implementato e testato |
| `Push` | sposta A ed entra B dalla direzione opposta | Implementato |
| `Slide` | B scivola sopra A | Implementato |
| `Wipe` | maschera direzionale con feather | Implementato |
| `Iris` | iride circolare con centro/feather | Implementato |
| `Zoom` | B zooma in/da un centro | Implementato |
| `Flash` | flash di colore al centro | Implementato |

Note:
* `ClipTransitionNode` riceve due input (A e B) e produce un output.
* Il kind è un `enum class ClipTransitionKind`, non una stringa: non c'è dispatch
  string-based.
* La cache key include già molti parametri, ma la matrice di certificazione
  completa è ancora da eseguire.

---

## 4. Inventario CameraTransition

### Kind attuali

```cpp
enum class CameraTransitionKind : std::uint8_t {
    Cut = 0,
    SmoothBlend = 1,
    EaseOutBlend = 5,
    SmoothRotationBlend = 6,
    FocusDistanceBlend = 7,
};
```

I nomi legacy `Push`, `WhipPan` e `FocusHandoff` sono stati rimossi dalla
superficie pubblica dopo la migrazione dei chiamanti. I valori numerici canonici
5/6/7 restano espliciti per stabilità futura; il repository non contiene però un
decoder di transizioni camera, quindi eventuali formati esterni devono migrare
2/3/4 prima del caricamento.

### Classificazione semantica

| Kind | Comportamento attuale | Problema noto |
|---|---|---|
| `Cut` | instant switch a `t >= 1` | OK |
| `SmoothBlend` | lerp pos/POI/fov/zoom + slerp rot + focus distance | OK baseline |
| `EaseOutBlend` | lerp pos con ease-out, slerp rot | Non è un vero push editoriale |
| `SmoothRotationBlend` | slerp rot con smoothstep | Non include overshoot/accel specifici |
| `FocusDistanceBlend` | blend dei campi camera con enfasi focus distance | Curva DOF dedicata ancora da valutare |

### Progress sampler locale

`ShotTimelineResolver::evaluate()` calcola:

```cpp
int denom = std::max(1, shot.transition_frames - 1);
float t = static_cast<float>(local_idx) / static_cast<float>(denom);
```

Questo è un altro sampler locale da unificare in TRN-02.

### Problemi noti

* Semantica dell'overlap poco chiara (contratto A vs B).
* Caso `transition_frames == 1` porta a `t = 0` invece di cut.
* Doppio stato sessione (`ShotTimelineSession` parametro non usato, `cache_`
  interna al resolver).
* Percorsi multipli per ottenere le transizioni (catalogo + fallback locali).

---

## 5. Inventario SourceTextTransition

### Enum attuale

```cpp
enum class SourceTextTransition : u8 {
    Hold,
    Cut,
    DissolveLayouts,
    Scramble,
    Morph,
};
```

### Classificazione

| Transizione | Semantica attuale | Problema noto |
|---|---|---|
| `Hold` | A rimane fino al boundary di B | OK, ma documentazione va allineata |
| `Cut` | A rimane fino al boundary di B | Semantica uguale a Hold; va resa esplicita o corretta |
| `DissolveLayouts` | B a piena opacità, A con `1 - mix` | Non è un vero dissolve: sovraesposizione al 50% |
| `Scramble` | caratteri sostituiti deterministici | OK regression baseline |
| `Morph` | testo interpolato + morph_map | Contratto da chiare rispetto a DissolveLayouts |

### Progress sampler locale

`AnimatedTextDocument::sample_at()` calcola:

```cpp
const float gap = static_cast<float>(next_kf->frame - prev_kf.frame);
const float pos = static_cast<float>(frame - prev_kf.frame);
const float raw_mix = (gap > 0.0f) ? (pos / gap) : 1.0f;
const float mix = std::clamp(raw_mix, 0.0f, 1.0f);
```

Questo sarà centralizzato in TRN-02.

---

## 6. Progress sampler locali

| Dominio | Funzione | File | Dettaglio |
|---|---|---|---|
| Layer | `TransitionNode::compute_progress` | `src/render_graph/nodes/transition_node.cpp` | Converte tempo globale in progress usando `ctx.frame_input.time_seconds`, `m_spec.delay`, `m_spec.duration` e `m_is_out`. |
| Clip | `ClipTransitionNode::execute` | `src/render_graph/nodes/clip_transition_node.cpp` | Usa già `sample_transition()` ma ancora con `FrameRate` derivato da `ctx.frame_input.fps`. |
| Camera | `ShotTimelineResolver::evaluate` | `src/scene/camera/camera_v1/shot_timeline.cpp` | Calcola `t = local_idx / max(1, transition_frames - 1)` per l'overlap tra due shot. |
| Text | `AnimatedTextDocument::sample_at` | `src/text/animated_text_document.cpp` | Calcola `mix = (frame - prev_kf.frame) / (next_kf.frame - prev_kf.frame)` per la transizione di contenuto testuale. |

Tutti saranno sostituiti dal `TransitionProgressSampler` canonico in TRN-02.

---

## 7. Gate architetturale

Script: `tools/check_transition_id_dispatch.sh`

Il gate implementa tre controlli:

1. **Catalog sync**: estrae gli ID registrati in
   `src/render_graph/transition/transition_catalog.cpp` e verifica che siano
   esattamente quelli della lista canonica `EXPECTED`.
2. **No string dispatch**: scansiona `src/`, `include/`, `tests/`, `content/`
   ed `examples/` per confronti stringa su `transition_id`
   (`transition_id == "..."` o `!= "..."`).
   * Sono consentiti solo i controlli sentinella contro `"none"` e `""`
     (usati dal graph builder per decidere se inserire un `TransitionNode`).
   * Tutti gli altri confronti sono considerati bypass architetturale e
     bloccano il gate.
3. **Single source of registration**: verifica che `register_transition("...")`
   in `src/`/`include()`/`content()`/`examples()` compaia solo nel catalogo
   canonico, in modo che nessun altro file possa introurre nuovi ID LayerReveal.

Il gate emette `GATE_PASS` se tutti i controlli passano, `GATE_FAIL`
altrimenti. Non modifica il codice.

### Limitazioni note

* Il gate rileva confronti stringa espliciti sul nome `transition_id`. Se uno
  sviluppatore aliasa `spec.transition_id` in una variabile locale e confronta
  quella variabile, il pattern non viene rilevato automaticamente. Anche questo
  è considerato bypass architetturale e deve essere catturato in ADR/peer review.

## Forward points

* TRN-02 — `TransitionProgressSampler` canonico.
* TRN-03 — pulizia layer transitions (cache key completa, fail-loud, parametrizzazione).
* TRN-04 — correzione transizioni testo.
* TRN-05 — pulizia transizioni camera.
* TRN-06 — certificazione matrice minima.
* TRN-07 — espansione controllata di `ClipTransitionNode` (nuovi tipi solo dopo certificazione).
