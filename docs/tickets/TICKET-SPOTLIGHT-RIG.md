# TICKET-SPOTLIGHT-RIG — Spotlight con cone/falloff e light rig multipla

## Stato
OPEN (2026-10-28). Provenienza: gap analysis "AE + Premiere parity"
(MotionNations 30 video, in particolare #25 "Cinematic Lighting" interamente
dedicato al light rig). Stato registrato in `docs/stato/chronon3d.md` §13.

## TODO — cosa manca (agg. 2026-10-28)

- [ ] Tipi `SpotLight`/`PointLight` in `light_context.hpp` + contributo software (attenuazione, coseno del cone).
- [ ] Lighting loop multi-luce in lane Vulkan strict-native con binding std430 verificato.
- [ ] Luce multipla in RenderPlan come array tipizzato con cap dichiarato, ordine deterministico, decoder fail-closed.
- [ ] Ombre per-spot: solo dopo `TICKET-SHADOW-NATIVE-CATCHER.md`.

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

- [ ] ≥4 spot contemporanei con colori/cone indipendenti su scena golden.
- [ ] Parità software/Vulkan (MAE/p99 nella famiglia CAM-04).
- [ ] Controller comune: rotazione del rig tramite transform parent aggiorna
      tutte le luce coerentemente.
- [ ] Payload oltre il cap respinto fail-closed; CTest verde.

## Dipendenze

- `TICKET-SHADOW-NATIVE-CATCHER.md` per le ombre per-spot (le prime fasi
  possono procedere senza ombre).
- `TICKET-LIGHT-RIG-V1.md` per l'autoring.

## Ordine

Dopo `TICKET-SHADOW-NATIVE-CATCHER.md`, in parallelo a
`TICKET-ENVIRONMENT-LIGHT-IBL.md`.
