# TICKET-VIDEO-COMPILER-ARCH-V1 — Video-compiler architecture (CompiledTemplateProgram → DeviceProgram → hot loop)

## Stato: OPEN — Fase A DONE (2026-08-22), fasi B–M PLANNED

Architettura "video compiler offline": il grosso del lavoro intellettuale avviene
prima del primo frame (compilazione) e il runtime si riduce a far scorrere la
pipeline hardware con quasi-zero CPU overhead per frame.

Principio guida: **un'unica architettura**, non cinque ottimizzazioni indipendenti.
`CompiledFrameProgram` (operations + static bake + layer batches) deve diventare
gradualmente il vero "eseguibile" della scena invece di un secondo renderer.

## Architettura finale (forma obiettivo)

```text
Scene / Template
       │
       ▼
SceneIR
       │
       ├── temporal analysis
       ├── static analysis
       ├── ROI analysis
       ├── resource lifetime
       ├── pixel-format inference
       ├── primitive lowering
       ├── fusion
       └── backend selection
       │
       ▼
CompiledTemplateProgram
       │  bind job assets/strings/videos
       ▼
PreparedJobProgram
       │  specialize for actual GPU
       ▼
DeviceProgram
       │
       ├── persistent resources
       ├── baked surfaces
       ├── descriptor/resource table
       ├── parameter ring
       ├── Vulkan command programs
       └── CUDA Graphs
       │
       ▼
HOT LOOP
write params → replay → encode
```

## Tre livelli separati (no handle hardware nel file compilato)

Non serializzare `VkImage`, `VkPipeline`, `cudaGraphExec_t`: sono oggetti
process/device-specific e non appartengono al `.c3dprog`.

```cpp
struct CompiledTemplateProgram {
    ProgramFingerprint fingerprint;
    ParameterSchema parameters;
    ResourceManifest resources;
    std::vector<StaticBakeRegion> static_regions;
    std::vector<CompiledGpuBatch> batches;
    std::vector<MaterializationBoundary> boundaries;
    PhysicalResourcePlan resource_plan;
    ExecutionSchedule execution;
};

struct PreparedJobProgram {
    const CompiledTemplateProgram* program;
    JobResourceBindings resources;
    JobParameterBlock initial_parameters;
    BakedResourceSet baked_resources;
};

struct DeviceProgram {
    BackendKind backend;
    DeviceResourceTable resources;
    FrameSlotRing frame_slots;
    BackendExecutionProgram execution;
};
```

Il template (es. "BREAKING NEWS TEMPLATE") si compila una volta; Job A / Job B
(text/image diversi) usano **lo stesso programma** con bindings diversi.

### Pipeline di compilazione

```text
Raw RenderGraph
      ↓ Canonicalization
      ↓ Temporal Analysis
      ↓ Dead Node Elimination
      ↓ Identity Elimination
      ↓ Constant Folding
      ↓ Bounding/ROI Analysis
      ↓ Static Island Discovery
      ↓ Materialization Analysis
      ↓ Primitive Lowering
      ↓ Pixel Format Inference
      ↓ Lifetime Analysis
      ↓ Physical Slot Allocation
      ↓ GPU Fusion / Layer Batching
      ↓ Backend Selection
      ↓ Execution Scheduling
      ↓ CompiledTemplateProgram
```

Il RenderGraph originale diventa il **source language** del compiler, non ciò che
eseguiamo.

## Fasi di implementazione (sequenza vincolante)

| Fase | Implementazione | Risultato |
|------|-----------------|-----------|
| **A** | `CompiledTemplateProgram` + parameter/resource schema | RenderGraph diventa AST |
| **B** | Temporal Analysis + maximal static islands | elimina lavoro statico |
| **C** | PhysicalResourcePlan + preflight VRAM | zero churn / OOM controllati |
| **D** | Persistent FrameSlot parameter ring | pochi byte/frame |
| **E** | Vulkan command replay | CPU orchestration minima |
| **F** | GpuLayerBatch universale | immagini/testo senza intermedi |
| **G** | PixelProgram IR + fusion | elimina pass GPU intermedi |
| **H** | Persistent daemon + template cache | cold-start quasi nullo |
| **I** | PixelDomain inference | evita conversioni inutili |
| **J** | NV12 Maximum Attack backend | NVDEC → composite → NVENC |
| **K** | CUDA Graph replay | launch overhead minimo |
| **L** | Macro-ROI | non renderizzare pixel invariati |
| **M** | Multi-frame waves/autotuning | saturazione hardware finale |

La parte **A–G** è il prerequisito: senza quella base, YUV-first e CUDA Graphs
rischiano di diventare fast-path speciali; con quella base diventano **due
lowering diversi dello stesso programma compilato**.

## Dettagli per fase (contract del ticket)

### A. CompiledTemplateProgram — DONE (Fase A first commit, 2026-08-22)
`ProgramFingerprint` + `ParameterSchema` + `ResourceManifest` + `StaticBakeRegion`
list + `CompiledGpuBatch` (alias di `CompiledLayerBatch` in Fase A) +
`MaterializationBoundary` list + accessor `resource_plan()` / `execution()`
verso il compiled graph. RenderGraph = source language.

**Implementazione Fase A (first commit):**
- `include/chronon3d/render_graph/compiler/compiled_template_program.hpp` (NEW):
  ABI surface — `ProgramFingerprint` (+ `std::hash`) con `kRenderAbiV1` /
  `kQualityProfileDefault`, `ParameterSchemaEntry` / `ParameterSchema`,
  `ResourceKind` / `ResourceManifestEntry` / `ResourceManifest`,
  `StaticBakeRegion`, `CompiledGpuBatch` (alias), `MaterializationBoundaryKind` /
  `MaterializationBoundary`, `CompiledTemplateProgram` (shared_ptr<const
  CompiledFrameGraph> = single source of truth + metadata + accessors).
- `src/render_graph/compiler/compiled_template_program.cpp` (NEW):
  `compile_template_program(CompiledFrameGraph)` — move nel shared_ptr, poi
  lift di fingerprint (structure_hash + kRenderAbiV1), parameter schema (da
  operations con parameter_size>0 + prepared table frame_count),
  resource manifest (solo binding_meta.active, classify Image=shape_type 7 /
  Text=TextRun / Video=Video), static regions (da static_bakes),
  batches (da layer_batches), boundaries vuoti (Fase G).
- `src/render_graph/CMakeLists.txt` (EDIT): `compiled_template_program.cpp` in
  `chronon3d_graph_compiler` OBJECT lib.
- `tests/render_graph/compiler/test_template_program.cpp` (NEW): 7 TEST_CASE —
  ABI default-empty, fingerprint equality+hash, lift fingerprint, parameter
  schema lift, resource manifest classify, static regions+batches lift,
  move semantics.
- `tests/render_graph/compiler/template_program_tests.cmake` (NEW) +
  `tests/manifests/test_definitions.cmake` (EDIT): suite UNIT unconditional.

**Verifica:** build `chronon3d_template_program_tests` PASS + ctest
7/7 TEST_CASE PASS, 59/59 assertions. Cat-3 minimal-surface: 1 NEW ABI header
+ 1 NEW impl + 1 NEW test + 1 NEW .cmake + 2 EDIT = 6 file; zero duplicazione
delle strutture esistenti (compose via shared_ptr, non copia i vector di
`CompiledFrameGraph`). Forward-point: macchina-verifica end-to-end su
composizione reale (tramite frame-graph compiler) DEFERRED-WBH.

### B. Temporal Analysis formalizzata
```cpp
enum class TemporalClass : uint8_t {
    Static, TransformDynamic, ParameterDynamic, ContentDynamic, ExternalDynamic
};
struct TemporalCapabilities {
    bool content_depends_on_time;
    bool transform_depends_on_time;
    bool parameters_depend_on_time;
    bool depends_on_external_frame;
};
```
Propagazione topologica una volta in compilazione: `own state static AND all
inputs static → node static`. (Precedente esistente: `TemporalClass{Pure,
Stateful, TimeDependent}` in `include/chronon3d/render_graph/core/cache_policy.hpp`
— riusare/estendere, non duplicare.)

### C. Maximal Static Island Baking
Bake della più grande regione statica possibile (image+text+composite+transform →
`BakedResource #17`). `StaticBakeRegion { BakeResourceId id; GraphNodeId root;
std::vector<GraphNodeId> members; ResourceFingerprint fingerprint; SurfaceDesc
output_desc; }` — il compiler conosce solo `BakeResourceId`, il runtime risolve.

### D. Prepare come fase vera
`COMPILE → PREPARE → HOT RENDER`. In prepare: resolve fonts, shape static text,
raster missing glyph, populate global atlas, decode/upload static assets, allocate
physical surfaces, create descriptors, instantiate pipelines, bake static islands,
prepare frame parameter ring, record/replay programs, instantiate CUDA Graphs,
warm NVDEC/NVENC. Solo dopo, `FRAME 0` fa parte del render misurato.

### E. PhysicalResourcePlan
Lifetime analysis + interval coloring/aliasing: logici non sovrapposti condividono
lo stesso slot fisico. `PhysicalGpuSlot { SurfaceDesc; VkImage; VkImageView;
VulkanAllocation; DescriptorResourceId; }` creati **prima** del rendering →
`vkCreateImage/ImageView/AllocateMemory/descriptor per frame = 0`.

### F. Admission control VRAM
```cpp
struct DeviceMemoryPlan {
    uint64_t persistent_assets, baked_surfaces, physical_slots, frame_slot_buffers;
    uint64_t decoder_budget, encoder_budget, atlas_budget, scratch_budget;
    uint64_t safety_margin, estimated_peak;
};
if (estimated_peak > available_budget) reject_or_degrade_job();
```
Output diagnostico "GPU VRAM available / predicted peak / ADMITTED". Fondamentale
per la concurrency del daemon. **Non promettere "OOM impossibile"** (driver e
decoder/encoder possono comunque fallire).

### G. FrameParameterTable — unica cosa che cambia per frame
Programma statico possiede texture handles/atlas/shader/pipeline/batch
topology/resource graph; il frame cambia solo position/opacity/scale/rotation/
effect/timeline/source frame. `FrameParameterBlock` SoA/fixed-offset compilato:
`parameter_offset` + `parameter_size` per parametro (già previsto in
`CompiledOperation`).

### H. Command replay
- **Vulkan**: FrameSlot ring (0/1/2), ogni slot = command program con offset/buffer
  fissi alla registrazione. `slot = ring.acquire(); write_params(slot..., frame);
  submit(slot.command_buffer);` — nessuna modifica del command buffer per frame.
- **CUDA Graphs**: `CudaExecutionSlot { void* host_parameter_block; void*
  device_parameter_block; cudaGraphExec_t graph_exec; }`; graph contiene già il
  memcpy params o legge host-visible. Evitare `cudaGraphExecUpdate()` per frame;
  mantenerlo solo come fallback per cambi di indirizzi/parametri strutturali.

### I. Frame loop obiettivo
```cpp
RenderResult render_frame(FrameIndex frame) {
    auto& slot = m_slots.acquire();
    m_timeline.evaluate_into(frame, slot.parameter_block);
    m_device_program.launch(slot);
    m_encoder.enqueue(slot.output);
    return {};
}
```
Dopo timeline-on-GPU: `slot.header->frame_index = frame; m_device_program.launch(...)`.

### J. YUV-first / PixelDomain inference
`enum class PixelDomain { Coverage, Yuv420, Rgba8, LinearRgba16F, LinearRgba32F }`.
Ogni operation dichiara `PixelDomainCapabilities { DomainMask accepted_inputs;
DomainMask supported_outputs; bool preserves_domain; }`. `NVDEC NV12 → crop →
overlay → NVENC` senza conversioni; solo le regioni che richiedono RGB materializzano.

### K. Native NV12 surface + compositing 2×2
```cpp
struct NativeVideoSurface {
    SurfaceId id; uint32_t width, height;
    PlaneResource y, uv;
    VideoColorSpace color_space;   // BT.601 / BT.709 / BT.2020
    ColorRange range; TransferFunction transfer; ChromaLocation chroma_location;
    ExternalMemoryHandle external_memory;
};
```
Kernel 2×2 (4 luma + 1 chroma): calcolare la coverage/contribuzione dei 4 pixel
PRIMA di aggiornare UV (4:2:0 condivide il chroma; un downsample sbagliato produce
bordi colorati nei sottotitoli sottili). `ColorMathRequirement { EncodedSafe,
LinearRgbRequired }`: se LinearRgbRequired il compiler inserisce
`YUV → linear RGB → effect → linear RGB → YUV`.

### L. GpuLayerBatch universale (stesso IR, backend diverso)
```cpp
struct LayerInstance {
    PrimitiveKind kind; ResourceIndex resource;
    TransformIndex transform; PaintIndex paint;
    Rect source; Rect destination; BlendMode blend;
};
```
Vulkan/RGB → instanced raster/compute su RGBA; NVIDIA → fused CUDA kernel su NV12.

### M. PixelProgram IR + fusion con boundaries matematiche
`enum class PixelOpcode { SampleTexture, SampleMask, AffineTransform,
MultiplyOpacity, Premultiply, Unpremultiply, ColorMatrix, Gamma, Lut, Mask,
SourceOver, Store }` + `PixelInstruction { opcode; ValueId input0, input1;
ParameterRef params; }`. `ProcessorCapabilities` esteso: `gpu, fusible,
pixel_local, neighborhood_local, requires_materialization, halo_radius, domains`.
Maximal fusible regions (come le static islands). **Attenzione register pressure**:
cost model semplice (max operations per fused program / max texture samples / max
neighborhood radius), poi tuning con Perfetto/benchmark. Cache kernel
specializzati (`.c3dcache/kernel/*.spv|*.cubin`, key `KernelVariantKey`).

### N. Daemon persistente + template cache
`ChrononDaemon { DeviceRuntime GPU0 (Vulkan/CUDA context, shader/kernel cache,
global glyph atlas, texture residency cache, decoder/encoder session pool,
program cache), JobScheduler }`. Il job NON crea VkDevice/CUDA context/cache/
atlas/NVENC runtime — tutto già vivo. `TemplateProgramKey { Hash128 topology_hash;
RenderAbiVersion renderer_abi; QualityProfile quality; }` (niente headline/contenuti
= bindings). `ParameterImpact { RuntimeOnly, ResourceBinding,
ProgramSpecialization, TopologyChange }` per evitare invalidazioni inutili.
`ResidencyBudget { textures, glyph_atlas, baked_templates, compiled_programs }`
LRU/LFU con pinned residency per job in esecuzione. Scheduler memory-aware:
`if (active_peak + candidate_peak <= render_budget) admit; else queue;`.

### O. Portable vs Maximum Attack — un solo compiler
Condividono SceneIR / Temporal analysis / Static islands / Resource plan /
GpuLayerBatch / PixelProgram IR / FrameParameterSchema; poi
`BackendCompiler::compile(CompiledTemplateProgram, DeviceCapabilities) → DeviceProgram`
(Vulkan Lowering | NVIDIA Lowering). Capability resolver unico:
`DeviceCapabilities { vulkan, cuda, nvdec, nvenc, descriptor_indexing,
descriptor_buffer, cuda_graphs, external_memory, external_semaphore, nv12_direct,
vram_budget }` + `choose_execution_plan(...)`. Descriptor/resource table
permanente (non-uniform indexing quando disponibile).

### P. Dirty Macro-ROI (come conseguenza del compiler)
`DynamicBounds { BBox maximum_extent; BBox current_extent; }` propagati; il
compiler conosce quali layer si muovono dove → dirty macro region. Se
`dirty_area / frame_area < threshold` (25–35% empirico da misurare) → ROI
execution, altrimenti full-frame.

### Q. Multi-frame waves (SOLO dopo single-frame path perfetta)
Prima `1 frame → quasi zero CPU overhead`; poi misurare occupancy; se GPU
sottoutilizzata `FrameBatch = 4/8/16` via `grid.z = frame_count` o scheduler
pipelined con resources indipendenti.

## Metrica finale obiettivo

```text
CPU FRAME WORK
graph traversal 0 / processor lookup 0 / dynamic allocation 0 /
shader selection 0 / descriptor allocation 0 / text shaping 0 /
glyph raster 0 / surface creation 0
parameter bytes written ~1–20 KB / GPU program launches 1/few

GPU: static work skipped, static islands reused, dynamic layers batched,
pixel-local effects fused, unneeded pixels culled, host readback 0

VIDEO: NVDEC frame N+2 / render frame N+1 / NVENC frame N
```

## Criteri di accettazione (per chiusura)

- [ ] Fase A–G (A–G è il prerequisito architetturale) implementate e macchina-verificate
- [ ] `vkCreate*/vkAllocateMemory/descriptor allocation per frame = 0` nel steady state
- [ ] Frame loop obiettivo: `acquire → evaluate_into → launch → enqueue`
- [ ] Compiler a due lowering (Vulkan | NVIDIA) sullo stesso CompiledTemplateProgram
- [ ] Preflight VRAM con `reject_or_degrade_job()` + diagnostica ADMITTED/REJECTED
- [ ] Template cache topologica: Job A e Job B con bindings diversi condividono il programma
- [ ] Metrica finale: parametri scritti 1–20 KB, host readback 0, NVDEC/render/NVENC pipelined

## Forward-points (azioni successive, atomiche su main, no branch)

Ogni fase (A–M) è un follow-up separato con commit atomici su `main` (regola
NO BRANCHES ONLY MAIN). L'ordine è vincolante: A → B → C → D → E → F → G → H → I →
J → K → L → M. Ogni fase DEVE essere un lavoro autonomo (scheda/commit dedicati)
prima di aprire la successiva.

## Cross-link canonici

- `docs/ROADMAP.md` — riga sintetica "Video Compiler Architecture (CompiledTemplateProgram)" (PLANNED, questo commit)
- `docs/FOLLOWUP_TICKETS.md` — row Non-Blocking Backlog / P2 con link a questo ticket
- `include/chronon3d/render_graph/core/cache_policy.hpp` — precedente `TemporalClass{Pure, Stateful, TimeDependent}` da riusare/estendere in Fase B (non duplicare)
- AGENTS.md §regole — no nuovi singleton/registry/resolver/cache senza ADR; no `#include <msdfgen>/<libtess2>/<unicode[/...]>`; Cat-3 minimal-surface; "Fare PR piccole e mirate"
