# TICKET-MAP-VECTOR-FILL-NATIVE-20261116 — fill nativo a N vertici per le placche vettoriali

- **Stato:** PASS — 2026-11-16
- **Ambito:** Chronon3d, Vulkan native shape dispatch (path fill), lowering `ModernMap1` in RenderingGen
- **Origine:** mappe `ModernMap1` (Italia/Giappone) con fill del territorio a triangoli e reveal a opacità per layer
- **Verifica:** build `linux-video-release` (`ninja -j4 apps/chronon3d_cli/chronon3d_cli`); `go build ./...`, `go vet ./internal/overlay/`, `go test ./internal/overlay/ -count=1` PASS; `tests/chronon3d_vulkan_debug_context_tests` 12/12 casi PASS (parità pixel fill a N vertici vs software); render 1920×1080, 192 frame per piano (3 piani) con lane video ffmpeg e timing sidecar del CLI

## Sintomo

`try_native_path_fill` promuoveva solo path con **1 contorno da 3 a 8 vertici**;
tutto il resto finiva sul rasterizer software. Le mappe quindi triangolavano
ogni placca in decine di layer (45 nel piano Italia) e il reveal animava
l'opacità di ognuno: 122 passate composite per frame, `avg_frame_ms = 1839`,
fill che "poppava" a triangoli e bordi interni visibili.

Le maschere non sono un'alternativa: `apply_scene_layer_masks` le applica come
`MaskNode`, che non produce clipping sui layer shape (verificato su backend
Vulkan e software).

## Modifica

1. **Shader** `src/backends/vulkan/shaders/fill_rect.comp`, nuovo kind `10`:
   poligono even-odd letto dallo SSBO del path-geometry buffer, con copertura
   analitica dalla distanza minima al bordo (ramp di 1 px). Lo stream è
   `{contour_count, [vertex_count, x, y, …] × n}`: ogni contorno è chiuso su se
   stesso, altrimenti l'ultimo vertice di un'isola verrebbe collegato al primo
   della successiva e la parità even-odd capovolgerebbe l'area attraversata
   (primo tentativo: cuneo di carta dentro la penisola).
2. **Backend** `Impl::fill_polygon` + `VulkanBackend::fill_polygon_surface`:
   upload dei vertici nel buffer per-slot già usato dai path dashed (nessun
   buffer nuovo), dispatch limitato al bbox del path, blend premultiplied
   invariato.
3. **Promozione** `try_native_path_fill`: accetta `contours.size() >= 1` e
   qualunque numero di vertici, unisce tutti i contorni in una sola dispatch,
   `ramp = 1.0f`.
4. **Lowering Go** (`semantic_map_modern.go`): un solo layer `:territory` per
   placca, con tutte le isole come subpath (`modernMapPathGeometry`),
   niente più `triangulateModernMapRing`/`mergeConvexModernMapTriangles`
   (rimossi). Il reveal diventa un unico cover carta con gradiente lineare
   trasparente→carta sul bordo d'attacco che spazza da ovest a est
   (`:fillwipe`), con `duration_frames = wipe + 1`: così il fill resta statico e
   cacheable e, dopo la spazzata, il renderer torna a emettere frame duplicati.
   Le keyframe di un track componente `position_x` sono **delta** rispetto alla
   posizione del layer, non coordinate assolute.

## Misure (1920×1080, 24 fps, 192 frame)

| Piano | Prima | Dopo |
|---|---|---|
| Italia pin | 63 layer, 122 passate, `avg_frame_ms` 1839 (misura su 48 frame) | 20 layer, 37 passate, `avg_frame_ms` 55.2, 176/192 frame duplicati |
| Italia route | — | `avg_frame_ms` 62.7, 176/192 duplicati |
| Giappone pin | — | `avg_frame_ms` 55.7, 176/192 duplicati |
| Wall clock per clip 8 s | 98 s su 48 frame | 15–17 s su 192 frame |
| Peso MP4 8 s | 17 MB (render precedente) | 0.33–0.51 MB |

Coda statica misurata a parte: 13.7 ms/frame con il cover spento dopo la
spazzata, contro ~230 ms/frame se il cover animato resta vivo per tutto il clip.

## Fill a N vertici anche nel ramo fill+stroke

`try_native_path_stroke` accettava un solo contorno e al massimo otto vertici di
fill (push constant `std::array<Vec2, 8>`); oltre quella soglia il path con bordo
ridisegnava tutto in software. Ora il poligono di fill viaggia nello stesso
SSBO dei punti dello stroke: `fill_dashed_path` accetta
`fill_polygon` + `fill_contours` e appende
`{contour_count, [vertex_count, xy…] × n}` dopo i punti, quindi lo shader
kind `8` testa l'interno leggendo i contorni (una chiusura per contorno, come
kind `10`) e la promozione non ha più né il cap a otto vertici né il vincolo di
contorno unico. `fill_polygon` accetta `ramp = 0` (copertura binaria) per
l'interno dello stroke, allineato al primo-hit del rasterizer software.

Bug chiuso insieme: per uno stroke **solido** l'interno non veniva disegnato
affatto. Lo shader esce sui pixel interni (il fill vince, lo stroke è saltato
dentro) e la passata di fill separata partiva solo con `pattern` non vuoto,
cioè solo per i path dashed: un path pieno con bordo solido restava cavo. La
passata di fill ora parte per ogni path con fill visibile, ritagliata sul bbox
del poligono.

## Parità pixel con il rasterizer software

`tests/backends/test_vulkan_debug_context.cpp`, caso *Vulkan native N-vertex
path fills match the software rasterizer*: placca a 12 vertici (tutti i lati
diagonali, oltre il vecchio cap) disegnata da `draw_node` in tre configurazioni
— solo fill, fill + stroke solido 5 px, fill + stroke dashed 4 px — confrontata
pixel per pixel con `renderer::PathRasterizer`.

- interno (pixel ad almeno `width/2 + 1` px dal contorno): **0 differenze** con
tolleranza 1e-4 per canale — 8684 px solo-fill, 7952 fill+stroke solido,
8136 fill+stroke dashed;
- bordo: kernel diversi (ramp analitico 1 px nel compute, area esatta nel CPU)
e join dei vertici, quindi si confronta la copertura: somma alpha entro 0.5%
su tutto il frame ed entro 5% nella fascia di bordo (misurato: 0.16% e 1.5%
solo-fill, 0.03% e 0.09% con stroke solido, identico con dashed).

Il test avrebbe fallito sul bug del fill cavo (migliaia di pixel interni a zero)
e su una geometria spostata (copertura fuori tolleranza). Suite completa
`chronon3d_vulkan_debug_context_tests`: 12/12 casi, 34911 asserzioni PASS.

## Costo dei frame animati del wipe

Misura sui sidecar (`frame_times_ms[].gpu_observed`, `job.cpu_breakdown`) con
`wipe = 14`:

| Frame | render totale | device span GPU | passate | composite | layer_batch | node dispatch |
|---|---|---|---|---|---|---|
| 0–14 (wipe) | 222–250 ms | 32.7 ms | 37 composite + 3 layer_batch | 31.8 ms | 0.88 ms | ~178 ms |
| 16–191 (statici) | 0.1 ms | — | — | — | — | — |

Letto: sul GPU dominano le passate **composite** (31.8 dei 32.7 ms, 97%), ma
il GPU è solo il 13% del frame; il costo vero è il **CPU di dispatch** che
ricompone l'intero stack (37 passate, ~1.85 per layer, nessun effetto nel
piano) perché finché un frame differisce dal precedente il renderer non può
duplicarlo. Non esiste una passata "colpevole" da rendere più economica: il
costo è per frame eseguito (`graph_execute` 3945 ms su 16 frame = 246 ms/frame
contro 0.1 ms di un duplicato).

Riduzione applicata: `modernMapFillWipeFrames` 14 → 8 (0.33 s a 24 fps, la
spazzata resta fluida perché la feather da 120 px maschera il passo).

| Italia pin 192 frame | wipe 14 | wipe 8 |
|---|---|---|
| frame eseguiti | 16 | 10 |
| `graph_execute_ms` | 3945 | 2563 |
| `node_dispatch_ms` | 2843 | 1860 |
| `gpu_execute_ms` | 588 | 385 |
| render loop | 11.70 s | 9.68 s (−17%) |
| wall clock | 15.73 s | 13.82 s (−12%) |
| MP4 8 s | 480 954 B | 461 707 B |

Regressione visiva controllata: il fill è completo a frame 8 (frame 8 vs frame
100: solo rumore di compressione, 338 px oltre 8/255) e un frame della route
(frame 60) è **pixel-identico** al render precedente al livello fill+stroke.

## Residui

- `fill_path`/`fill_path_surface` (kind `4`, 3–8 vertici) restano in uso solo
  per i path senza stroke che arrivano da altri chiamanti; la promozione dei
  path usa `fill_polygon`.
- Il costo per frame animato è CPU-bound sul dispatch: una vera riduzione (non
  "meno frame") richiede ricomposizione parziale sul dirty rect o fusione delle
  passate composite nel graph compiler.
- Il collo di bottiglia del wall clock residuo è l'encoder (x264 `slow`,
  `submit_cpu_ms` 7.0 s su 192 frame, il 50% del render loop): un preset più
  veloce ridurrebbe il wall time più di qualsiasi altra modifica al piano, al
  costo di un MP4 più pesante.
