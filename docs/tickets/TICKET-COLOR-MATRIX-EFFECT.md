# Color Matrix effect

**Stato: PARTIAL** — implementazione, schema e test software locali passano; la prova pixel software/Vulkan richiede un host Vulkan disponibile ed è l'unico gate di accettazione rimasto.

## TODO — cosa manca (agg. 2026-10-28)

- [ ] Eseguire il test di parità strict Vulkan del contratto premoltiplicato su un host Vulkan attrezzato; non eseguito nel checkout CPU corrente.

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
- Vulkan pixel parity is already encoded in `tests/registry/test_backend_registry_vulkan_effect_parity.cpp` (`Vulkan ColorMatrix preserves the premultiplied software contract`), including transparent/HDR input, RGBA mixing and biases, alpha clamping, invalid coefficients, and dirty bounds. It was not run because this pass excludes host-GPU certification.

The canonical status document is `docs/CURRENT_STATUS.md`; `docs/stato/chronon3d.md` is absent from this checkout. The implementation remains PARTIAL until the external Vulkan parity gate is observed passing.
