# Color Matrix effect

**Stato: COMPLETE (2026-10-29)** — implementazione, schema, suite software e prova pixel strict Vulkan/software PASS su NVIDIA RTX A4000. Chiusura funzionale locale; nessuna certificazione CI clean-SHA è inclusa.

## TODO — cosa manca (agg. 2026-10-28)

- [x] Test di parità strict Vulkan del contratto premoltiplicato eseguito su NVIDIA RTX A4000: 1/1 caso selezionato, 18/18 assertion, errore assoluto massimo 5.96046e-08 (tolleranza 2e-4).

## Scope

Add a canonical 4x5 RGBA color matrix to Chronon3d so authoring can express
duotone, sepia, channel remapping, alpha transforms, and other affine grades
without a dedicated effect per look.

## Implementation

- Canonical ID: `color.matrix` (effect type 53).
- Coefficients are row-major: four output rows, each containing RGBA weights
  followed by a bias. Coefficients are bounded to `[-16, 16]`.
- Render Plan catalog parameters use `m00`...`m33` and `b0`...`b3`; the
  coefficients are animatable.
- Software execution uses the existing fused `ColorPipeline`.
- Vulkan reuses the native color-matrix compute kernel and preserves the
  software premultiplied-alpha contract; output alpha is clamped to `[0, 1]`.
- No Skia dependency is introduced.

## Validation

- Schema: `python3 -m json.tool schemas/json/chronon.render-plan.v3.extensions.schema.json` — PASS; `color.matrix` è ammesso sia nel catalogo parametri sia nell'animazione.
- Local implementation/catalog tests: `cmake --build build/chronon/linux-video-fast-dev --target chronon3d_effects_tests -j12` — PASS; binario costruito il 2026-10-28 19:23 UTC, dopo i sorgenti di test.
- Effects suite: `ctest --test-dir build/chronon/linux-video-fast-dev -R '^chronon3d_effects_tests$' --output-on-failure` — PASS, 1/1.
- Production CLI build: `cmake --build build/chronon/linux-video-fast-dev --target chronon3d_cli -j12` — PASS; ricostruzione finale dopo la configurazione test, CLI prodotto il 2026-10-28 19:31 UTC (verificato `ninja -n`: no work to do).
- Vulkan pixel parity is encoded in `tests/registry/test_backend_registry_vulkan_effect_parity.cpp` (`Vulkan ColorMatrix preserves the premultiplied software contract`), including transparent/HDR input, RGBA mixing and biases, alpha clamping, invalid coefficients, and dirty bounds. This focused test now passes on the hardware lane below.

## Chiusura del gate hardware — 2026-10-29

Base `d37725c97b1c47c133d62c6f5d6a4cd8fdca2471`, worktree dirty con il
lavoro AE preesistente; Linux x86_64, Debug, Vulkan ON. Non è una prova
clean-SHA né CI e non chiude i gate globali di release.

- `cmake --build build/chronon/linux-video-test --target chronon3d_backend_registry_tests -j16` — PASS, exit 0; log locale `.tmp/ticket-color-matrix-build.log`.
- Binary esistente e più recente di `tests/registry/test_backend_registry_vulkan_effect_parity.cpp` verificato prima dell'esecuzione.
- `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json vulkaninfo --summary` — PASS, solo NVIDIA RTX A4000, driver 595.99.02; log `.tmp/ticket-color-matrix-device.log`. Il primo tentativo con `VK_LOADER_DRIVERS_SELECT` enumerava anche llvmpipe, quindi la prova conclusiva usa l'ICD NVIDIA esplicito.
- `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json build/chronon/linux-video-test/tests/chronon3d_backend_registry_tests --test-case='Vulkan ColorMatrix preserves the premultiplied software contract' --no-colors` — PASS, exit 0, 1/1 caso selezionato, 18/18 assertion. Log `.tmp/ticket-color-matrix-current-vulkan.log`.
- L'oracle è il processor software reale; `EffectStackNode` usa `require_native_gpu=true`, produce una superficie nativa e il test ne scarica i pixel per il confronto. Questo readback diagnostico NON certifica zero-readback end-to-end.
- Identità/HDR/trasparenza e clamp alpha: errore 0; mix RGBA/bias e clip: massimo 5.96046e-08, sotto la tolleranza 2e-4; coefficienti invalidi rifiutati da entrambi i percorsi.
- Suite software effects corrente: PASS, 304/304 casi, 5,540,351 assertion nel run CTest del 29-10.

I log citati sono artefatti locali gitignored, non certificati pubblicati.
Il solo gate residuo di questa scheda era la parità hardware, ora osservata
PASS. La certificazione globale CI same-SHA resta separata in
[`TICKET-MAIN-TRUTH-STAGE-1-2`](../TICKET-MAIN-TRUTH-STAGE-1-2.md).
