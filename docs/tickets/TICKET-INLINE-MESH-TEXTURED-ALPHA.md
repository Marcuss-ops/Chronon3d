# TICKET-INLINE-MESH-TEXTURED-ALPHA — Soggetto animato textured/alpha

## Stato
OPEN (2026-10-28). Provenienza: blocco già noto in `docs/stato/chronon-motion3d.md`
§3 ("Soggetto animato textured/alpha... bloccato su un cambio coordinato del
contratto/runtime Chronon3D") e prerequisito per il glTF animato della parità
AE. Stato registrato in `docs/stato/chronon3d.md` §13.

## TODO — cosa manca (agg. 2026-10-28)

- [ ] Accordare il contratto geometria/skin per frame con `TICKET-GLTF-ANIMATED-V1` (ChrononMotion3D) prima di implementare.
- [ ] Disegno inline mesh textured/alpha nella lane `vulkan_native` (oggi solo la lane software funziona).
- [ ] Estendere il materializer bridge V3 al caso textured/alpha senza superare il budget payload.
- [ ] Pixel proof sulla rotta CLI canonica; estendere `chronon3d_motion_v3_bridge_tests` al caso textured/alpha.

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

1. **Fase 0 obbligatoria: accordo del contratto** geometria/skin per frame con
   `TICKET-GLTF-ANIMATED-V1.md` (ChrononMotion3D): quali attributi variano per
   frame (topologia/UV/alpha) e budget payload. È un cambio coordinato tra le
   due repo: nessun codice prima di questo accordo.
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
- [ ] `chronon3d_motion_v3_bridge_tests` esteso al caso textured/alpha.
- [ ] CTest verde.

## Dipendenze

- `TICKET-SHADOW-NATIVE-CATCHER.md` per l'integrazione con ombre/shutter
  (desiderabile, non bloccante).
- Cambio coordinato col contratto ChrononMotion3D: accordare l'interfaccia con
  `TICKET-GLTF-ANIMATED-V1.md` prima di implementare.

## Ordine

Dopo `TICKET-SHADOW-NATIVE-CATCHER.md`, in parallelo agli effetti P2.
