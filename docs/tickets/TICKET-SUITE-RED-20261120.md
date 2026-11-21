# TICKET-SUITE-RED — due gate rossi a HEAD dopo il checkpoint 2026-11-20

**Stato:** CLOSED (aggiornato 2026-11-21) — A e B **risolti** con evidenza
(`deterministic_tests` 94/94 con CLI con encoder; `golden_e2e_tests` Passed 7,43 s dopo
il rebaseline `997d684d4`). A3 chiuso: la lane `linux-video-test` è riconfigurata con
`CHRONON3D_ENABLE_NATIVE_FFMPEG=ON` e la suite gira verde **con il CLI della lane**
(94/94, 11.797 assert, rc 0).
**Priorità:** P1
**Data:** 2026-11-20
**Ambito:** Chronon3d — suite `linux-video-test`, gate golden e2e e lifetime Vulkan
**Owner:** C3D Vulkan backend owner (A); render/visual owner (B)
**Tipo:** finding ticket — non autorizza nuovi renderer né cambia i golden senza review

## Contesto

La suite completa a `Chronon3d@78a7473b7` (2026-11-19, tree pulito) dà **92/94
test passati**. I due rossi hanno cause distinte e riproducibili, entrambe
documentate con comandi e log in
[`docs/evidence/reconciliation-20261120/README.md`](../evidence/reconciliation-20261120/README.md).

## A — `chronon3d_deterministic_tests`: `VkDescriptorSetLayout` non distrutto

- Evidenza: `tests/deterministic/test_text_warm_cold.cpp:279` fallisce su
  `VUID-vkDestroyDevice-device-00378`; il layout `0xc000000000c` resta vivo
  quando il device viene distrutto.
- Riproduzione: `CHRONON3D_CLI_BINARY=<release>/chronon3d_cli \
  build/chronon/linux-video-test/tests/chronon3d_deterministic_tests`.
  Con il CLI della lane test il test fallisce prima, perché quella lane ha
  `CHRONON3D_ENABLE_NATIVE_FFMPEG=OFF` e non apre l'encoder: è un limite di
  configurazione, non la causa del leak.
- Attribuzione: HEAD tocca solo compiler/materializer, quindi il leak precede
  `78a7473b7`; i candidati sono i commit Vulkan `ff949336a`, `588f2c14f`,
  `18077e3ae`, `c33bbe33f` (quest'ultimo introduce un layout dedicato per
  l'inline mesh).
- Non è coperto dal perimetro SyncVal di
  [RENDER-STACK-HARDENING](TICKET-RENDER-STACK-HARDENING-20261106.md): qui è
  lifetime di oggetti, non sincronizzazione.

Criteri di accettazione:

- [x] Il layout dedicato viene distrutto prima del device; il test
      `test_text_warm_cold` passa senza sopprimere gli errori di validazione.
- [x] Nessun nuovo VUID `vkDestroyDevice-device-00378` nei log della suite.
- [x] La lane test viene riconfigurata con `CHRONON3D_ENABLE_NATIVE_FFMPEG=ON`
      (o il test salta esplicitamente quando l'encoder manca), così il rosso
      iniziale non maschera quello vero.

### Esito A — risolto 2026-11-20

Causa: `inline_mesh_bin_descriptor_layout` è creato in
`src/backends/vulkan/vulkan_backend_lifecycle_layouts_private.cpp:150` ma il
teardown in `vulkan_backend_lifecycle_private.cpp` distruggeva solo
`inline_mesh_descriptor_layout`. Aggiunta la `vkDestroyDescriptorSetLayout`
nel blocco dopo `kernels.destroy(device)` e i pool, prima del device.

Verifica: rebuild Release incrementale (`5/5`, 50 s) e
`CHRONON3D_CLI_BINARY=<release>/chronon3d_cli chronon3d_deterministic_tests`
→ **94/94 casi, 11.797 assert, 0 errori di validazione, rc 0**.

A3 — chiuso 2026-11-21. `build/chronon/linux-video-test/CMakeCache.txt` ha ora
`CHRONON3D_ENABLE_NATIVE_FFMPEG:BOOL=ON`, quindi il CLI della lane compila il percorso
nativo. Verifica end-to-end, senza puntare al CLI Release:

```
ninja -C build/chronon/linux-video-test -j4 chronon3d_deterministic_tests chronon3d_cli
CHRONON3D_CLI_BINARY=$PWD/build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli \
  ./build/chronon/linux-video-test/tests/chronon3d_deterministic_tests
# [doctest] test cases: 94 | 94 passed | 0 failed | 0 skipped
# [doctest] assertions: 11797 | 11797 passed | 0 failed
# [doctest] Status: SUCCESS!   (rc 0)
```

Il caso `Typed native shutter submits each sample before encoder handoff` viene quindi
esercitato davvero (0 skipped) e il rosso iniziale non può più mascherare una
regressione dietro un limite di configurazione. In aggiunta, il caso ora dichiara
esplicitamente il limite quando la lane è costruita con l'encoder OFF
(`MESSAGE` + `return` in `tests/deterministic/test_text_warm_cold.cpp`), così una
configurazione senza encoder non produce più un rosso indistinguibile da un bug.

## B — `chronon3d_golden_e2e_tests`: drift di 02 e 03 rispetto ai golden

- Evidenza: `01_editorial_text` combacia; `02_packshot_2p5d` produce
  `2ef9c9b7ed3aae8f` (golden `38d28c27625c0e4b`) e `03_mixed_mesh`
  `0cf034343ca695d4` (golden `eb961e58b33b6167`). Il runner passa sempre
  `--backend software`, quindi non è una differenza di driver.
- Attribuzione: `goldens.v1.json` dichiara `renderer_sha 30b4b721a`,
  `backend software`, 2026-11-16. HEAD `78a7473b7` modifica
  `scene_materializer.cpp` e `scene_lowering.cpp`, coerente con i due piani
  colpiti e non con il piano testo.
- Non aggiornare `goldens.v1.json` senza review visiva: la procedura è quella
  di [GOLDEN-E2E-REBASELINE](archive/TICKET-GOLDEN-E2E-REBASELINE-20261116.md).

Criteri di accettazione:

- [x] Frame 0 dei tre piani rivisto visivamente; delta atteso o bug classificato.
- [x] `goldens.v1.json` riallineato con `renderer_sha`, data e review aggiornati.
- [x] `ctest -R chronon3d_golden_e2e_tests` verde in una lane software.

### Esito B — risolto 2026-11-20

Il delta è confinato ai pannelli immagine di 02/03. Causa: `78a7473b7` ha
sostituito il `static_cast` tra `LayerFitMode` e `FitMode` con una mappatura
esplicita; i due enum dichiarano i valori in ordini diversi, quindi
`"fit": "stretch"` si comportava da cover (ritaglio) e ora stira davvero.
Il piano solo-testo non cambia, coerente col pattern osservato.

Rebaseline eseguita con `run_golden_e2e.py --update-goldens` e CLI Release
(36 frame, 3 scene deterministiche). Il runner riscrive solo
`schema/generator/scenes`: i metadati `backend/date/host/renderer_sha/review`
sono stati ripristinati a mano, con `renderer_sha=78a7473b7` (commit che ha
prodotto gli hash; i successivi `cfc4a75a4`/`647adfe23` e la modalità scanline
in lavorazione non toccano questi piani).

`ctest -R chronon3d_golden_e2e_tests` → **Passed, 7,43 s**.

## Fuori scope

- Gate SVG, content e OCIO: non compilati nelle lane correnti.
- CI same-SHA: resta in [MAIN-TRUTH](TICKET-MAIN-TRUTH-STAGE-1-2.md).
