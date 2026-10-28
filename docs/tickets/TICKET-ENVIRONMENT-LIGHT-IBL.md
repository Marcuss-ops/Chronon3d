# TICKET-ENVIRONMENT-LIGHT-IBL — Environment light (HDRI/IBL) nel motore

## Stato
OPEN (2026-10-28). Provenienza: gap analysis "AE + Premiere parity" sui 30
tutorial MotionNations (2026-10-28). Stato registrato in
`docs/stato/chronon3d.md` §13.

## TODO — cosa manca (agg. 2026-10-28)

- [ ] Modello `EnvironmentLight` (probe cubemap, `intensity`, `rotation_y` animabile) fuso con ambient/rim senza duplicarne la semantica.
- [ ] Lane software: irradianza SH (9 coefficienti) al load + specular con mip lookup.
- [ ] Lane Vulkan strict-native: sampling cubemap negli shader di lighting, fail-closed se la risorsa manca.
- [ ] Campo `environment_light` opzionale nello schema RenderPlan + decoder fail-closed + validazione CLI.
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

1. **Interfaccia coi byte cubemap** — accordare con `TICKET-HDRI-ENV-V1.md`
   (ChrononMotion3D) il formato cubemap+SH in ingresso; nel frattempo usare
   fixture fake per non bloccarsi sull'altro repo.
2. **Modello `EnvironmentLight`** in `light_context.hpp`/`lighting_rig.hpp`:
   probe cubemap (handle risorsa), `intensity`, `rotation_y` animabile, fusione
   con ambient/rim esistenti senza duplicarne la semantica.
   Verifica: test di non-regressione — key/rim/ambient restano invariati.
3. **Lane software** — irradianza SH (9 coefficienti) calcolata dal cubemap al
   load + specular approssimata con mip lookup.
   Verifica: coefficienti SH deterministici su HDRI di test noto.
4. **Schema RenderPlan** — campo opzionale `environment_light` accanto a
   `camera_samples`; decoder fail-closed su payload malformati (stile emissione
   V3); validazione CLI.
   Verifica: payload malformato respinto; round-trip deterministico.
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

- [ ] `environment_light` accettato dal decoder RenderPlan e rifiutato
      fail-closed se malformato o non supportato in strict GPU.
- [ ] Parità software/Vulkan su golden dedicata (MAE/p99 nei range della
      famiglia CAM-04).
- [ ] Rotation angle animabile con per-keyframe easing, determinismo verificato.
- [ ] CTest verde; nessuna regressione sulle lane luce esistenti
      (key/rim/ambient restano invariati).

## Dipendenze

- `TICKET-HDRI-ENV-V1.md` (ChrononMotion3D) per i byte del cubemap.
- Convive con `TICKET-LIGHTS-SHADOWS-PARTICLES.md` (nessun contenuto duplicato:
  questo ticket copre solo IBL).

## Ordine

Dopo `TICKET-SHADOW-NATIVE-CATCHER.md` (il pass luce nativo va stabilizzato
prima di aggiungere sorgenti).
