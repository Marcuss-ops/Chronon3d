# TICKET-C3D-LIGHTWRAP-GPU-2026-11-22 — light-wrap senza download/blur/upload per frame

**Stato:** `OPEN — implementazione GPU pronta; certificazione runtime non eseguita`
**Priorità:** P1
**Data:** 2026-11-22
**Owner:** render graph / Vulkan backend
**Origine:** C3D-AUDIT-01
**Padre:** [TICKET-CHRONON-AUDIT-2026-11-22](TICKET-CHRONON-AUDIT-2026-11-22.md)

## Problema

Con `image_light_wrap_asset` attivo, `LightingNode` esegue ogni frame:

1. `lighting_surface_points` sul backend;
2. `download_surface` dell'intera surface;
3. due blur CPU (`blur_image_box`, `blur_alpha_box`);
4. `upload_surface` della surface modificata.

Il costo è proporzionale al canvas, non al ROI illuminato, e il path GPU→CPU→GPU
annulla il vantaggio del backend nativo.

## Evidenza

- `[CODICE]` `src/render_graph/nodes/lighting_node.cpp:329-403`.
- `[CODICE]` `rg -n "light_wrap" src/backends/vulkan` → **0 match**: non esiste
  uno shader o un'API Vulkan dedicata.
- `[MISURA]` Il gate del padre richiede `download_surface count = 0` per frame;
  oggi il conteggio è almeno 1 per ogni frame con wrap attivo.

## Piano di chiusura

1. **Shader nativo.** Aggiungere un kernel `light_wrap.comp` (o un pass dedicato
   in `lighting.comp`) in `src/backends/vulkan/shaders/`, registrandolo nella
   generazione shader già usata dal preset `linux-video-test`.
2. **API backend.** Esporre su `RenderBackend`/`VulkanBackend` una chiamata
   tipizzata, per esempio `apply_light_wrap_surface(destination, source,
   wrap_aux, params)`. Il fallback CPU resta solo per backend senza capacità.
3. **Cache dell'asset wrap.** Decodificare `image_light_wrap_asset` una volta e
   cachare il blur CPU in `GpuAssetCache`, con chiave
   `content_digest + radius + formato`. Il blur dell'asset non deve dipendere
   dal frame.
4. **Sostituire il blocco per-frame.** In `lighting_node.cpp` sostituire
   download + blur + upload con una sola dispatch GPU; nessuna allocazione
   `rgba`/`tmp_colors` per frame.
5. **Telemetria.** Contare `download_surface`, `upload_surface` e
   `light_wrap_dispatch` per frame; esporli nel sidecar o nei counters esistenti.
6. **Parità.** Conservare il fallback CPU e verificare che il path GPU produca
   lo stesso risultato entro la tolleranza pixel già usata dai test lighting.

## Gate di accettazione

- [ ] Job con wrap: `download_surface` count = **0** per frame.
- [ ] `create_surface`/`upload_surface` del wrap avvengono solo al primo frame o
      dopo cambio asset/raggio; in steady state il delta è 0.
- [ ] MP4/overlay SHA invariato rispetto al baseline CPU.
- [ ] Misura before/after di `gpu_execute_ms` e wall su almeno 30 frame.
- [ ] `chronon3d_deterministic_tests` verde.
- [ ] Il fallback CPU resta disponibile e testato su backend senza capacità.

## Comandi di verifica

```bash
rg -n "light_wrap|download_surface|upload_surface" \
  src/render_graph/nodes/lighting_node.cpp src/backends/vulkan

ninja -C build/chronon/linux-video-test -j2 chronon3d_deterministic_tests
./build/chronon/linux-video-test/tests/chronon3d_deterministic_tests

# Job di certificazione con image_light_wrap_asset:
# eseguire due volte, confrontare SHA e leggere i counters del sidecar.
sha256sum <baseline.mp4> <nuovo.mp4>
```

## Rischi e rollback

- Il kernel GPU può divergere dal blur CPU sul bordo: usare la stessa
  tolleranza pixel dei test lighting e coprire ROI/alpha parziali.
- Se il driver non espone il kernel, il fallback CPU deve essere esplicito e
  contato, non silenzioso.
- Rollback: mantenere il blocco CPU dietro capability check; nessuna modifica
  al formato dell'asset.

## Definition of Done

Il ticket si chiude quando il job di certificazione mostra zero download
per-frame, SHA invariato, speedup misurato e test deterministici verdi. La
rimozione del vecchio blocco CPU può avvenire solo dopo il verde del fallback.

## Esito verifica — 2026-11-22

- `src/render_graph/nodes/lighting_node.cpp`: nessun `download_surface` o
  `upload_surface`; il wrap usa il kernel `lighting.comp` (binding storage-image
  2) e l’asset filtrato resta in `GpuAssetCache` con chiave
  `content_digest + raggio`.
- Il fallback CPU resta nel ramo non nativo.
- `chronon3d_deterministic_tests`: 94/94 verde; compositor CPU: 71/71 verde.
- Smoke Vulkan headless con ICD llvmpipe: test “persistent headless device”
  28/28 verde; backend registry completo 161/161.
- **Non misurati:** conteggio runtime `download_surface=0`, parità pixel
  GPU/CPU, SHA MP4/overlay contro baseline e speedup before/after. Non esiste
  ancora un test Vulkan dedicato al light-wrap; il ticket resta aperto.
