# TICKET-INLINE-MESH-TEXTURED-ALPHA — Soggetto animato textured/alpha

## Stato
PARTIAL (2026-10-28). Provenienza: blocco già noto in `docs/stato/chronon-motion3d.md`
§3 ("Soggetto animato textured/alpha... bloccato su un cambio coordinato del
contratto/runtime Chronon3D") e prerequisito per il glTF animato della parità
AE. Stato registrato in `docs/stato/chronon3d.md` §13.

## TODO — cosa manca (agg. 2026-10-28)

- [x] Accordare il contratto geometria/skin per frame con `TICKET-GLTF-ANIMATED-V1` (ChrononMotion3D): topologia indicizzata fissa, posizioni e normali opzionali per frame, UV/material/texture/alpha statici.
- [ ] Disegno inline mesh textured/alpha nella lane `vulkan_native` (la lane software funziona).
- [x] Materializer bridge V3 per posizioni e normali animate con budget payload fail-closed.
- [x] Estendere il test ABI del bridge al caso mesh animata con texture asset e normali.
- [x] Pixel proof sulla rotta CLI canonica.
- [x] Lane software: applicare il `alpha_cutoff` del materiale ai texel dopo
      il campionamento prospettico della texture; i texel sotto soglia non
      scrivono colore né depth.

## Goal

Rendere renderizzabili i soggetti 3D animati con texture e alpha: mesh inline
testurizzata (semantica vertex/UV/alpha) con contratto geometria/skin per
frame, e supporto nativo inline-mesh nella lane `vulkan_native` (oggi la lane
vulkan non disegna inline mesh: solo la lane software funziona).

## Scope (questa repo)

1. **Contratto geometria/skin per frame**: variazione di topologia/UV/alpha
   tra frame, coerente con `inline_mesh.animation` dello schema V3 (1..4096
   campioni, `frame` 0..1000000, `positions` 3D) già supportato per le
   posizioni bakeate.
2. **Lane `vulkan_native`**: disegno inline mesh con texture/UV/alpha nello
   strict path (oggi gap dichiarato).
3. **Materializer RenderPlan**: soggetto textured/alpha nel bridge V3, budget
   payload invariato (fuori budget → respinto fail-closed).
4. **Pixel proof della rotta CLI di default**: la verità pixel deve passare
   dalla rotta canonica, non dallo still-PNG non canonico.

## Fuori scope

- L'importazione glTF di skin/morph/animazioni (ChrononMotion3D,
  `TICKET-GLTF-ANIMATED-V1.md`): questo ticket riceve i frame già bakeati.
- Il materiale PBR completo (`TICKET-PBR-MATERIALS-V1.md`).

## Piano di chiusura passo-passo (agg. 2026-10-28)

Precondizione trasversale: feature freeze attiva — nessuna feature entra in
produzione finché la CI non è verde sullo SHA esatto (`docs/stato/README.md`);
le certificazioni strict-native richiedono l'host GPU NVIDIA.

1. **Contratto concordato** con ChrononMotion3D: topologia e indici sono fissi;
   le sample portano posizioni e, opzionalmente, normali per ogni vertice.
   UV, materiale, texture e alpha cutoff restano sul mesh base. Il budget
   comprende entrambe le array animate.
2. **Materializer bridge V3** — soggetto textured/alpha nel bridge senza
   superare il budget payload (oltre budget → respinto fail-closed).
   Verifica: test di respesione oltre budget.
3. **Disegno inline mesh textured/alpha in `vulkan_native`** — il gap
   principale (oggi solo la lane software disegna inline mesh): vertex/UV/
   alpha nello strict path.
   Verifica: parità software/Vulkan sul soggetto di test entro le metriche
   dichiarate.
4. **Estendere `chronon3d_motion_v3_bridge_tests`** al caso textured/alpha.
5. **Pixel proof sulla rotta CLI canonica** — la verità pixel deve passare
   dalla rotta CLI di default, non dallo still-PNG non canonico.
   Verifica: cattura pixel sulla rotta canonica.
6. **Determinismo** su due run con stesso seed; integrazione con ombre/shutter
   desiderabile (assorbire in chiusura se `TICKET-SHADOW-NATIVE-CATCHER.md` è
   già a terra, altrimenti follow-up esplicito nel `## Stato`).
7. **Chiusura formale** — CTest verde; spuntare `## Accettazione`; `## Stato` →
   `COMPLETE`; spostare il file in `docs/tickets/archive/`; aggiornare
   `docs/stato/chronon3d.md` §13 e rimuovere il blocco correlato da
   `docs/stato/chronon-motion3d.md` §3.

## Accettazione

- [ ] Soggetto animato con texture+alpha renderizzato in `vulkan_native` e
      software con parità entro le metriche dichiarate.
- [ ] Payload oltre budget respinto; determinismo su due run con stesso seed.
- [x] Test ABI del bridge copre mesh animata, asset texture, campioni normali,
      frame selection e compilazione V3.
- [ ] CTest verde.

## Dipendenze

- `TICKET-SHADOW-NATIVE-CATCHER.md` per l'integrazione con ombre/shutter
  (desiderabile, non bloccante).
- Cambio coordinato col contratto ChrononMotion3D: accordare l'interfaccia con
  `TICKET-GLTF-ANIMATED-V1.md` prima di implementare.

## Ordine

Dopo `TICKET-SHADOW-NATIVE-CATCHER.md`, in parallelo agli effetti P2.

## Evidenza locale (2026-10-28)

- `chronon3d_renderer_core_tests` verifica che una mesh textured conserva il
  blending alpha e scarta il pixel quando il texel alpha è sotto il cutoff;
  CTest completo del target renderer core PASS (8.21 s) nel run finale.
- La compilazione `chronon3d_renderer_core_tests -j12` PASS dopo il cambio.
- `cmake --build build/chronon/linux-video-fast-dev --target
  chronon3d_renderer_core_tests chronon3d_c_abi_tests -j12` — PASS dopo
  l'estensione V3 a campioni normali opzionali.
- `ctest ... -R '^chronon3d_renderer_core_tests$'` — PASS (1/1, 8.39 s).
- `chronon3d_c_abi_tests --test-case='inline mesh animation selects adjacent samples and preserves texture asset' --no-skip` — PASS (1/1, 20 assertions); verifica decode, compiler e normale base interpolata.
- Restano il disegno Vulkan strict-native (implementazione locale) e le relative
  certificazioni esterne GPU/CI.
- 2026-10-29: un RenderPlan V3 con mesh animata, posizioni e normali per frame
  è stato eseguito con `chronon3d_cli render --plan ... --backend software`.
  Il PNG ha mean `0.0220`, alpha mean `0.0410` e due colori. La correzione
  applica la camera RenderPlan risolta e rimuove l'offset canvas-centered prima
  della proiezione prospettica.
