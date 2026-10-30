# TICKET-SHADOW-NATIVE-CATCHER — Ombre native, shadow catcher, shutter

## Stato
OPEN (2026-10-28). Provenienza: gap analysis "AE + Premiere parity"
(MotionNations: 9 video "advanced 3D" usano ground shadow/shadow catcher) e
dai difetti già noti in `docs/stato/chronon-template.md` §2. Stato
registrato in `docs/stato/chronon3d.md` §13.

## TODO — cosa manca (agg. 2026-10-28)

> Logica e punti di intervento completi nel «Piano di chiusura passo-passo» qui sotto; sintesi con dove mettere le mani:

- [ ] **Pass ombre nativo in lane Vulkan strict-native** — il nodo `Shadow` oggi fallisce fail-closed. Dove: shadow map + nodo `Shadow` nella lane Vulkan (`src/backends/vulkan/`), decoder del piano.
- [ ] **Shadow catcher catch-only** con semantica alpha corretta sui bordi. Dove: materiali/shape renderer (ground solid catch-only).
- [x] **Accumulatore temporale weighted-add nativo** (2026-10-29): implementato in `VulkanBackend::accumulate_surface_weighted` (`src/backends/vulkan/vulkan_backend_surface_api_ops_private.cpp:70-94`) e cablato in `CompositionFrameState::render_temporal_accumulation` (`src/render_graph/pipeline/temporal/composition_frame_temporal.cpp:249-421`), certificato `150/150 PASS` raw RGBA8 (`max p99 = 2.0`, `mean MAE = 0.110`) in `docs/evidence/camera-authority-migration/T16/shutter-parity-20261022/t16-shutter-full150-raw-metrics-20261027.json`.
- [x] **Residuo fail-open DOF raw verificato** (2026-10-29): il repro originale al frame 75 produce pixel opachi, differisce dal controllo DOF-off e si ripete byte-identico; dettagli nel checkpoint sotto. Nessun nuovo fix attribuito a questa sessione.
- [ ] **A/B software vs Vulkan su produzione approvata** + repeatability strict (serve host GPU NVIDIA).

## Goal

Chiudere la famiglia ombre/luci T05 (oggi PARTIAL: il nodo `Shadow` fallisce
fail-closed in strict Vulkan) e rendere usabile la pipeline cinematografica
che i tutorial AE replicano con "ground shadow + solid catcher".

## Scope (questa repo)

1. **Pass ombre nativo**: shadow map risolta interamente in lane Vulkan
   strict-native; il nodo `Shadow` non deve più fallire fail-closed.
2. **Shadow catcher**: materiale catch-only (il ground solid riceve e mostra
   solo le ombre), semantica alpha corretta sui bordi.
3. **Accumulatore temporale weighted-add nativo**: precondizione nota per lo
   shutter full-frame (oggi l'operatore Vulkan è solo source-over; la lane
   shutter si ferma chiedendo questo accumulatore — vedi
   `docs/stato/chronon-template.md` §2).
4. **Fix fail-open DOF**: la rotta raw con `camera.dof.enabled` oggi esce 0
   restituendo frame RGBA tutto zero; deve segnalare fail-closed l'operazione
   non supportata (o disegnare il frame).
5. **A/B software vs Vulkan** su produzione approvata + repeatability strict.

## Fuori scope

- Il light rig autorevole e i preset ombra editoriali (ChrononMotion3D,
  `TICKET-LIGHT-RIG-V1.md`).
- Risoluzione del crash TBB già contenuto (guardia host): resta contenuto.

## Piano di chiusura passo-passo (agg. 2026-10-28)

Precondizione trasversale: feature freeze attiva — nessuna feature entra in
produzione finché la CI non è verde sullo SHA esatto (`docs/stato/README.md`);
le certificazioni strict-native richiedono l'host GPU NVIDIA.

1. **Riprodurre i due difetti come test di regressione oggi rossi** — nodo
   `Shadow` che fallisce fail-closed in strict-native e rotta raw con
   `camera.dof.enabled` che esce 0 con frame RGBA a zero. Senza questa base la
   chiusura non è dimostrabile.
   Verifica: i test falliscono per il motivo giusto (fallimento dichiarato,
   non crash).
2. **Accumulatore temporale weighted-add nativo** — operatore di accumulo
   accanto al solo source-over nella lane Vulkan; è la precondizione nota dello
   shutter (oggi la lane shutter si ferma qui,
   `docs/stato/chronon-template.md` §2).
   Verifica: accumulo deterministico su frame di prova + test unitari.
3. **Pass ombre nativo in lane Vulkan strict-native** — shadow map risolta
   interamente in lane nativa; il nodo `Shadow` decodifica e renderizza senza
   fallback.
   Verifica: i test del passo 1 diventano verdi; parità A/B software con le
   metriche RGB-L1 delle run T05 esistenti.
4. **Shadow catcher catch-only** — il ground solid riceve e mostra solo le
   ombre, alpha corretto sui bordi, nessun residuo di staging.
   Verifica: frame di certificazione 1920×1080.
5. **Shutter 90°/8 campioni** in strict-native sull'accumulatore del passo 2.
   Verifica: NVENC accetta i frame (oggi zero).
6. **Fix fail-open DOF** — scegliere e dichiarare una delle due semantiche
   (errore esplicito fail-closed oppure frame disegnato) e testarla.
   Verifica: DoF-on differisce da DoF-off oppure errore esplicito; mai
   successo con frame trasparente.
7. **Certificazione A/B software vs Vulkan su produzione approvata** +
   repeatability strict su due run (serve l'host GPU NVIDIA).
8. **Chiusura formale** — CTest verde e golden registrata; spuntare tutte le
   voci di `## Accettazione`; portare `## Stato` a `COMPLETE` (convenzione di
   `docs/tickets/archive/TICKET-PER-KEYFRAME-EASING.md`); spostare il file in
   `docs/tickets/archive/`; registrare lo stato risolto in
   `docs/stato/chronon3d.md` §13 e aggiornare T05.

## Accettazione

- [ ] Nodo `Shadow` verde in strict-native con parità A/B software (metriche
      RGB-L1 come da run T05 esistenti, oltre soglia approvata).
- [ ] Shadow catcher 1920×1080: ombre visibili, nessun residuo di staging,
      alpha corretto.
- [x] Shutter 90°/8 campioni completa in strict-native con accumulatore
      weighted-add (`t16-shutter-full150-raw-metrics-20261027.json`: 150/150 PASS raw RGBA8).
- [x] DoF-on differisce da DoF-off oppure errore esplicito; mai successo con
      frame trasparente (repro raw T16/frame 75 verificato il 2026-10-29).
- [ ] CTest verde; golden registrata.

## Residuo DOF raw — verifica 2026-10-29

Sul Release corrente, più recente dei sorgenti DOF/output, rieseguito il comando
originale in `docs/evidence/camera-authority-migration/T16/dof-ablation/README.md`:
Vulkan, sink raw RGBA, frame 75, piano `t16_three_depth_dof_on.plan.json`,
1920×1080, senza imporre strict-native. Controllo ottenuto disabilitando DOF
nella camera e in tutti i camera sample, lasciando invariato il resto.

- Tre processi (on/off/on ripetuto): exit 0 e 8,294,400 byte ciascuno.
- Alpha minimo/massimo 255/255 in tutti e tre i frame; nessun successo trasparente.
- Media RGB on 68.326114/255, off 66.755947/255; 862,691 pixel RGB diversi.
- On e ripetizione byte-identici, SHA-256 `d56dcdf39c37434a402c666c24161b6ebb6c8765d260e9faf9448d20e7495279`.
- Off SHA-256 `5fb217d2080d92e2c2591f4fd5f934f66f9a6856cf5282f80947c60ae00a4379`.
- Risultati e log locali: `.tmp/dof-closeout-results.json`, `.tmp/dof-closeout.log`,
  `.tmp/dof-closeout-{on,off,repeat}.rgba.log`; artefatti gitignored.
- Test graph mirati DOF/capability/lifetime/dispatch: 9 casi, 362 assertion PASS
  (`.tmp/ticket-graph-focused.log`), binario ricostruito con `-j16`.

La nota storica sul raw tutto zero è superata per questo repro. Nel codice
corrente sono già presenti la preservazione dell'output nativo terminale DOF
e il readback quando manca il backing CPU; non è stato introdotto un fix
speculativo né dimostrata qui la causalità del vecchio raw zero. Si chiude
solo questo criterio verificabile, non il ticket: catch-only, shutter weighted-add,
qualità ombre e certificazione completa restano aperti. Nessuna review visiva
eseguita in questa verifica e nessuna CI same-SHA.

## Dipendenze

- Host GPU NVIDIA dedicato per la certificazione (blocco trasversale noto:
  `docs/stato/README.md`).
- Nessuna dipendenza da altre repo.

## Ordine

**Primo di tutto** il P1: è prerequisito di spotlight, IBL e del rendering
dei soggetti animati.
