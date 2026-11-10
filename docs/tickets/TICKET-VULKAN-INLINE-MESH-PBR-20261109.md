# TICKET-VULKAN-INLINE-MESH-PBR-20261109 — La lane Vulkan ignora la texture metallic-roughness della mesh inline

- **Stato:** OPEN (2026-11-09)
- **Priorità:** P1
- **Data:** 2026-11-09
- **Ambito:** Chronon3d Vulkan strict-native; inline mesh V3; consumatore del bridge ChrononMotion3D
- **Owner:** Chronon3d Vulkan inline-mesh owner
- **Parent:** [TICKET-RENDER-STACK-HARDENING-20261106](TICKET-RENDER-STACK-HARDENING-20261106.md) W3; [TICKET-CM3D-REMAINING-WORK-20261106](../../../ChrononMotion3D/docs/tickets/TICKET-CM3D-REMAINING-WORK-20261106.md) §4/§7
- **Tipo:** engine feature + pixel certification; nessun cambio di schema RenderPlan, nessun secondo renderer

> Evidenza di codice letta sull'albero di lavoro al 2026-11-09. Non è una
> certificazione same-SHA: C3D e CM3D hanno modifiche non committate.

---

## 1. Problema

RenderPlan V3 trasporta già tutto il materiale PBR della mesh inline:

- `InlineMeshMaterialPlan` con `base_color`, `metallic`, `roughness`,
  `emissive`, `double_sided`, `alpha_cutoff`
  (`Chronon3d/include/chronon3d/render_plan/inline_mesh.hpp:23-36`);
- `InlineMeshPlan::metallic_roughness_texture_asset`, dove il canale **G**
  porta la roughness e il canale **B** il metallic secondo glTF
  (`inline_mesh.hpp:54-59`);
- il decoder, il compilatore, il fingerprint e la scena materializzata
  portano tutti il campo fino a `RenderNode`
  (`src/render_plan/render_plan_decoder_layer_geometry.cpp:22-26`,
  `src/compiler/scene_materializer.cpp:232`,
  `src/render_plan/fingerprint_layer_identity.cpp:108`).

Il renderer **software** consuma davvero il materiale: decodifica l'immagine
metallic-roughness, la converte in linear-sRGB e la passa a
`render_mesh_filled_lit`
(`src/backends/software/processors/software_mesh_processor.cpp:101-132`).

La lane **Vulkan strict-native** è solo parziale:

- `src/backends/vulkan/shaders/inline_mesh.comp` ha una sola texture
  (`binding = 1`, base color), un tint nei push constant, alpha cutoff e
  depth. Non ha un binding per la metallic-roughness e non valuta il
  materiale per-pixel;
- i fattori scalari `metallic`/`roughness` **non sono ignorati del tutto**:
  `source_node_native_mesh.cpp:172-218` li copia in `Material2_5D` e calcola
  l'illuminazione **per-vertex**. È un'approssimazione, non il PBR per-pixel
  del software;
- la texture `inline_mesh_metallic_roughness_texture_asset` non viene né
  preparata né caricata: il path nativo carica solo
  `inline_mesh_texture_asset` (`source_node_native_mesh.cpp:67-99`) e
  `draw_inline_mesh_surface` non ha un handle per la MR
  (`vulkan_backend_surface_api_effects_private.cpp:588-605`);
- `emissive` non viene copiato in `Material2_5D`; `double_sided` sì, ma la
  sua semantica di culling non è certificata su questa lane.

**Conseguenza brutale:** un piano V3 con
`inline_mesh.metallic_roughness_texture` viene validato, fingerprintato e
accettato, ma sulla lane Vulkan la texture MR viene **ignorata in silenzio**:
la mesh usa solo il colore base, il tint e l'illuminazione per-vertex. Due
materiali con la stessa base color e MR diversa possono risultare identici.
Non c'è errore, non c'è fallback dichiarato, non c'è capability negata. La
certificazione PBR esistente (`TICKET-PBR-MATERIALS-V1`, COMPLETE 2026-10-28)
copre il percorso software; non copre Vulkan.

Questo non è un dettaglio estetico. Night File e Deep Dive usano materiali
metallici/plastici e superfici x-ray; se il target è la lane GPU di produzione,
oggi non possono rivendicare lo stesso materiale del software.

---

## 2. Cosa manca esattamente

### 2.1 Binding e campionamento per-pixel

- [ ] Aggiungere al kernel inline una seconda texture image (metallic-roughness)
      e il materiale per-pixel (fattori + emissive). Riutilizzare il descriptor
      layout e la pipeline `GpuKernelId::InlineMesh`; non creare un secondo
      kernel o un secondo resolver.
- [ ] Campionare G (roughness) e B (metallic) con UV0, clamp e spazio lineare,
      coerenti con il percorso software. La texture base color resta separata.
- [ ] Gestire il caso "materiale presente, texture MR assente" con i fattori
      scalari; il caso "texture MR presente" con il prodotto
      `fattore × texel` come da glTF metallic-roughness.
- [ ] Portare `emissive`, `double_sided` e `alpha_cutoff` alla stessa semantica
      del software. Se un campo non è supportato, il backend deve rifiutare la
      capability o dichiarare un fallback esplicito, non ignorarlo.

### 2.2 Shading e parità

- [ ] La shader inline oggi riceve `vertex_light` precalcolato sull'host
      (`inline_mesh.comp:33-36`, `vertex_light`). Con una texture MR il
      contributo speculare non può restare per-vertex: serve una valutazione
      per-pixel coerente con `render_mesh_filled_lit`.
- [ ] Definire una volta sola la formula di shading condivisa con il software
      (o un oracle numerico con tolleranza) e documentarla. Non duplicare la
      formula in due implementazioni divergenti senza test di parità.
- [ ] Misurare la differenza su un frame raw, non su un MP4: encoder, chroma e
      container non sono una prova di materiale.

### 2.3 Test e fail-closed

- [ ] Aggiungere una fixture V3 con due materiali che hanno **la stessa base
      color** e **MR diversa** (per esempio una piastra con roughness 0.15 e
      metallic 0.9, e una con roughness 0.9 e metallic 0.0), stessa camera e
      stessa luce. Se la texture non viene campionata, i due materiali devono
      risultare identici: il test deve fallire.
- [ ] Verificare separatamente il canale G (roughness) e il canale B
      (metallic), non solo "il frame cambia".
- [ ] Aggiungere un test che dimostri che una texture MR presente ma non
      supportata **non** viene accettata in silenzio su una lane che dichiara
      PBR nativo.
- [ ] Verificare che la lane software resti il riferimento e che i due backend
      usino lo stesso piano, gli stessi asset e gli stessi frame.

### 2.4 Documentazione

- [ ] Aggiornare `TICKET-RENDER-STACK-HARDENING-20261106.md` W3: la riga
      "sampling software V3 lato C3D" è **stale**. Il software campiona già
      base color, alpha e G/B; il gap residuo è la parità Vulkan.
- [ ] Aggiornare `docs/stato/chronon3d.md` e `docs/CURRENT_STATUS.md` solo
      quando esiste un pixel gate, non quando il campo è serializzato.

---

## 3. Criteri di accettazione

- [ ] Un piano V3 con `inline_mesh.material` e
      `inline_mesh.metallic_roughness_texture` produce su Vulkan una mesh in cui
      la texture MR cambia i pixel; due materiali con la stessa base color e MR
      diversa non sono identici.
- [ ] Parità software/Vulkan misurata su raw RGBA8, con soglie preregistrate
      **dopo** il primo run, non scelte a posteriori per far passare il test.
      La metrica deve includere la ROI della mesh e non solo il full-frame.
- [ ] Zero errori di validazione Vulkan, zero readback/staging, zero fallback;
      la lane è `--backend vulkan --gpu-hot-path-mode require_gpu_native`.
- [ ] I test C ABI, renderer core, backend registry e compiler boundary restano
      verdi; nessun caso skippato o rimosso.
- [ ] Evidenza in `Chronon3d/docs/evidence/` con piano, asset, comandi,
      output, hash e confronto software/Vulkan.
- [ ] `TICKET-PBR-MATERIALS-V1` resta la chiusura del lato **software**; questo
      ticket chiude il lato **Vulkan**. Non si promuove l'uno per l'altro.

**Non-goals:** normal map, occlusion map, clearcoat/sheen/transmission/IOR,
un material graph, un secondo renderer, un cambio di schema RenderPlan. I campi
esistono già; manca il consumatore.

---

## 4. Impatto

| Formato | Impatto |
|---|---|
| Inside Out / Stick Story / Paper Atlas (2D) | Nessuno: non usano mesh PBR. |
| Tall Tale (2.5D) | Basso: layer e camera; il PBR non è il centro. |
| Night File (3D) | Alto sulla lane GPU: metallo/plastica e riflessi non sono certificati. |
| Deep Dive (3D) | Alto: x-ray/emissive/metallo richiedono il materiale; il software è l'unica lane onesta. |

**Regola:** se il target è un pilota software, questo ticket non blocca. Se il
target è la produzione GPU strict-native, blocca la rivendicazione "PBR
character" e va chiuso prima di dire che il motore è pronto.

---

## 12. Implementazione (2026-11-09)

Stato dopo l'implementazione: **CODICE LANDED, GATE PIXEL LOCALE PASSATO; PARITÀ NUMERICA E SAME-SHA ANCORA APERTE.**

- `inline_mesh.comp` ha il binding 4 per la metallic-roughness (G roughness,
  B metallic) e legge un normale world-space per vertice (stride 11 float).
- Il percorso per-pixel replica `render_mesh_filled_lit`:
  `roughness = clamp(roughness * G, 0.04, 1)`,
  `shininess = clamp(2/r² - 2, 1, 1024)`,
  `metallic = clamp(metallic * B, 0, 1)`, `specular = 0.04(1-m)+m`,
  `diffuse *= 1-m`, luce direzionale + ambient + rim derivato dallo speculare.
  L'emissive viene sommato dopo la luce, come nel software.
- I push constant sono esattamente 128 byte (il minimo garantito): environment
  probe e rim light esplicita **falliscono chiusi** sul percorso per-pixel,
  invece di essere ignorati in silenzio.
- La lane Vulkan carica l'asset MR in linear-sRGB e lo passa come surface GPU,
  con residency nel `GpuAssetCache`.

File: `src/backends/vulkan/shaders/inline_mesh.comp`,
`src/backends/vulkan/vulkan_backend_inline_mesh_private.cpp`,
`src/backends/vulkan/vulkan_backend_surface_api_effects_private.cpp`,
`include/chronon3d/render_graph/render_backend_texture_api.hpp`,
`src/render_graph/nodes/source_node_native_mesh.cpp`.

Verifica eseguita:
- `glslangValidator -V` e `spirv-val`: **verdi**.
- `-fsyntax-only` sui TU C3D modificati e sul test di dispatch: **verdi**.
- Nuovo caso `Vulkan inline mesh samples metallic-roughness per pixel` in
  `tests/registry/test_backend_registry_vulkan_dispatch.cpp`, con lo stesso
  confronto rough/smooth dell'oracle software.

Gate eseguito: `chronon3d_backend_registry_tests --test-case="Vulkan inline
mesh samples metallic-roughness per pixel"` → **1/1 caso, 16/16 asserzioni,
exit 0**. Il caso prova che il lato rough (G=1) è più luminoso del lato smooth
(G=0) e che l'alpha resta opaco.
Restano: parità numerica Vulkan↔software sullo stesso frame e certificazione
same-SHA.
