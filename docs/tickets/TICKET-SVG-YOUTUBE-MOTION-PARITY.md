# TICKET-SVG-YOUTUBE-MOTION-PARITY — Parità primitiva SVG per video motion YouTube completi

> Obiettivo del ticket: stabilire e verificare, con evidenze raw-pixel, la parità di
> **tutte le primitive SVG necessarie a produrre un video YouTube completo** (1920×1080,
> 30 fps, H.264, deterministico) senza workaround esterni: morphing, glow, filtri,
> gradienti, testo animato, marker, reveal e transizioni.

**Parent:** [TICKET-SVG-RENDER-PLAN.md](TICKET-SVG-RENDER-PLAN.md)
**Related:** [TICKET-SVG-DOCUMENTARY-VIDEO-PARITY.md](TICKET-SVG-DOCUMENTARY-VIDEO-PARITY.md), [TICKET-MOTION-COMPOSITING-PRESETS.md](TICKET-MOTION-COMPOSITING-PRESETS.md), [TICKET-SVG-GROUP-ISOLATION.md](TICKET-SVG-GROUP-ISOLATION.md), [TICKET-SVG-TEXT-ADVANCED-SCOPES.md](TICKET-SVG-TEXT-ADVANCED-SCOPES.md)

Lo stato della parità descritto qui sotto deriva da questo checkout 2026-10-29:
loader SVG, C ABI suite, raw gates sotto `scripts/ci/` e roadmap Stage 0–5 nel
parent ticket. Aggiorna direttamente qui se le evidenze cambiano.

## Stato

IN PROGRESS — la lane software supporta già il nucleo motion-graphics (geometry,
paint, gruppi isolati, filtri root, morph `d`, operatori target-scoped, testo
bounded, gate MP4 encoded). Mancano parità per: filtri element-scoped (glow per
elemento), SMIL esteso (repeat additive/`keySplines`/`animateMotion`), marker e
pattern su geometria, mask paint ricco, blend-mode per elemento, testo RTL raster
(failing gate), GPU parity e clean-SHA CI. Non si dichiara parità completa del
SVG 1.1/browser.

## Matrice di parità primitive (stato 2026-10-29)

Codici stato: **OK** = certificato con evidenza raw/raw gate nel checkout; **BOUNDED**
= funziona entro un contratto documentato, casi più ampi fail-closed; **FAIL-CLOSED**
= rifiutato esplicitamente dal loader/compiler; **OPEN** = non implementato o gate
failing.

### A. Geometria

| Primitiva SVG | Stato | Evidenza / note | Priorità |
| --- | --- | --- | --- |
| `path` (tutti i comandi, archi) | OK | Loader 48/48 casi; fixture geometry 1e-4 | — |
| `rect`/`circle`/`ellipse`/`line`/`polyline`/`polygon` | OK | Contratto importer gate | — |
| `g` annidati con trasformazioni statiche | OK | Motion gate + ablation | — |
| `use` locale su `<defs>` | OK | Bounded depth/instance; loader passa | — |
| `<defs>` gradients (`url(#id)`) | OK | objectBoundingBox + userSpaceOnUse, pad/repeat/reflect | — |
| clip/mask singola per elemento o scope root | BOUNDED | root clip-path/mask, uno per scope; intersezione scopes | — |
| mask con paint ricco / luminance mask | FAIL-CLOSED | attuale: solo geometria bianca opaca | P1 |
| `marker` (punte di freccia) | FAIL-CLOSED | non semantica di marker; workaround: geometry duplicata | P1 |
| `pattern` su geometrie (element paint) | OPEN | pattern verificato solo per text paint (tile 1-256px) | P1 |
| `<image>` dentro SVG | FAIL-CLOSED | footage via RenderPlan layer, non via SVG | P2 |
| `<svg>`/`<symbol>` annidati, `switch`, `foreignObject` | FAIL-CLOSED | fuori contratto bounded | P2 |

### B. Paint e stile

| Primitiva | Stato | Evidenza / note | Priorità |
| --- | --- | --- | --- |
| fill/stroke solidi multipli + opacity per elemento | OK | sibling-path nodes, ordine di pittura | — |
| `fill-rule`, caps, joins, miter, dash | OK | stroke gate 2674/119 px | — |
| CSS bounded: presentation attrs, `<style>`, classi, `currentColor` | OK | Phase 3 | — |
| gradienti lineari/radiali con `gradientTransform` statico | OK | integration; radial richiede bounds quadrati | — |
| ereditarietà tra gradienti (`href`/stops) | FAIL-CLOSED | gap attuale, typico in asset reali | P1 |
| `mix-blend-mode` / blend per elemento o gruppo | OPEN | compositor supporta blend math ma nessun lowering | P1 |
| filter attribute su elemento/gruppo | FAIL-CLOSED | solo root filter (vedi §D) | **P0** |

### C. Composizione gruppo

| Primitiva | Stato | Evidenza / note | Priorità |
| --- | --- | --- | --- |
| opacità gruppo isolata (ADR-035) annidata fino a depth 16 | OK | isolation gate; overlap oracle pass | — |
| clip/mask distinct scopes intersecanti | OK | nested clip gate | — |
| statico + animato translate commutabili | OK | con fail-closed su non-commuting | — |
| static+animated non-commutanti (es. scale statica + translate animata) | FAIL-CLOSED | tipico di asset YouTube reali; widened scope va pianificato | P1 |

### D. Filtri e glow

| Primitiva | Stato | Evidenza / note | Priorità |
| --- | --- | --- | --- |
| filtri root: chain lineare ≤8 di `feGaussianBlur` (isotropo), `feDropShadow`, `feColorMatrix` (matrix), finale `feBlend` (BackgroundImage) | BOUNDED | `svg_filter_parser.cpp` 159 righe, fail-closed oltre | — |
| **glow per elemento singolo (non su tutto il root)** | OPEN | oggi glow nativo esiste come effetto (`EffectKind::Glow`, fullframe CPU/Vulkan) ma non è raggiungibile dal filtri per-elemento | **P0** |
| `feOffset`, `feFlood`, `feMerge`, `feComponentTransfer`, `feTurbulence`, `feDisplacementMap` | FAIL-CLOSED | non nel contratto | P1 |
| filtri con element-scoped region/units, non-isotropic blur, color-space | FAIL-CLOSED | out of scope attuale | P1 |
| cleanup glow su layer animato | BOUNDED | SVG-04 gate: 602 halo pixel, 0 residue al punto precedente, repeat deterministico (marker sintetico; scena reference ancora aperta) | **P0** |

### E. Animazione (SMIL + element_animations + operatori)

| Primitiva | Stato | Evidenza / note | Priorità |
| --- | --- | --- | --- |
| `animateTransform` translate / uniform scale / rotate (pivot fisso, da origin o centro esplicito) | OK | loader + C ABI, motion gate con ablation | — |
| `animate` opacity, fill/stroke color+opacity, stroke-width, stop color/opacity | OK | bounded from/to / 2 values, begin=0, 1 iterazione, freeze, linear/discrete | — |
| **morph `d` (2–256 valori, topologia identica, keyTimes crescenti)** | OK | path morph gate: endpoint diversi; fail-closed su cambio topologia | **P0** |
| animazione group translate/scale/rotation/opacity annidata | OK | motion gate + ablations | — |
| `element_animations` per ID stable + path operators target-scoped (trim, repeater animated, offset, wiggle, round corners, zigzag, pucker/bloat, twist, morph, merge) | OK | encoded MP4 certification, 61-frame raw samples deterministici | — |
| `repeatCount>1` / `indefinite`, `values` lungo, begin offset, additive/suffix | FAIL-CLOSED | bounding SMIL: senza repeat, le pipeline YouTube a ciclo continuo fallback su RenderPlan tracks | P1 |
| `keySplines` / easing spline su SMIL | FAIL-CLOSED | easing via `AnimationTrackPlan` keyframes (RenderPlan), non via SMIL | P1 |
| `animateMotion` / `mpath` (movimento lungo path) | FAIL-CLOSED | follow-path solo con plan custom (marker documentary): serve parità generale | **P0** |
| `<style>@keyframes` / CSS animation | FAIL-CLOSED | motion chronon passa sempre per track `AnimationTrackPlan` | P2 |

### F. Testo

| Primitiva | Stato | Evidenza / note | Priorità |
| --- | --- | --- | --- |
| layout runs, `x/y lists`, `dx/dy`, `text-anchor`, styled chunks, baselines, spacing, `textLength` (2 modes, run diretto), `rotate` per-cluster | OK | text raw gates pass; fail-closed su combinazioni limitate | — |
| text fill/stroke, dash, gradient, pattern, marcatura a grafeme | OK | text stroke/gradient/pattern/chunk gates pass | — |
| `textPath` bounded | OK | coordinates gate | — |
| **Arabic RTL raster placement** | FAIL-CLOSED | gate chunk attuale fail (52 px alpha divergenza): bidi lowering/dispatch open; P0 per canali arabi, P1 altrimenti | P1 |
| `textLength` annidato + `rotate` annidato + textPath lengthAdjust | FAIL-CLOSED | delegato a [TICKET-SVG-TEXT-ADVANCED-SCOPES.md](TICKET-SVG-TEXT-ADVANCED-SCOPES.md) | P1 |
| vertical writing mode | FAIL-CLOSED | out of scope | P2 |
| pattern inheritance / paint esclusivo | FAIL-CLOSED | tile userSpaceOnUse, contenuti solidi | P1 |

### G. Deliverable video / runtime

| Primitiva | Stato | Evidenza / note | Priorità |
| --- | --- | --- | --- |
| MP4 H.264 1080p30 deterministico via `--encoder-backend pipe` | OK | showcase 360/360 raw hash identici; MP4 bit-identico su repeat | — |
| native encoder direct | BOUNDED | mismatch decodificati su alcuni build; usare pipe finché non certificato | P1 |
| **render performance SW (≈0.19 fps a 1080p su host condiviso: 360 frame in ~31 min)** | OPEN | per video YouTube richiede lookahead GPU/parallelizzazione; record baseline qui e target in [TICKET-100X-PERF-WAVES.md](TICKET-100X-PERF-WAVES.md) | **P0** |
| GPU parity (Vulkan) su tutte le primitive sopra[^1] | OPEN | stroke-only Vulkan defect ha ticket dedicated; CPU/GPU diff da misurare a 1080p | **P0** |
| clean-SHA CI same-source verde | OPEN | requisito di chiusura del parent | **P0** |
| determinism random-access (frame N riprodotto = frame N in sequenza) | BOUNDED | showcase ripetuto; FX-04 gate non certificato generalmente | P1 |
| composizione vector + footage nello stesso timeline | BOUNDED | fixture plan-level; ingest encoded con plate reale open | P1 |

[^1]: La lane SVG usa già il compilatore e l'esecutore Vulkan esistenti; la parità
si certifica su hardware disponibile e non si chiude localmente.

## Scope di un «video YouTube completo con SVG» (definizione di parità)

Un video si dichiara producibile **senza workaround** quando un singolo asset SVG
authoritative + un RenderPlan V3 può: (1) fare reveal con trim/morph, (2)
applicare glow/blur/shadow a singolo elemento e non solo al root, (3) muovere
un marker lungo una path con `animateMotion` o equivalente in-plan, (4) animare
testo multi-chunk con paint per-range, (5) ripetere in loop (repeatCount) senza
duplicare frame manualmente, (6) comporre SVG + footage nello stesso timeline,
(7) mantenere determinismo pixel su repeat e random access finché il render
completa entro un budget accettabile documentato. Ciascuno dei punti (1)–(7)
deve avere un raw gate dedicato.

## Acceptance gates (da aggiungere al ripopolamento)

- [ ] **SVG-EL-01 elemento glow** — gate raw: un solo elemento con filter/glow, halo limitato al bbox dilatato di quel elemento, zero cambio pixel sul resto del frame, repeat deterministico al 100%.
- [ ] **SVG-EL-02 element filter chain** — chain ≤8 applicata a un singolo elemento con `filter="url(#f)"`; fuori dal bbox del target zero pixel cambiano.
- [ ] **SVG-MO-01 animateMotion** — marker centroid segue la path con errore ≤1 px su f0/15/30/45/60 (pattern `svg_peak_pixel_gate.py` come base).
- [ ] **SVG-MO-02 loop repeat** — `repeatCount=2` (SMIL) o loop RenderPlan track produce f0 == f(period end) entro 1 px.
- [ ] **SVG-PA-01 rich mask** — mask con gradient/stroke al minimum per luminance; diff documentato contro expected alpha.
- [ ] **SVG-TX-01 RTL raster** — gate chunk attuale riportato a divergenza alpha ≤2 px e output deterministico.
- [ ] **SVG-GPU-01 CPU/GPU parity** — 10 frame 1080p con diff RMS documentato per backend, dove capability presente.
- [ ] **SVG-PERF-01 budget** — misurare wall/CPU su scena showcase 1080p e registrare target «video 60 s ≤ 6 h wall» o simile (decidere l'owner perf).
- [ ] **CI clean-SHA** — full SVG suite verde su checkout pulito remote (stesso SHA di run locale), evidence in `docs/evidence/`.

## Regola di chiusura (vale per questa scheda)

1. Separa **implementazione locale**, **test locale**, **parità GPU**, **CI same-SHA** e
   **decisioni di scope**; una categoria non sostituisce l'altra.
2. Registra per ogni prova: comando completo, SHA (clean o dirty), preset build,
   host/device, exit status, contatori e percorso evidenza sotto `docs/evidence/`.
3. Gate esterni (Vulkan host, footage reale, CI) non si chiudono localmente: lo Stato
   resta `PARTIAL`/`WAITING EXTERNAL` con prerequisito e owner espliciti.
4. Non si rimuove nessuna varior fail-closed senza gate di equivalenza; non si
   implementano primitive P1/P2 senza decisione di scope del parent ticket.5. Chiusura effettiva = tutti i gate **P0** sotto; aggiorna questa sezione `## Stato`, gli indici `docs/CURRENT_STATUS.md` / `docs/FOLLOWUP_TICKETS.md`, e `TODO-RIPRESA-100X-E-TICKET-APER-TI.md`, poi archivia come convenzione repo.

## Anti-obiettivi

Nessun editor SVG grafico, nessuna parità SMIL/CSS da browser, nessun SVG 1.1
completo perché sì; ogni estensione deve restare bounded e fail-closed, come
imposto dal contratto del parent ticket.
