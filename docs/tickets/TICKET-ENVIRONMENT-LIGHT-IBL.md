# TICKET-ENVIRONMENT-LIGHT-IBL — Environment light (HDRI/IBL) nel motore

## Stato
PARTIAL (2026-10-28). Provenienza: gap analysis "AE + Premiere parity" sui 30
tutorial MotionNations (2026-10-28). Stato registrato in
`docs/stato/chronon3d.md` §13. ChrononMotion3D e Chronon3D hanno concordato il
`HDRI_ENV_V1` binary layout; the reader, CPU cubemap/mip/SH output, fixture,
and golden are implemented in Motion3D. V3 RenderPlan field emission and
decoding, fingerprinting, malformed-input validation, and compiler asset
validation are implemented. The compiler resolves the mounted asset, decodes
its binary payload, and checks RenderPlan SH consistency. Software diffuse SH
reaches the common software lighting evaluator for 2.5D and mesh shading.
Software specular mip lookup is implemented and covered by the renderer-core
test. Visible CLI output and strict-native GPU sampling remain open.

## TODO — cosa manca (agg. 2026-10-28)

- [x] Modello diffuse `EnvironmentLight` (SH probe, `intensity`, `rotation_y` statico) fuso con ambient/rim senza duplicarne la semantica.
- [x] Rotazione `rotation_y` animabile via scalar track e shared per-keyframe easing evaluator.
- [x] Lane software: irradianza SH (9 coefficienti) valutata per normale nel lighting software, inclusi intensity e rotazione Y statica.
- [x] Lane software: specular con mip lookup dalla cubemap HDRI, con roughness-to-mip mapping.
- [ ] Lane Vulkan strict-native: sampling cubemap negli shader di lighting, fail-closed se la risorsa manca.
- [x] Campo `environment_light` opzionale V3 + decoder/fingerprint + validazione dell'asset montato e coerenza SH.
- [x] Decoder binario `HDRIENV1` con bounds, finite checks, complete mip
      chain, face ordering, and trailing-byte rejection.
- [x] Runtime compiler resolves `probe_asset` under the mounted root, decodes
      HDRIENV1, and checks asset SH against RenderPlan SH.
- [x] Feed the plan SH into software diffuse shading.
- [x] Feed cubemap mip sampling into software shading.
- [ ] Feed cubemap sampling into strict-native GPU shading.
- [x] Interfaccia HDRI_ENV_V1 byte payload concordata con ChrononMotion3D (`HDRIENV1`, little-endian, six faces/mips + SH).
- [ ] Ordine: dopo `TICKET-SHADOW-NATIVE-CATCHER.md`; dipende dai byte cubemap di `TICKET-HDRI-ENV-V1.md` (ChrononMotion3D).

## Goal

L'environment light (HDRI) è la feature richiesta dai tutorial AE 2026 di
maggioranza relativa (9/30 video, serie "advanced 3D"): illuminazione globale
da HDRI con rotation angle animabile, resa coerente su entrambe le lane
software e Vulkan strict-native.

Oggi il modello luce (`include/chronon3d/rendering/lighting_rig.hpp`) espone
solo key direzionale + rim + ambient tone: non esiste alcuna infrastruttura
IBL/cubemap nel motore.

## Scope (questa repo)

1. **Modello**: `EnvironmentLight` in `light_context.hpp`/`lighting_rig.hpp`:
   probe cubemap (handle risorsa), `intensity`, `rotation_y` (animabile),
   fusione con ambient/rim esistenti senza duplicarne la semantica.
2. **Lane software**: irradianza SH (9 coefficienti) calcolata dal cubemap al
   load; specular approssimata con mip lookup.
3. **Lane Vulkan strict-native**: sampling cubemap negli shader di lighting;
   nessun readback CPU; fail-closed se la risorsa manca.
4. **Schema RenderPlan**: campo opzionale `environment_light` accanto a
   `camera_samples`; decoder fail-closed su payload malformati (stile emissione
   V3); validazione CLI.
5. **Determinismo**: stessi campioni a seed uguale, firma golden stabile.

## Fuori scope

- Il loader HDRI (equirect → cubemap + mip) e il contratto `HDRI_ENV_V1` sono
  di ChrononMotion3D: `ChrononMotion3D/docs/tickets/TICKET-HDRI-ENV-V1.md`.
- Materiali PBR (roughness per lo specular): `TICKET-PBR-MATERIALS-V1.md`
  (ChrononMotion3D).

## Piano di chiusura passo-passo (agg. 2026-10-28)

Precondizione trasversale: feature freeze attiva — nessuna feature entra in
produzione finché la CI non è verde sullo SHA esatto (`docs/stato/README.md`);
le certificazioni di parità richiedono l'host GPU NVIDIA.

1. **Interfaccia coi byte cubemap** — concordata con ChrononMotion3D
   `HDRI_ENV_V1` (`ChrononMotion3D/docs/HDRI_ENV_V1.md`): payload binario
   little-endian con magic `HDRIENV1`, dimensioni, rotazione/intensità,
   27 float SH, mip chain e sei facce RGB float in ordine +X/-X/+Y/-Y/+Z/-Z.
   Il reader producer e la fixture RGBE sono implementati. Il decoder C3D
   accetta e valida il campo metadata/SH di RenderPlan; il caricamento e la
   validazione runtime dei byte referenziati restano aperti.
2. **Modello `EnvironmentLight`** in `light_context.hpp`/`lighting_rig.hpp`:
   probe cubemap (handle risorsa), `intensity`, `rotation_y` animabile, fusione
   con ambient/rim esistenti senza duplicarne la semantica.
   Verifica: test di non-regressione — key/rim/ambient restano invariati.
3. **Lane software** — irradianza SH (9 coefficienti) calcolata dal cubemap al
   load e consumata dal renderer; specular approssimata con mip lookup resta aperta.
   Verifica: coefficienti SH deterministici su HDRI di test noto.
4. **Schema RenderPlan** — completato: campo V3, decode di asset reference,
   intensità, rotazione, 27 SH e animazione `rotation_y`, fingerprint e test.
   Il compiler legge e valida i byte `HDRIENV1`, confronta l'SH del piano e
   collega la diffuse SH e i mip speculari al lighting software. Resta il
   render CLI visibile.
5. **Lane Vulkan strict-native** — sampling cubemap negli shader di lighting,
   nessun readback CPU, fail-closed se la risorsa manca.
   Verifica: parità software/Vulkan (MAE/p99 famiglia CAM-04) su golden
   dedicata.
6. **Rotation angle animabile** con per-keyframe easing (usare la resolver
   condivisa già in main da `TICKET-PER-KEYFRAME-EASING`) e determinismo:
   stessi campioni a seed uguale, firma golden stabile.
7. **Chiusura formale** — CTest verde senza regressione sulle lane luce
   esistenti; spuntare `## Accettazione`; `## Stato` → `COMPLETE`; spostare il
   file in `docs/tickets/archive/`; aggiornare `docs/stato/chronon3d.md` §13.

## Accettazione

- [x] `environment_light` accettato dal decoder RenderPlan; il compiler carica
      e valida l'asset, e il percorso software consuma la parte SH diffusa.
- [x] Asset `HDRIENV1` risolto e validato dal boundary compiler; asset SH deve
      corrispondere al RenderPlan.
- [x] Lighting software valuta SH Lambert-convolved nella basis Y-up condivisa
      con Motion, applica intensity/rotazione Y statica e include i parametri
      nell'hash del lighting context; aggiunto test CPU su SH costante.
- [ ] CLI produce output HDRI visibile.
- [ ] Parità software/Vulkan su golden dedicata (MAE/p99 nei range della
      famiglia CAM-04).
- [x] Rotation angle animabile con per-keyframe easing; fingerprint include la track.
- [x] CTest C ABI verde (full suite); il test HDRI CPU verifica la diffusione SH.
- [ ] Regressione visiva key/rim/ambient sulle lane esistenti e output CLI.

## Dipendenze

- `TICKET-HDRI-ENV-V1.md` (ChrononMotion3D) per i byte del cubemap.
- Convive con `TICKET-LIGHTS-SHADOWS-PARTICLES.md` (nessun contenuto duplicato:
  questo ticket copre solo IBL).

## Ordine

Dopo `TICKET-SHADOW-NATIVE-CATCHER.md` (il pass luce nativo va stabilizzato
prima di aggiungere sorgenti).

## Evidenza locale (2026-10-28)

- `chronon3d_c_abi_tests --test-case='HDRI*' --no-skip` — 3/3 cases,
  34/34 assertions PASS. Copre RenderPlan decode/fingerprint, binary payload,
  mounted-root resolution, SH consistency, software diffuse SH e compilazione.
- `chronon3d_c_abi_tests --test-case='HDRIENV1 asset decoder*' --no-skip` —
  1/1 case, 12/12 assertions PASS. Copre layout little-endian, valori, magic,
  truncation, trailing bytes e metadato non finito.
- `cmake --build build/chronon/linux-video-fast-dev --target chronon3d_c_abi_tests
  chronon3d_cli -j12` — PASS dopo le modifiche software SH e rotation track.
- `ctest --test-dir build/chronon/linux-video-fast-dev --output-on-failure
  -R '^chronon3d_c_abi_tests$'` — PASS, 1/1 target (150.83 s) dopo le ultime
  modifiche. Nessun test host-GPU o CI è incluso in questa evidenza.
- Build locale aggiornata `cmake --build build/chronon/linux-video-fast-dev
  --target chronon3d_c_abi_tests chronon3d_cli
  chronon3d_renderer_core_tests -j12` — PASS il 2026-10-28. CTest mirato
  `chronon3d_renderer_core_tests` + `chronon3d_c_abi_tests` — 2/2 PASS
  (8.21 s + 151.48 s). Visible CLI rendering, specular cubemap sampling e
  Vulkan GPU parity restano aperti.
- `cmake --build build/chronon/linux-video-fast-dev --target
  chronon3d_renderer_core_tests chronon3d_c_abi_tests -j12` — PASS dopo
  l'aggiunta del cubemap ai `LightContext` software.
- `ctest --test-dir build/chronon/linux-video-fast-dev --output-on-failure
  -R '^chronon3d_renderer_core_tests$'` — PASS (1/1, 8.42 s); include il test
  `environment specular samples the HDRI cubemap mip selected by roughness`.
- `ctest --test-dir build/chronon/linux-video-fast-dev --output-on-failure
  -R '^chronon3d_c_abi_tests$'` — PASS (1/1, 354.45 s).
- `cmake --build build/chronon/linux-video-fast-dev --target chronon3d_cli
  -j12` — PASS (104/104).
- Strict-native Vulkan sampling/parity e output CLI dedicato non sono ancora
  verificati.
