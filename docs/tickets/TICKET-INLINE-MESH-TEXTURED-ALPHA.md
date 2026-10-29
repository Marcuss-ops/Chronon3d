# TICKET-INLINE-MESH-TEXTURED-ALPHA — Soggetto animato textured/alpha

<!-- protocollo-chiusura-completa:2026-10-29:v1 -->

## Protocollo di chiusura completa (2026-10-29)

> Autorità dedicata: sezione **27 — manca lane Vulkan native** di `../../../PIANO-CHIUSURA-TICKET-APER-TI-2026-10-29.md` (fase consigliata **F2** · priorità **P1**).
> Questo blocco rende la scheda autosufficiente: riporta qui i criteri dedicati alla chiusura e le regole valide per dichiararla `COMPLETE`.
> Fotografia dell'audit 2026-10-29: prima di eseguire un criterio, verificane l'attualità contro il codice, i test e le evidenze già presenti in questa scheda.

### Criteri dedicati da portare a verde

- [x] Implementare disegno della mesh inline textured+alpha in `vulkan_native` consumando il contratto di geometria/skin concordato con ChrononMotion3D; mantenere il software path come oracle.
- [x] Fare render CLI visibile del soggetto animato con texture/alpha e verificare blending, animazione e frame output contro golden software (MAE/p99 entro il gate CAM-04).
- [x] Aggiungere rifiuto fail-closed per payload oltre budget e test determinismo due run stesso seed.
- [x] Ricostruire target e CTest rilevanti; registrare parità/device/SHA, non solo compilazione. La suite registry passa; la certificazione CI dello SHA pushato resta esterna.

### Regola di chiusura (valida per questa e ogni scheda)

1. Separa sempre **implementazione locale**, **test locale**, **parità GPU o asset/riferimenti esterni**, **CI same-SHA** e **decisioni di prodotto**; una categoria non sostituisce l'altra.
2. Registra per ogni prova: comando completo, SHA (clean o dirty), preset build, host/device, exit status, contatori (test/assert/frame/hash/digest) e percorso dell'evidenza sotto `docs/evidence/`.
3. I gate esterni (runner, content pack, footage, host GPU, decisione di owner) non si chiudono localmente: lo Stato resta `PARTIAL`/`BLOCKED` o `WAITING EXTERNAL` con prerequisito e owner espliciti. Non trasformare mai `NOT RUN`/`FAIL` in `PASS`.
4. Non rimuovere fallback, API o capability fail-closed prima del relativo gate di equivalenza/census; non implementare feature proposte senza la decisione di scope.
5. Chiusura effettiva = tutti i criteri dedicati verdi (oppure formalmente ritirati con motivazione e owner) + aggiornamento di `## Stato` in questa scheda, degli indici `docs/CURRENT_STATUS.md`/`docs/FOLLOWUP_TICKETS.md` se la scheda è indicizzata, di `TODO-RIPRESA-100X-E-TICKET-APER-TI.md`, e archiviazione della scheda secondo le convenzioni del repository.

### Percorso rapido (ordine di esecuzione consigliato)

1. Riconfronta i criteri dedicati qui sopra con lo stato attuale della scheda: le voci già soddisfatte non si rieseguono, si ricollegano all'evidenza esistente.
2. Frammenta l'esecuzione in micro-step: chiudi prima le azioni locali raggiungibili nel checkout corrente, poi le decisioni di contratto/scope aperte, infine prepara e richiedi i gate esterni del piano.
3. Dopo ogni criterio, spontalo in questa sezione con il link all'evidenza; annota nella stessa sezione `NOT RUN`/`BLOCKED` dove la prova non è disponibile, senza cancellare il criterio.
4. Quando tutti i criteri dedicati sono verdi (o ritirati in modo formale), applica la Regola di chiusura (punto 5), aggiorna gli indici canonicali e archivia la scheda.

<!-- /protocollo-chiusura-completa:2026-10-29:v1 -->

## Stato
PARTIAL (updated 2026-10-29). Provenienza: blocco già noto in `docs/stato/chronon-motion3d.md`
§3 ("Soggetto animato textured/alpha... bloccato su un cambio coordinato del
contratto/runtime Chronon3D") e prerequisito per il glTF animato della parità
AE. Stato registrato in `docs/stato/chronon3d.md` §13.

## TODO — cosa manca (agg. 2026-10-28)

- [x] Accordare il contratto geometria/skin per frame con `TICKET-GLTF-ANIMATED-V1` (ChrononMotion3D): topologia indicizzata fissa, posizioni e normali opzionali per frame, UV/material/texture/alpha statici.
- [x] Disegno inline mesh textured/alpha nella lane `vulkan_native` (la lane software funziona).
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

- [x] Soggetto animato con texture+alpha renderizzato in `vulkan_native` e
      software con parità entro le metriche CAM-04: foreground MAE 1.492,
      p99 8, max 17 sul confronto H.264 decodificato.
- [x] Payload oltre budget respinto; determinismo su due run con stesso seed.
- [x] Test ABI del bridge copre mesh animata, asset texture, campioni normali,
      frame selection e compilazione V3.
- [x] CTest verde per il target registry Vulkan (119 casi, 1.914.121 asserzioni); test budget C ABI mirato 1/1, 8 asserzioni.

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
- 2026-10-29 follow-up glTF Motion3D: un piano V3 producer con 31 sample
  distinti, texture RGBA e alpha cutoff supera `validate` e produce MP4 software
  via NVENC (31/31 frame decodificabili), ma tutti i framemd5 risultano uguali;
  il frame mostra metà nero/metà verde scuro e non il soggetto. Il pixel proof
  statico già registrato non certifica quindi l'animazione glTF. Evidenza:
  [`ticket-roundtrip-20261029`](../../../ChrononMotion3D/docs/evidence/ticket-roundtrip-20261029/README.md).
- 2026-10-29 diagnostic raw follow-up: la fixture è stata centrata e scalata;
  il software raw sink mostra texture/alpha e 31 posizioni animate con hash
  distinti. L'NVENC MP4 resta black/green. Questi render provengono però da un
  CLI Release buildato al commit `67fd33f7f`, non dal source HEAD `014eeb4df`, e
  non aggiornano il pixel-proof canonico finché non si ripetono sul binario
  current-source. Evidenza e frame campione:
  [`ticket-roundtrip-scaled-20261029`](../../../ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/README.md).
- Current-source rerun 2026-10-29: `linux-video-release` software raw sink
  mostra il soggetto textured/alpha su 31 frame distinti; il current-source
  video export H.264/NVENC inizialmente risultava corrotto sia col program
  route sia col legacy route. Il rendering Vulkan native, il confronto
  software/Vulkan e il determinismo GPU restano non verificati. Dettagli:
  [`ticket-roundtrip-current-source-20261029`](../../../ChrononMotion3D/docs/evidence/ticket-roundtrip-current-source-20261029/README.md).
- Follow-up NVENC 2026-10-29: corretto il layout host YUV420P/NV12 mismatch;
  default e canonical composition route producono ora 31/31 frame H.264
  animati, con RMSE RGB 0.7673 contro l'oracle software compositato su nero.
  Rimane aperta la lane `vulkan_native`: questo risultato non la certifica.
  Evidenza:
  [`native-av-nv12-layout-20261029`](../evidence/native-av-nv12-layout-20261029/README.md).
- Probe Vulkan stretto 2026-10-29: la CLI accetta `--backend vulkan` e completa
  il frame sulla route `full_graph_native`, ma l'output RGBA 640×360 è tutto
  trasparente nero (0 pixel non-zero). Il dispatch Vulkan corrente non ha una
  route inline-mesh. I test software/C ABI precompilati passano per texture,
  alpha, budget e primitive deterministiche; non coprono il disegno Vulkan e
  non chiudono il ticket. Evidenza:
  [`inline-mesh-vulkan-probe-20261029`](../evidence/inline-mesh-vulkan-probe-20261029/README.md).
- Strict-native Vulkan follow-up 2026-10-29: implementata la rasterizzazione
  indicizzata textured/alpha con UV perspective-correct, alpha cutoff e
  source-over. La fixture animata produce 31/31 frame NVENC visibili e due
  render consecutivi hanno SHA-256 identico. Il test shader focused passa
  23/23 asserzioni; il test del budget aggregato passa 8/8; `chronon3d_backend_registry_tests`
  passa 119 casi e 1.914.121 asserzioni dopo aver aggiornato l'atteso numero
  di kernel da 24 a 25. Il primo confronto ha rilevato il lighting ambientale
  al 20% omesso dal kernel; la pipeline ora interpola il fattore RGB per
  vertice dal `LightContext` canonico. Il rerun software/Vulkan NVENC rientra
  nel gate CAM-04 (foreground MAE 1.492, p95 5, p99 8, max 17). Due output
  strict-native hanno SHA identico. Lo stato resta `PARTIAL` in attesa della CI
  sullo SHA pushato. Evidenza completa:
  [`inline-mesh-vulkan-20261029`](../evidence/inline-mesh-vulkan-20261029/README.md).
- Rerun dal commit pulito e pushato `0e7303577524d8b1c1819213de022390cca6775e`
  (2026-10-29): il CLI Release ha renderizzato di nuovo la fixture Motion3D in
  strict-native Vulkan/NVENC. Decodifica 31/31 frame 640×360, 31 hash di frame
  distinti; receipt `full_graph_native`, zero downgrade, zero CPU readback e
  zero staging-copy NVENC. SHA MP4 `9d3ad734bccdfbc09569088508a3e10fb3fb06152af637b13530c0ab10da717b`,
  identico alla prova precedente. Log, MP4 e timing receipt sono nello stesso
  pacchetto [`inline-mesh-vulkan-20261029`](../evidence/inline-mesh-vulkan-20261029/README.md).
  Il gate CI sullo SHA esatto resta senza esito; il ticket rimane `PARTIAL`.
