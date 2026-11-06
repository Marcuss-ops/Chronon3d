# TICKET-SHADOW-NATIVE-CATCHER — Ombre native, shadow catcher, shutter

## Stato
PARTIAL (riconciliato 2026-11-05). Provenienza: gap analysis "AE + Premiere parity"
(MotionNations: 9 video "advanced 3D" usano ground shadow/shadow catcher) e
dai difetti già noti in `docs/stato/chronon-template.md` §2. Stato
registrato in `docs/stato/chronon3d.md` §13.

## TODO — cosa manca (agg. 2026-11-05)

> Logica e punti di intervento completi nel «Piano di chiusura passo-passo» qui sotto; sintesi con dove mettere le mani:

- [x] **Instradamento del pass ombre nativo** — `ShadowNode::execute` instrada a `execute_native` quando `require_native_gpu` è attivo; regressione 15 assertion e backend registry 1/1 (closeout D-06, 2026-11-04). Questo prova il dispatch, non chiude qualità/parità su produzione.
- [x] **Shadow catcher catch-only** — `Material2_5D::shadow_catcher`, builder, RenderPlan/schema/fingerprint e receiver trasparente nel graph builder. Regressione renderer 320×240: 6/6 assertion PASS il 2026-11-05.
- [x] **Accumulatore temporale weighted-add nativo** (2026-10-29): implementato in `VulkanBackend::accumulate_surface_weighted` (`src/backends/vulkan/vulkan_backend_surface_api_ops_private.cpp:70-94`) e cablato in `CompositionFrameState::render_temporal_accumulation` (`src/render_graph/pipeline/temporal/composition_frame_temporal.cpp:249-421`), certificato `150/150 PASS` raw RGBA8 (`max p99 = 2.0`, `mean MAE = 0.110`) in `docs/evidence/camera-authority-migration/T16/shutter-parity-20261022/t16-shutter-full150-raw-metrics-20261027.json`.
- [x] **Residuo fail-open DOF raw verificato** (2026-10-29): il repro originale al frame 75 produce pixel opachi, differisce dal controllo DOF-off e si ripete byte-identico; dettagli nel checkpoint sotto. Nessun nuovo fix attribuito a questa sessione.
- [ ] **A/B software vs Vulkan su produzione approvata** + repeatability strict (serve host GPU NVIDIA).

## Goal

Chiudere la famiglia ombre/luci T05 (oggi PARTIAL: il dispatch nativo e il
catcher sono implementati, mentre qualità e parità delle ombre native su una
scena approvata non sono ancora certificate) e rendere usabile la pipeline
cinematografica "ground shadow + solid catcher".

## Scope (questa repo)

1. **Pass ombre nativo**: `ShadowNode` ha il dispatch `execute_native` in
   strict-native; resta da certificare la qualità/parità su produzione.
2. **Shadow catcher**: materiale catch-only (il ground solid riceve e mostra
   solo le ombre), semantica alpha corretta sui bordi.
3. **Accumulatore temporale weighted-add nativo**: implementato e verificato
   (vedi criterio chiuso sopra e aggiornamento in `docs/stato/chronon-template.md` §2).
4. **Fix fail-open DOF**: il repro raw storico è superato; DoF-on differisce
   dal controllo off e si ripete byte-identico (evidenza sotto).
5. **A/B software vs Vulkan** su produzione approvata + repeatability strict.

## Fuori scope

- Il light rig autorevole e i preset ombra editoriali (ChrononMotion3D,
  `TICKET-LIGHT-RIG-V1.md`).
- Risoluzione del crash TBB già contenuto (guardia host): resta contenuto.

## Piano di chiusura passo-passo (riconciliato 2026-11-05)

Precondizione trasversale: feature freeze attiva — nessuna feature entra in
produzione finché la CI non è verde sullo SHA esatto (`docs/stato/README.md`);
le certificazioni strict-native richiedono l'host GPU NVIDIA.

1. **Difetti storici superati** — dispatch `Shadow` nativo e raw DoF
   trasparente. Le regressioni locali e i closeout D-06/T16 sono registrati
   sopra; questi repro non sono gate aperti.
2. **Accumulatore temporale weighted-add nativo — CHIUSO** — implementazione,
   integrazione e certificazione 150/150 raw RGBA8 sono documentate sopra.
3. **Pass ombre nativo in lane Vulkan strict-native** — shadow map risolta
   interamente in lane nativa; il nodo `Shadow` decodifica e renderizza senza
   fallback. Il dispatch esiste; il residuo è la parità A/B software con le
   metriche RGB-L1 delle run T05 e la qualità sulla scena approvata.
4. **Shadow catcher catch-only** — il ground solid riceve e mostra solo le
   ombre, alpha corretto sui bordi, nessun residuo di staging.
   Verifica: frame di certificazione 1920×1080.
5. **Shutter 90°/8 campioni — CHIUSO** — il render strict-native completa
   150/150 frame, evidenza raw RGBA8 sopra.
6. **Fix fail-open DOF — CHIUSO per il repro documentato** — DoF-on differisce
   da DoF-off ed è ripetibile; il vecchio frame tutto zero non è riprodotto.
7. **Certificazione A/B software vs Vulkan su produzione approvata** +
   repeatability strict su due run (serve l'host GPU NVIDIA).
8. **Chiusura formale** — CTest verde e golden registrata; spuntare tutte le
   voci di `## Accettazione`; portare `## Stato` a `COMPLETE` (convenzione di
   `docs/tickets/archive/TICKET-PER-KEYFRAME-EASING.md`); spostare il file in
   `docs/tickets/archive/`; registrare lo stato risolto in
   `docs/stato/chronon3d.md` §13 e aggiornare T05.

## Accettazione

- [ ] Nodo `Shadow` verde in strict-native con parità A/B software e qualità
      approvata su scena di produzione. Il dispatch nativo è provato; il gate
      A/B/qualità resta aperto.
- [x] Shadow catcher catch-only: il layer catcher resta trasparente fuori da
      caster/ombra e differisce dal controllo senza ombra (320×240, 6/6).
- [ ] Golden catch-only 1920×1080 con alpha ai bordi e review visiva.
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

## Riconciliazione evidenze 2026-11-05

Il closeout V1 D-06/D-07 (archiviato in `TICKET-CAMERA-3D-V1-CLOSURE.md`)
supera le descrizioni obsolete secondo cui il nodo `Shadow` sarebbe ancora
sempre fail-closed e il catcher mancherebbe. Sono chiusi qui il dispatch
`execute_native` e il criterio funzionale catch-only; restano aperti la qualità
e la parità A/B delle ombre native su produzione, la repeatability strict e il
gate CI same-SHA. Il ticket resta quindi `PARTIAL` e non viene archiviato.

Verifica sorgente corrente 2026-11-05: `chronon3d_renderer_core_tests` PASS
(suite completa), test `Shadow catcher:*` 6/6; `chronon3d_camera_tests` PASS
(suite completa). La fixture catcher è 320×240 e non sostituisce la golden
1920×1080. La suite Vulkan registry aggregata ha riportato 83 casi falliti su
`vkCreateInstance` (`VkResult=-9`) dopo probe iniziali; i test GPU mirati
multi-spot, eseguiti isolatamente, passano. C ABI shadow catcher 9/9 PASS.
Nessuna certificazione same-SHA CI o golden produzione è stata generata.

## Dipendenze

- Host GPU NVIDIA dedicato per la certificazione (blocco trasversale noto:
  `docs/stato/README.md`).
- Nessuna dipendenza da altre repo.

## Ordine

**Primo di tutto** il P1: è prerequisito di spotlight, IBL e del rendering
dei soggetti animati.
