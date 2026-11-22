# TICKET-C3D-LUT-STEADYSTATE-VERIFY-2026-11-22 — dimostrare la cache LUT con i contatori

**Stato:** `DONE (measured)` (2026-11-22)
**Priorità:** P1
**Data:** 2026-11-22
**Owner:** render graph / cache
**Origine:** C3D-AUDIT-02 (fix atterrato, verifica diretta mancante)
**Padre:** [TICKET-CHRONON-AUDIT-2026-11-22](TICKET-CHRONON-AUDIT-2026-11-22.md)

## Problema

Il fix C3D-AUDIT-02 è atterrato: `bulk_cached_lut_aux` usa `GpuAssetCache` con
chiave SHA-256 della tabella e rilascia la surface aux solo su miss. La parità
pixel Curves/Lut3D è verde, ma **non esiste un test che dimostri con i contatori
che il secondo frame non chiama `create_surface`/`upload_surface`**.

Senza questo test il gate “steady state senza upload” resta un'inferenza dal
codice, non una misura.

## Evidenza

- `[CODICE]` `src/render_graph/nodes/effect_stack_node_native_bulk.cpp`,
  `bulk_cached_lut_aux`.
- `[TEST]` `tests/registry/test_backend_registry_vulkan_effect_parity.part02.cpp:508`
  → 16/16 asserzioni, max abs error `5.96e-08`.
- `[TEST]` `tests/runtime/test_gpu_asset_cache_content_addressed.cpp` copre già
  hit/miss/eviction del cache generico, ma non il path LUT completo.

## Piano di chiusura

1. Aggiungere `tests/render_graph/nodes/test_effect_stack_lut_cache.cpp` a
   `chronon3d_render_graph_tests`.
2. Usare un `RenderBackend` contatore (stesso pattern di `CacheBackend` in
   `tests/runtime/test_gpu_asset_cache_content_addressed.cpp`) e un
   `GpuAssetCache` attaccato al registry.
3. Eseguire due volte lo stesso effetto Curves e lo stesso Lut3D con parametri
   identici; verificare:
   - primo frame: 1 `create_surface` + 1 `upload_surface` per aux;
   - secondo frame: **0** nuove create/upload, `GpuAssetCacheStats.hits` +1;
   - handle aux identico e nessun `release_surface` della surface cachata.
4. Cambiare un solo parametro della tabella: deve esserci un nuovo upload.
   Ripristinare i parametri originali: deve tornare un hit (budget permettendo).
5. Verificare il budget: con una cache piccola, l'eviction è contata e non
   corrompe il dispatch.
6. Eseguire il test di parità Curves/Lut3D già esistente.

## Gate di accettazione

- [x] Secondo frame LUT: `create_surface` e `upload_surface` delta = 0.
- [x] `initial_uploads` invariato tra frame 1 e frame 2; `hits >= 1`.
- [x] Cambio tabella → nuovo upload; ripristino → hit.
- [x] Nessun release della surface posseduta dalla cache.
- [x] Parità pixel Curves/Lut3D entro la tolleranza esistente.
- [x] Test eseguito sul working tree che contiene il test (nessun commit creato).

## Comandi di verifica

```bash
ninja -C build/chronon/linux-video-test -j2 chronon3d_render_graph_tests
./build/chronon/linux-video-test/tests/chronon3d_render_graph_tests \
  -tc='*LUT cache*'

./build/chronon/linux-video-test/tests/chronon3d_backend_registry_tests \
  -tc='Strict Vulkan Curves and Lut3D match software'
```

## Rischi e rollback

- Il test non deve dipendere dal driver Vulkan: usare il backend contatore e il
  path software/nativo già usato dai test di cache.
- Se il path LUT non riceve `gpu_asset_cache` (fixture senza cache), il test
  deve attaccarlo esplicitamente invece di assuming il fallback.

## Definition of Done

Il ticket si chiude quando il test dimostra con i contatori zero create/upload
al secondo frame, con parità pixel verde. A quel punto C3D-AUDIT-02 può passare
da “DONE (code+parity)” a “DONE (measured)”.

## Chiusura (2026-11-22)

- Nuovo `tests/render_graph/nodes/test_effect_stack_lut_cache.cpp`, registrato
  in `chronon3d_render_graph_tests`.
- `-tc='LUT cache*'`: **2/2 PASS, 56/56 asserzioni**.
- Curves e Lut3D: primo frame 1 upload aux; secondo frame 0 upload e
  `hits +1`; cambio tabella 1 upload; ripristino hit; aux mai rilasciata.
- Suite completa: **469/469 PASS, 19901/19901 asserzioni**.
- Parità esistente: **16/16 PASS**, max abs error `5.96e-08`.
- Nota: il test copre hit/miss/ripristino; l'eviction con budget ridotto resta
  coperta dal test generico `GpuAssetCache`, non da questo caso LUT.
