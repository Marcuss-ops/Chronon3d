# TICKET-TRN-01 — Transition inventory and architecture gate

> **Nota:** questo ticket è stato raccolto nel master tracker
> [`TICKET-TRN-TRANSITION-CLEANUP`](TICKET-TRN-TRANSITION-CLEANUP.md).
> Le informazioni qui sotto restano valide per il solo TRN-01, ma lo stato
> d’insieme e i forward points vengono mantenuti nel ticket master.

## Stato

SUPERSEDED BY TICKET-TRN-TRANSITION-CLEANUP

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
| Camera | `src/scene/camera/camera_v1/shot_timeline.cpp` | `CameraTransition` + catalogo | Transizioni tra shot |
| Text | `src/text/animated_text_document.cpp` | `SourceTextTransition` | Transizioni di contenuto testuale tra keyframe |

Questi tre sistemi non devono fondersi, ma devono condividere timing/progress
easing, validazione e determinismo.

---

## 2. Inventario LayerReveal (`TransitionNode`)

> **Nota di classificazione:** gli ID elencati qui sotto sono tutti **LayerReveal**
> transizioni, cioè effetti di entrata/uscita applicati a un singolo layer. Non
> esistono ancora vere **Clip transitions** (transizioni tra due sorgenti A e B
> come Cut/Dissolve). Le Clip transitions sono tracciate in TRN-07.

### ID attuali nel dispatch string-based

```text
none                  — pass-through, nessuna modifica al framebuffer
crossfade             — moltiplicazione alpha di un singolo layer
slide                 — spostamento del singolo layer
wipe_linear           — mascheramento lineare del singolo layer
smooth_wipe           — mascheramento con feather fisso 0.1
circle_iris           — iride circolare dal centro del canvas
flash                 — flash bianco sul layer singolo
procedural_remotion   — effetto procedurale con seed fisso 1.2
remotion              — effetto procedurale con speed=1.35, direction=3.0
```

### Parametri hardcoded nel renderer

| ID | Parametro | Valore hardcoded | Posizione |
|---|---|---|---|
| smooth_wipe | feather | `0.1f` | `transition_node.cpp` |
| circle_iris | centro | `(w*0.5, h*0.5)` | `transition_node.cpp` |
| circle_iris | feather radius | `max_r * 0.1f` | `transition_node.cpp` |
| flash | colore | `Color::white()` | `transition_node.cpp` |
| procedural_remotion | seed | `1.2f` | `transition_node.cpp` |
| procedural_remotion | colori | arancio/bianco | `transition_node.cpp` |
| remotion | speed | `1.35f` | `transition_node.cpp` |
| remotion | direction | `3.0f` | `transition_node.cpp` |
| remotion | angle | `0.0f` | `transition_node.cpp` |

### Progress sampler locale

`TransitionNode::compute_progress()` implementa il proprio campionamento tempo-
rale. Usa `easing::apply()` ma calcola autonomamente:

* layer time in secondi da `ctx.frame_input.time_seconds`;
* offset `delay` in secondi;
* durata in secondi da `m_spec.duration`;
* branch separati per in/out.

Questo sarà sostituito da `TransitionProgressSampler` in TRN-02.

### Problemi noti

* **In e out non sono supportati insieme** nel graph builder (vedi TRN-03).
* **Cache key incompleta**: non include durata, delay, easing, parametri specifici.
* **Fallback silenzioso**: ID sconosciuto copia il framebuffer sorgente.
* **`predicted_bbox()` non affidabile**: restituisce sempre il bbox dell'input.

---

## 3. Inventario CameraTransition

### Kind attuali

```cpp
enum class CameraTransitionKind : std::uint8_t {
    Cut,
    SmoothBlend,
    Push,
    WhipPan,
    FocusHandoff,
};
```

### Classificazione semantica

| Kind | Comportamento attuale | Problema noto |
|---|---|---|
| `Cut` | istant switch a `t >= 1` | OK |
| `SmoothBlend` | lerp pos/POI/fov/zoom + slerp rot + focus distance | OK baseline |
| `Push` | lerp pos con ease-out, slerp rot | Nome "Push" non rispecchia un vero push editoriale |
| `WhipPan` | slerp rot con smoothstep | Nome promette overshoot/accel che non esistono |
| `FocusHandoff` | come SmoothBlend ma interpola solo focus distance | Quasi duplicato di SmoothBlend |

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

## 4. Inventario SourceTextTransition

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

## 5. Progress sampler locali

| Dominio | Funzione | File | Dettaglio |
|---|---|---|---|
| Layer | `TransitionNode::compute_progress` | `src/render_graph/nodes/transition_node.cpp` | Converte tempo globale in progress usando `ctx.frame_input.time_seconds`, `m_spec.delay`, `m_spec.duration` e `m_is_out`. |
| Camera | `ShotTimelineResolver::evaluate` | `src/scene/camera/camera_v1/shot_timeline.cpp` | Calcola `t = local_idx / max(1, transition_frames - 1)` per l'overlap tra due shot. |
| Text | `AnimatedTextDocument::sample_at` | `src/text/animated_text_document.cpp` | Calcola `mix = (frame - prev_kf.frame) / (next_kf.frame - prev_kf.frame)` per la transizione di contenuto testuale. |

Tutti e tre i sampler saranno sostituiti dal `TransitionProgressSampler` canonico in TRN-02.

---

## 6. Gate architetturale

Script: `tools/check_transition_id_dispatch.sh`

Blocca qualsiasi nuovo `if (m_spec.transition_id == "...")` in
`src/render_graph/nodes/transition_node.cpp` che non sia nel catalogo
LayerReveal sopra. Il gate è introdotto in questo ticket; non fa parte
ancora della catena pre-push per non rallentare il flusso incrementale.

## Forward points

* TRN-02 — `TransitionProgressSampler` canonico.
* TRN-03 — pulizia layer transitions (in/out insieme, cache key, fail-loud).
* TRN-04 — correzione transizioni testo.
* TRN-05 — pulizia transizioni camera.
* TRN-06 — certificazione matrice minima.
* TRN-07 — vera `ClipTransitionNode` (Cut/Dissolve).
