# TICKET-TRN-TRANSITION-CLEANUP — Transitions cleanup: layer, camera, text

## Stato

OPEN (master tracker)

## Scopo

Raccogliere e tracciare lo stato attuale di tutti i sistemi di transizione in
Chronon3D (LayerReveal, Camera e Text), classificarli e guidare la pulizia e
certificazione **prima** di aggiungere nuove transizioni.

Questo ticket è il master tracker. Sostituisce/integra `TICKET-TRN-01`
(inventario + gate architetturale), che viene considerato incorporato in questa
vista d’insieme.

---

# 1. Stato attuale: tre sistemi distinti

| Sistema | Scopo | Stato attuale |
|---|---|---|
| `TransitionNode` | Entrata/uscita di un singolo layer | Presente, da ripulire |
| `CameraTransition` | Passaggio tra due shot camera | Presente, semanticamente parziale |
| `SourceTextTransition` | Cambio del contenuto testuale | Presente, con semantiche errate |

Questi tre sistemi **non devono fondersi**, ma devono condividere timing,
progress, easing, validazione e determinismo.

---

# 2. Transizioni dei layer (`TransitionNode`)

## 2.1 Cosa abbiamo oggi

`TransitionNode` riceve un `LayerTransitionSpec` con:

* `transition_id`;
* direzione;
* durata in secondi;
* delay;
* easing.

ID trovati nel codice:

```text
none
crossfade
slide
wipe_linear
smooth_wipe
circle_iris
flash
procedural_remotion
remotion
```

## 2.2 Problema critico: transition-out ignorata

Nel graph builder la logica è approssimativamente:

```cpp
if (has_in_trans) {
    // usa transition_in
} else if (has_out_trans) {
    // usa transition_out
}
```

Se un layer ha entrambe, viene costruita solo la transition-in. La
transition-out viene ignorata.  un **P0**.

Percorso corretto candidato:

```text
source
→ transition-in
→ effetti intermedi del layer
→ transition-out
→ compositing
```

o, se il contratto prevede entrambe dopo gli effetti:

```text
source
→ effetti
→ transition-in
→ transition-out
→ compositing
```

La posizione esatta va decisa una volta e bloccata con test.

## 2.3 Non è ancora una transizione tra due clip

`TransitionNode::execute()` usa soltanto `inputs[0]`. Non riceve un
framebuffer uscente A e uno entrante B. Oggi tutte le transizioni sono
effetti su un singolo layer, non vere transizioni editoriali tra due clip.

Una vera dissolve richiede:

```cpp
output = A * (1.0f - progress) + B * progress;
```

rispettando alpha premoltiplicato e spazio colore.

## 2.4 Cache key incompleta

La chiave cache include ID, `is_out`, direzione. Non include:

* durata;
* delay;
* easing;
* tutti i parametri specifici;
* seed;
* versione del descriptor.

## 2.5 Dispatch string-based e fallback silenzioso

L’implementazione è una catena `if (id == "...")`. Un ID sconosciuto arriva
all’ultimo `else` e copia semplicemente il framebuffer sorgente. Deve diventare
fail-loud:

```text
UnknownTransitionId
InvalidTransitionDirection
InvalidDuration
InvalidParameter
UnsupportedTransitionConfiguration
```

## 2.6 Parametri hardcoded

Diversi parametri sono cablati nel renderer:

| ID | Parametro | Valore hardcoded |
|---|---|---|
| `smooth_wipe` | feather | `0.1` |
| `circle_iris` | centro | fisso al centro canvas |
| `circle_iris` | feather radius | `10%` del raggio |
| `flash` | colore | bianco |
| `procedural_remotion` | seed, colori | `1.2`, arancio/bianco |
| `remotion` | speed, direction, angle | `1.35`, `3.0`, `0.0` |

Devono diventare parametri tipizzati del descriptor.

## 2.7 `predicted_bbox()` non affidabile

`predicted_bbox()` restituisce sempre il bbox dell’input. Non è corretto per
slide, blur, flash, glow procedurali, trasformazioni che spostano il contenuto.
Può compromettere dirty regions, tile pruning e cache parziale.

---

# 3. Transizioni camera (`CameraTransition`)

## 3.1 Cosa abbiamo oggi

Il sistema camera possiede:

* `Cut`;
* `SmoothBlend`;
* `Push`;
* `WhipPan`;
* `FocusHandoff`;
* `ShotTimeline`;
* sessioni per shot;
* catalogo DI;
* cache per random access.

## 3.2 Nomi vs comportamento

| Transizione | Nome suggerito alternativo / Problema |
|---|---|
| `Push` | `EaseOutCameraBlend`; non è un vero push editoriale con traiettoria/direzione/overshoot |
| `WhipPan` | Manca overshoot, curva di accelerazione specifica, shutter coupling, motion-blur envelope, settle finale |
| `FocusHandoff` | Quasi uguale a `SmoothBlend`; un focus handoff credibile richiede defocus/intervallo morbido e curva separata dal movimento camera |

## 3.3 Semantica dell’overlap poco chiara

Il resolver avvia la transizione a:

```cpp
shot.end_frame - shot.transition_frames
```

anche se lo shot successivo inizia solo a `shot.end_frame`. Durante la
transizione il tempo locale dello shot successivo può essere negativo e viene
forzato a zero. Non è un overlap reale.

Bisogna scegliere un solo contratto:

* **Contratto A (overlap reale)**:
  ```text
  shot A: [0, 40)
  shot B: [30, 70)
  transition: [30, 40)
  ```
* **Contratto B (pre-roll del prossimo shot)**:
  ```text
  shot B inizia a 40
  ma viene valutato anticipatamente da 30 con local time 0
  ```

Il contratto A è preferibile.

## 3.4 Caso `transition_frames == 1`

```cpp
denom = max(1, transition_frames - 1);
t = local_idx / denom;
```

Con durata di 1 frame, l’unico campione ottiene `t = 0`, non `t = 1`. Serve una
regola esplicita:

* `0 frame` → nessuna transizione;
* `1 frame` → cut direttamente al target;
* `>=2 frame` → interpolazione inclusiva da 0 a 1.

## 3.5 Doppio stato sessione

`ShotTimelineResolver::evaluate()` riceve `ShotTimelineSession&`, ma usa la
propria `cache_` come fonte dello stato. Il parametro `timeline_session` non
viene usato. Deve rimanere un solo concetto.

## 3.6 Più percorsi per risolvere le transizioni

Il resolver:

* consulta il catalogo;
* mantiene una mappa interna;
* possiede factory `default_*`;
* usa fallback locali;
* permette `set_transition()`.

Percorso corretto:

```text
CameraTransitionKind/ID
→ CameraTransitionCatalog
→ factory
→ CameraTransition
```

## 3.7 Test camera troppo deboli

I test attuali verificano principalmente:

* un valore a metà transizione;
* posizione agli endpoint;
* assenza di crash/errori strutturati.

Mancano verifiche forti per: quaternioni, POI, lens model, optics mode, DOF,
motion blur, flag discreti, parent/hierarchy, continuità, overshoot, random
access, sequenziale/parallelo.

---

# 4. Transizioni del testo (`SourceTextTransition`)

`AnimatedTextDocument` supporta:

* `Hold`;
* `Cut`;
* `DissolveLayouts`;
* `Scramble`;
* `Morph`.

## 4.1 `Cut` non corrisponde alla documentazione

La documentazione dice che il testo cambia al boundary del keyframe successivo.
L’implementazione, invece, tra due keyframe seleziona immediatamente il
documento successivo non appena si supera il keyframe precedente.

Soluzione chiara:

```text
Hold: A rimane fino al boundary di B
Cut: identico temporalmente a Hold, ma segnala nessun blending
```

## 4.2 Il crossfade non è un vero crossfade

Durante `DissolveLayouts`:

* il testo entrante viene renderizzato a piena opacità;
* il testo uscente viene renderizzato con `1 - mix`.

A `mix = 0` entrambi sono visibili → sovraesposizione e testo doppio.
Il corretto dissolve è:

```text
incoming alpha = mix
outgoing alpha = 1 - mix
```

Inoltre l’header parla di interpolazione delle posizioni dei glifi, ma il
percorso attuale costruisce due layout separati e li sovrappone. Bisogna
scegliere:

* rinominarla `DissolveLayouts`, se deve solo dissolvere;
* oppure implementare realmente il matching dei glifi e il movimento tra layout.

## 4.3 Effetti non simmetrici

Padding e rendering delle shadow sono applicati al lato attivo, non al lato
outgoing. In una transizione corretta entrambi i lati devono avere il proprio
fill, stroke, shadow, blur, material, font spans, bounding box.

## 4.4 I test crossfade sono solo no-crash

La suite crossfade con stroke dichiara esplicitamente che il contratto
principale è no-crash e non verifica output pixel-level. Serve certificazione
visiva.

---

# 5. Il test timeline chiamato “Transition” non testa una transizione visiva

Nella certification suite, il test `Timeline.Transition` verifica soltanto
`trim_before`, cioè lo spostamento del frame locale della sequence. Non deve
essere considerato una certificazione di transizioni visive.

Rinomina proposta: `Timeline.TrimBeforeRemap`.

---

# 6. Architettura da raggiungere

Non creare un enorme motore unico per tutto. Condividere soltanto il contratto
temporale.

```text
TransitionTiming
    ├── start
    ├── duration
    ├── delay
    ├── easing
    ├── direction
    └── normalized progress
```

Quindi quattro domini distinti:

```text
TransitionProgressSampler
    ├── LayerRevealProgram
    ├── ClipTransitionProgram
    ├── CameraTransitionProgram
    └── TextTransitionProgram
```

## Sampler temporale unico

Una sola funzione canonica:

```cpp
struct TransitionSample {
    float linear_progress;
    float eased_progress;
    bool before;
    bool active;
    bool after;
};

TransitionSample sample_transition(
    SampleTime time,
    SampleTime start,
    SampleDuration duration,
    EasingCurve easing
);
```

Da usare da:

* `TransitionNode`;
* camera shot transitions;
* text document transitions;
* future clip transitions.

Niente più smoothstep locale, ease-out scritto a mano, progress basato sul gap,
conversioni diverse frame/secondi, fallback a 30 fps.

---

# 7. Sequenza di pulizia consigliata

## TRN-01 — Inventario e gate architetturale

* registrare tutte le transizioni esistenti;
* classificare `LayerReveal`, `Clip`, `Camera`, `Text`;
* trovare tutti i confronti su stringhe;
* trovare progress sampler locali;
* impedire l’aggiunta di nuovi `if (transition_id == ...)`;
* rinominare il test timeline relativo al trim.

## TRN-02 — Timing canonico

* introdurre `TransitionProgressSampler`;
* usare `SampleTime` e `FrameRate` razionale;
* definire `[start, end)`;
* definire durata 0, 1 e 2 frame;
* definire endpoint 0 e 1;
* centralizzare easing;
* aggiungere test per 23.976, 24, 25, 29.97, 30, 50 e 60 fps.

## TRN-03 — Pulizia delle transizioni layer

* supportare contemporaneamente in e out;
* descriptor tipizzato;
* eliminare il lungo dispatch string-based;
* unknown ID fail-loud;
* includere tutti i parametri nella cache key;
* correggere predicted bbox;
* trasformare feather, colore, seed, speed e centro in parametri;
* separare generatori di maschera dal compositing.

Struttura target:

```text
TransitionCatalog
    → TransitionDescriptor
    → compile_transition()
    → TransitionProgram
    → evaluate_mask()/evaluate_pixel()
```

## TRN-04 — Correttezza delle transizioni testo

* correggere `Cut`;
* incoming alpha = `mix`;
* outgoing alpha = `1 - mix`;
* applicare effetti simmetricamente;
* decidere `DissolveLayouts` contro vero `MorphLayouts`;
* aggiungere golden frame 0%, 25%, 50%, 75%, 100%.

## TRN-05 — Pulizia camera

* decidere overlap reale;
* correggere durata di un frame;
* rimuovere lo stato sessione duplicato;
* usare soltanto il catalogo;
* definire una policy per ogni campo della camera;
* rinominare transizioni che non rispettano ancora il comportamento promesso;
* testare continuità quaternion e tutti i parametri.

## TRN-06 — Certificazione delle transizioni già presenti

Matrice minima:

```text
16:9 e 9:16
ingresso e uscita
durata 1, 2, 10, 30 frame
linear e tre easing
quattro direzioni
cache cold/warm
scheduler seriale/parallelo
accesso sequenziale/casuale
alpha trasparente e opaco
frame iniziale, medio, finale
```

Solo quando questa matrice è verde ha senso aggiungere nuove transizioni.

## TRN-07 — Vera transizione tra clip

Iniziare soltanto con:

1. `Cut`;
2. `Dissolve`.

Dopo Cut e Dissolve corretti e certificati:

* Push;
* Slide;
* Linear Wipe;
* Smooth Wipe;
* Iris;
* Blur Dissolve;
* Zoom;
* Flash;
* effetti procedurali.

---

# 8. Test obbligatori

## Timing

* `duration = 0, 1, 2, 30`;
* frame prima dell’inizio, primo frame, frame centrale, ultimo frame, frame
  successivo;
* progress finito e dentro `[0,1]`;
* endpoint esatti;
* easing monotono quando previsto;
* stesso `SampleTime` → stesso risultato.

## Layer

* in e out sullo stesso layer;
* unknown ID produce errore;
* cambiare easing/durata cambia cache key;
* direzione Left/Right speculare;
* nessun pixel inatteso ai bordi;
* bbox previsto contiene bbox reale.

## Testo

* nessun aumento di luminosità al 50%;
* outgoing e incoming hanno alpha complementare;
* stroke e shadow presenti su entrambi;
* testo lungo contro corto, font diversi, RTL, grapheme ed emoji;
* random access identico alla sequenza.

## Camera

* tutti i campi uguali a `from` con `t=0` e a `to` con `t=1`;
* nessun NaN in 100 campioni;
* rotazione `179° → -179°` segue l’arco breve;
* durata un frame, overlap esatto, gap rifiutato o gestito esplicitamente;
* accesso casuale uguale al sequenziale.

## Clip transitions future

Per Dissolve:

```text
p=0   → output identico ad A
p=0.5 → blend matematicamente previsto
p=1   → output identico a B
```

Per Slide:

* nessun buco trasparente se A e B devono coprire il canvas;
* entrambi i source si muovono secondo la stessa coordinata;
* policy chiara per risoluzioni diverse;
* clipping corretto.

---

# 9. Definition of Done prima di aggiungere altro

Non aggiungere nuove transizioni finché non sono soddisfatti:

* transition-in e transition-out funzionano insieme;
* non esiste più il dispatch monolitico per stringhe;
* ID sconosciuti falliscono chiaramente;
* cache key contiene tutti i parametri;
* timing è condiviso e frame-rate-safe;
* `Cut` del testo ha una semantica corretta;
* il crossfade del testo usa alpha complementari;
* il sistema camera ha una sola fonte dello stato;
* ogni transizione attuale possiede test iniziale, medio e finale;
* cache e random access producono output identici;
* esiste una distinzione netta tra layer reveal e clip transition.

---

# 10. Cronologia e forward points

* `TICKET-TRN-01` — inventario iniziale e gate architetturale (incorporato in
  questo ticket).
* TRN-02 — `TransitionProgressSampler` canonico.
* TRN-03 — pulizia layer transitions.
* TRN-04 — correzione transizioni testo.
* TRN-05 — pulizia transizioni camera.
* TRN-06 — certificazione matrice minima.
* TRN-07 — vera `ClipTransitionNode` (Cut/Dissolve).
