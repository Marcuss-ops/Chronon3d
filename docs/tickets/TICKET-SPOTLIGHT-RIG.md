# TICKET-SPOTLIGHT-RIG — Spotlight con cone/falloff e light rig multipla

## Stato
PARTIAL (riconciliato 2026-11-05). Provenienza: gap analysis "AE + Premiere parity"
(MotionNations 30 video, in particolare #25 "Cinematic Lighting" interamente
dedicato al light rig). Stato registrato in `docs/stato/chronon3d.md` §13.

## TODO — cosa manca (agg. 2026-11-05)

- [x] **Tipi `SpotLight`/`PointLight` + contributo software** — tipi e valutazione cone/penumbra disponibili in `lighting_eval.hpp`; evidenza di closeout V1, D-08.
- [x] **Lighting loop multi-luce Vulkan strict-native** — loop shader con payload SSBO std430. Test GPU-only dedicato: quattro spot, confronto pixel-per-pixel con `evaluate_lighting_at`, 71/71 assertion PASS sulla RTX A4000 (2026-11-05). Resta da eseguire la parità MAE/p99 CAM-04 su scena/risoluzione di produzione.
- [x] **Luce multipla in RenderPlan** — array tipizzato, decoder fail-closed e ombre per-spot nel graph builder; evidenza di closeout V1, D-08.
- [x] **Controller comune** — implementato e certificato nel ticket Motion [`TICKET-LIGHT-RIG-V1`](../../../ChrononMotion3D/docs/tickets/archive/TICKET-LIGHT-RIG-V1.md): rotazione parent comune, assert geometriche e showcase 62/62.
- [ ] **Ombre per-spot** — solo dopo `TICKET-SHADOW-NATIVE-CATCHER.md`; se il P1 non è a terra alla fine, follow-up esplicito nel `## Stato` di chiusura.
- [x] **Limite Vulkan >64 sorgenti strict-native fail-closed** — regressione diretta `LightingNode`, 3/3 assertion PASS (2026-11-05).
- [ ] **Chiusura formale** — golden ≥4 spot con colori/cone indipendenti; `## Stato` → `COMPLETE`; spostamento in `docs/tickets/archive/`; `docs/stato/chronon3d.md` §13.

## Goal

Supportare light rig editoriali come nel tutorial AE: più spot colorati con
cone angle, penumbra/falloff e fascio focalizzato, governabili da un
controller comune (null rotabile), oltre alla key direzionale esistente.

## Scope (questa repo)

1. **Tipi luce**: `SpotLight` (position, direction, cone_angle, penumbra,
   falloff, color, intensity) e `PointLight` in `light_context.hpp`.
2. **Lane software**: contributo spot con attenuazione e coseno del cone.
3. **Lane Vulkan strict-native**: lighting loop multi-luce negli shader;
   binding dei parametri in stile `TextRunDynamic` (layout std430 verificato).
4. **RenderPlan**: luce multipla come array tipizzato (cap dichiarato, stile
   `kEffectTypeCount`), ordine deterministico; decoder fail-closed.
5. **Ombre per spot**: solo dopo `TICKET-SHADOW-NATIVE-CATCHER.md`.

## Fuori scope

- L'autoring del rig come asset di scena (preset, rotazione via null) è
  ChrononMotion3D: `TICKET-LIGHT-RIG-V1.md`.
- HDRI/IBL: `TICKET-ENVIRONMENT-LIGHT-IBL.md`.

## Piano di chiusura passo-passo (agg. 2026-10-28)

Precondizione trasversale: feature freeze attiva — nessuna feature entra in
produzione finché la CI non è verde sullo SHA esatto (`docs/stato/README.md`);
le certificazioni di parità richiedono l'host GPU NVIDIA.

1. **Accordo dell'interfaccia di emissione** — fissare col formato di bake
   atteso da `TICKET-LIGHT-RIG-V1.md` (ChrononMotion3D): cap sorgenti, ordine
   deterministico, semantica di parenting. Basta un contratto scritto prima
   del codice.
2. **Tipi luce** — `SpotLight` (position, direction, cone_angle, penumbra,
   falloff, color, intensity) e `PointLight` in `light_context.hpp`.
   Verifica: compilazione + test di costruzione payload.
3. **Lane software** — contributo spot con attenuazione e coseno del cone.
   Verifica: valori numerici attesi su casi noti (punto dentro/fuori cone,
   penumbra ai bordi).
4. **RenderPlan** — luce multipla come array tipizzato con cap dichiarato
   (stile `kEffectTypeCount`), ordine deterministico, decoder fail-closed.
   Verifica: payload oltre il cap respinto; round-trip deterministico.
5. **Lane Vulkan strict-native** — lighting loop multi-luce negli shader con
   binding dei parametri in stile `TextRunDynamic` (layout std430 verificato).
   Verifica: parità MAE/p99 nella famiglia CAM-04 contro la lane software.
6. **Controller comune** — la rotazione del rig tramite transform parent
   aggiorna tutte le sorgenti coerentemente.
   Verifica: assert geometriche sul bake (tutte le direzioni ruotate della
   stessa quantità).
7. **Ombre per-spot** — solo dopo `TICKET-SHADOW-NATIVE-CATCHER.md`; se il P1
   non è ancora a terra alla fine di questo ticket, dichiararlo esplicitamente
   come follow-up nel `## Stato` di chiusura, non lasciarlo implicito.
8. **Chiusura formale** — scena golden con ≥4 spot a colori/cone indipendenti;
   spuntare `## Accettazione`; `## Stato` → `COMPLETE`; spostare il file in
   `docs/tickets/archive/`; aggiornare `docs/stato/chronon3d.md` §13.

## Accettazione

- [x] ≥4 spot contemporanei con colori/cone indipendenti: fixture software 28/28 (D-08) e fixture Vulkan strict-native pixel-per-pixel 71/71 (2026-11-05).
- [ ] Parità software/Vulkan (MAE/p99 nella famiglia CAM-04).
- [x] Controller comune: rotazione del rig tramite transform parent aggiorna
      tutte le luci coerentemente (TICKET-LIGHT-RIG-V1, assert geometriche).
- [x] Payload oltre il cap respinto fail-closed (3/3 assertion, 2026-11-05).
- [ ] CTest/CI same-SHA verde.

## Dipendenze

- `TICKET-SHADOW-NATIVE-CATCHER.md` per le ombre per-spot (le prime fasi
  possono procedere senza ombre).
- `TICKET-LIGHT-RIG-V1.md` per l'autoring.

## Ordine

Dopo `TICKET-SHADOW-NATIVE-CATCHER.md`, in parallelo a
`TICKET-ENVIRONMENT-LIGHT-IBL.md`.

## Riconciliazione evidenze 2026-11-05

Il closeout V1 D-08 in [`TICKET-CAMERA-3D-V1-CLOSURE`](archive/TICKET-CAMERA-3D-V1-CLOSURE.md)
conferma tipi spot/point, contributo software, array RenderPlan fail-closed,
ombre per-spot nel graph builder e test di quattro spot indipendenti (28/28
assertion). Sono chiusi i criteri software/contratto e la fixture funzionale.
Il loop Vulkan è ora implementato e verificato pixel-per-pixel con quattro
spot su fixture 4×4; il cap 64 fallisce in modo esplicito e il controller è
coperto dal ticket Motion. Restano la parità MAE/p99 CAM-04 a risoluzione di
produzione, CTest/CI same-SHA e ombre per-spot, che dipendono dalla chiusura
della qualità strict-native in `TICKET-SHADOW-NATIVE-CATCHER.md`. La suite
Vulkan aggregata del 2026-11-05 fallisce su errori `vkCreateInstance` dopo i
probe; i tre test spotlight mirati passano 91/91 assertion. Il ticket resta
`PARTIAL` e non viene archiviato.
