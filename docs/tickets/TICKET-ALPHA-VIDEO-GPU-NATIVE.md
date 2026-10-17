# TICKET-ALPHA-VIDEO-GPU-NATIVE

## Stato (misurato, 2026-10-17)

La certificazione T13 verifica il compositing GPU di **PNG alpha decodificati**
(still-image lane PASS) ma **non** la decodifica di un video alpha continuo su
GPU. Fatti misurati sul ramo corrente:

1. **qtrle è CPU-only.** Il source alpha MOV (qtrle, RGBA) prodotto da threepp
   viene rigettato dal decoder nativo strict in `require_gpu_native`; NVDEC non
   ha un profilo qtrle/RGBA. La via CPU-readback/CPU-encode non può certificare
   un handoff full-GPU (THREEPP_CERTIFICATION T13, paragrafo "animated alpha
   video").
2. **La via Vulkan image-layer fallisce alla materialization** della
   native-surface per i layer immagine animati (stessa evidenza, tentativo
   registrato).

## Rotta proposta: dual-stream NVDEC (colore + alpha mono)

NVDEC non decodifica RGBA; la rotta standard è decodificare **due stream
NVDEC** dello stesso timeline:

- **Stream colore**: il video principale (NV12/ certainly decodificato nativo,
  come oggi).
- **Stream alpha**: lo stesso timeline reso/lavorato come **monochrome 8-bit**
  nel contenitore (Y-only NV12: la Y porta l'alpha). NVDEC decodifica Y-only
  native; il plano delta resta device-local.

Poi in composizione, nel percorso full_graph_native:

1. Materializzare l'alpha riga NV12 come RGBA via kernel CUDA esistente
   (c'è già infrastruttura `cuda_nv12_kernels` e pos-profilo per NV12→RGBA).
2. Moltiplicare il canale colore per l'alpha **solo sul GPU** (nessun readback
   host): il kernel di composizione riceve entrambe le superfici native.

### Perché Y-only e non transparent-codec

- VP9 alpha (secondo stream WebM) richiederebbe un secondo demux + profilo
  libvpx (CPU lato alfa). Dual-stream mp4 mantiene tutto su NVDEC.
- L'encoding alpha (eventuale) resta fuori scope: il consumo è compositing GPU
  in ingresso, non il rebroadcast di alpha.

## Cambi previsto (fase per fase)

| Fase | Contenuto | Gate di accettazione |
|---|---|---|
| P1 schema | Campo `alpha_source` opzionale nel layer video del RenderPlan (pass al decoder) | schema v2 accetta + fail-closed su `alpha_source` nel test `render_plan_schema`, P01 decode lambdato |
| P2 decoder | `NativeVideoFrameDecoder` apre + decodifica la stream alpha mono3 (secondo hw session) | probe hwlab: due sessioni NVDEC parallele senza game hint, letture device-local fino alla composizione |
| P3 composizione | Kernel CUDA merge NV12(Y-alpha) × colore RGBA device-local | confrontingo pixel CPU-vs-GPU margine tirato implicitamente (1 lsb), zero readback in encode path |
| P4 e2e | T13 animated alpha rullata in `require_gpu_native` | 150/150 frame native, zero bytes conversion/staging, ROI alpha gate per frame, receipt `full_graph_vulkan`; qtrle rimane il rejection case negativo |

## Riferimenti

- `ChrononMotion3D/docs/THREEPP_CERTIFICATION.md` — T13 "animated alpha video"
  e il paragrafo composito/provenance.
- `docs/tickets/TICKET-KEYING-GPU-NATIVE.md` — semantica premultiplied.Alpha
  del software processor che il merge GPU deve evocare pari-pari.
- `src/media/video/compositor/cuda/cuda_nv12_kernels.hpp` — infrastruttura
  NV12/esclusione CUDA in ri-uso per il step 2.
