# Chronon3D — Improvement Roadmap

> Analisi basata sullo stato attuale del codebase (maggio 2026).
> Ordinate per impatto/valore — le prime voci danno più risultato con meno fatica.

---

## 🚨 IMMEDIATE — Da fare oggi (1-2 ore ciascuno)

### I1. Bake del grid_bg su EXR con mmap

**Problema:** Il background statico viene ricalcolato ogni sessione.
**Soluzione:** Renderizza una volta sola → salva come EXR tiled 256×256 (DWAA) → carica con `mmap` (zero copy).
**Dove:** `apps/chronon3d_cli/commands/command_bake_layer.cpp` (esiste già) + nuovo `GridBgCache` loader.
**Guadagno stimato:** 100-200ms risparmiati al primo frame.
**Comandi esistenti da sfruttare:**
```bash
./chronon3d_cli bake-layer --comp LilDirkClean --layer grid_bg --output /tmp/grid_bg.exr
```
**Prossimi passi:**
- [ ] Aggiungere `GridBgCache::load_mmap(path)` che fa `mmap + EXR tiled read`
- [ ] Modificare `DarkGridBackground::resolve()` per controllare se il file EXR esiste e usare quello
- [ ] Aggiungere flag `--baked-grid` a `command_video` che salta il render del grid se il cache è caldo

---

### I2. Huge Pages per il FramebufferPool

**Problema:** Allocazione con pagine da 4KB → TLB miss pesanti su Zen3/Threadripper.
**Soluzione:** `VirtualAlloc` con `MEM_LARGE_PAGES` (2MB) su Windows, `mmap(MAP_HUGETLB)` su Linux.
**Dove:** `src/cache/framebuffer_pool.cpp` + `include/chronon3d/cache/framebuffer_pool.hpp`.
**Guadagno stimato:** TLB miss -70%, throughput -5-10% su tutti i frame.
**Codice minimo:**
```cpp
// Dentro FramebufferPool::acquire_unique o nel costruttore
#ifdef _WIN32
    HANDLE hProcessToken;
    OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hProcessToken);
    LUID luid;
    LookupPrivilegeValue(NULL, SE_LARGE_PAGE_NAME, &luid);
    TOKEN_PRIVILEGES tp = {1, {luid, SE_PRIVILEGE_ENABLED}};
    AdjustTokenPrivileges(hProcessToken, FALSE, &tp, 0, NULL, 0);

    void* ptr = VirtualAlloc(NULL, bytes, MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
    // fallback a VirtualAlloc normale se fallisce
#else
    // Linux: mmap con MAP_HUGETLB | MAP_ANONYMOUS | MAP_PRIVATE
#endif
```
**Prossimi passi:**
- [ ] Aggiungere helper `try_alloc_large_pages(size_t bytes)` con fallback a page size normale
- [ ] Applicare a tutte le allocazioni del pool (canvas 1920×1080 @ 32 buffer ≈ 256MB)
- [ ] Loggare quando si cade nel fallback per diagnosticare il sistema

---

### I3. Dirty Rect Bitmask per il Compositing

**Problema:** Ogni frame rasterizza tutto anche se solo il testo cambia.
**Soluzione:** Griglia fissa di macro-regioni (64×36 per 4K = 2304 celle). Ogni nodo marca le celle che tocca. Compositing finale salta le non-marcate.
**Dove:** Nuovo file `include/chronon3d/core/dirty_rect_mask.hpp` + uso in `SoftwareRenderer::render_scene`.
**Guadagno stimato:** Se solo il testo cambia → rasterizzi ~1% dell'immagine invece del 100%.
**Struttura:**
```cpp
class DirtyRectMask {
    static constexpr int W = 64, H = 36;  // celle 64×64 per 4K
    std::array<std::uint64_t, W * H / 64> bits;  // 36 uint64 = 2304 bit
public:
    void mark(int cell_x, int cell_y) { bits[idx] |= mask; }
    void clear() { bits.fill(0); }
    bool is_dirty(int cell_x, int cell_y) const;
    // Iterator per scanare solo le celle dirty senza iterare tutta l'immagine
};
```
**Prossimi passi:**
- [ ] Definire `DirtyRectMask` con bitmask compact
- [ ] Integrare in `RenderNode::resolve()` — ogni nodo marca la propria bbox nella mask
- [ ] Nel compositing finale, saltare i quadrati non dirty
- [ ] Wire up in `render_pipeline.cpp` — la mask vive tra graph build e execute

---

### I4. Thread Affinity + NUMA per i Worker

**Problema:** I thread worker vagano liberamente tra core → cache L1/L2 non Hot, NUMA cross-socket.
**Soluzione:** `SetThreadAffinityMask` per pin su core fisici, `VirtualAllocExNuma` per allocare sul nodo locale.
**Dove:** `src/backends/software/software_renderer.cpp` + worker pool initialization.
**Guadagno stimato:** 5-10% throughput su sistemi multi-socket (Threadripper, Xeon).
**Codice minimo:**
```cpp
// Nel costruttore del worker pool
void pin_to_core(int core_id) {
    SetThreadAffinityMask(GetCurrentThread(), 1ULL << core_id);
}

// Nell'acquire del framebuffer pool
#ifdef _WIN32
    VirtualAllocExNuma(GetCurrentProcess(), NULL, bytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE, numa_node);
#else
    // numa_alloc_onnode su Linux
#endif
```
**Prossimi passi:**
- [ ] Determinare il numero di core fisici con `GetActiveProcessorCount`
- [ ] Allocare il framebuffer pool sul nodo NUMA 0 (o quello corrente)
- [ ] Pin each worker thread a un core fisico specifico
- [ ] Disabilitare hyperthreading per i task blur/composite (prefetches più puliti)

---

## ⚡ SHORT-TERM — Questa settimana (1-3 giorni ciascuno)

### S1. io_uring per la Pipe FFmpeg (Linux)

**Problema:** Scrittura classica su pipe → context switch ripetuti → backpressure.
**Soluzione:** `io_uring` con `IORING_OP_WRITE_FIXED` e buffer registrati — zero copy da RAM a socket.
**Dove:** `apps/chronon3d_cli/utils/ffmpeg_pipe_encoder.cpp`.
**Guadagno stimato:** -15-20ms di latenza per frame a 4K.
**Alternativa Windows:** Named pipe con `FILE_FLAG_NO_BUFFERING` + `WriteFileGather`.
**Prossimi passi:**
- [ ] Wrap `io_uring` in una classe `RingWriter` con setup/teardown
- [ ] Registrare i buffer YUV/KV12 come registered buffers
- [ ] Sostituire la `write()` loop attuale con `io_uring_enter(IORING_OP_WRITE_FIXED)`
- [ ] Fallback a `write()` normale se `io_uring` non è disponibile (kernel < 5.1)

---

### S2. Temporal Hashing — Cache Nodo con Storia

**Problema:** Un nodo viene ricalcolato anche se i suoi input non sono cambiati rispetto ai frame precedenti.
**Soluzione:** Hash del nodo (parametri + transform + time) + hash degli N frame precedenti. Se la sequenza è identica → reuse interpolato.
**Dove:** `include/chronon3d/render_graph.hpp` — nella classe base `RenderNode`.
**Guadagno stimato:** Per animazioni lineari (camera motion, opacity fade) → skip completo del render del nodo.
**Struttura:**
```cpp
struct NodeHash {
    size_t params_hash;      // hash dei parametri correnti
    size_t history_hash[3];  // hash dei 3 frame precedenti
};

// Nel RenderNode::resolve():
// se history_hash[0] == prev_frame_hash && history_hash[1] == prev_prev_hash
// → risultato già in cache, skip rendering
```
**Prossimi passi:**
- [ ] Aggiungere `NodeHash cache_key` a ogni RenderNode
- [ ] Implementare `TemporalCache` — mappa hash → framebuffer in pool
- [ ] Nel `resolve()`, controllare la cache prima di rasterizzare
- [ ] Invalutare la cache quando i parametri cambiano (hash mismatch)

---

### S3. Prefetch L1/L2 nei Loop di Composite

**Problema:** Il loop principale di blending ha cache miss perché accede a pixel sparsi.
**Soluzione:** `prefetcht0` ogni 4KB di pixel letti — la CPU carica in anticipo i dati nella cache L1.
**Dove:** `src/backends/software/simd/highway_kernels.cpp` — inside `composite_normal_premul` e simili.
**Guadagno stimato:** ~3-5% miglioramento IPC su Zen3.
**Codice minimo (da aggiungere inside the pixel loop):**
```cpp
for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
        // prefetch the next cache line (64 bytes = 16 pixels)
        if ((x % 16) == 0) {
            _mm_prefetch((const char*)&src_row[x + 16], _MM_HINT_T0);
        }
        // ... normal composite work ...
    }
}
```
**Prossimi passi:**
- [ ] Identificare i loop hot (composite, blur horizontal pass, color conversion)
- [ ] Aggiungere `_mm_prefetch` ogni 16 pixel in ogni loop hot
- [ ] Verificare con `perf stat` che i cache misses calano

---

### S4. OpenEXR con DWAA per Bake Statici

**Problema:** I bake vengono salvati come PNG 8-bit → perdita qualità, decompressione lenta.
**Soluzione:** EXR tiled 256×256 con compressione DWAA (loseless, molto più veloce di ZIP).
**Dove:** `apps/chronon3d_cli/commands/command_bake_layer.cpp` + `src/backends/image/exr_writer.cpp` (da creare).
**Guadagno stimato:** Bake 16-bit più veloci di PNG, mmap read parziale, nessuna perdita cromatica.
**Dipendenza:** Il progetto sembra avere OpenEXR già come deps (vcpkg.json → `openexr`).
**Prossimi passi:**
- [ ] Verificare che OpenEXR sia nel vcpkg.json
- [ ] Creare `exr_writer.cpp` con tiled writes (256×256 tile, DWAA)
- [ ] Modificare `command_bake_layer` per salvare in EXR invece di PNG di default
- [ ] Aggiungere `--exr-bake` flag

---

## 🏗️ MEDIUM-TERM — Questo mese (1-2 settimane ciascuno)

### M1. Frame Graph Compiler — Render Graph to Executable

**Problema:** Ogni nodo è una chiamata virtuale → 35+ virtual calls per frame. Il grafo è interpretato, non compilato.
**Soluzione:** A tempo di build della composition, genera una sequenza lineare di comandi C++ senza rami, senza map, senza virtual. Compila con `/O2 /arch:AVX2`.
**Dove:** Nuovo file `src/render_graph/graph_compiler.cpp` + `graph_compiler.hpp`.
**Guadagno stimato:** 30-40% speedup eliminando overhead di dispatch.
**Approccio step-by-step:**
1. Analizzare il grafo statico della composition (non cambia durante il render)
2. Determinare l'ordine di esecuzione dei nodi (topological sort)
3. Allocare i framebuffer in base al lifetime analysis (aliasing dei pool)
4. Generare codice C++ come stringa → scrivere su file `.cpp`
5. Chiamare il compilatore di sistema (`cl.exe` / `g++ -O2 -march=native`) per generare una `.dll` / `.so`
6. Caricare la `.dll` con `dlopen` / `LoadLibrary` e callare il punto d'ingresso

```cpp
// Output del compiler — un esempio per LilDirkClean:
extern “C” void render_LilDirkClean_Frame(
    float* pool_ptr,    // arena allocata dal pool
    int frame_idx,
    float time_seconds,
    uint32_t* output_rgba) 
{
    // Sequenza lineare — ZERO chiamate virtuali
    auto& grid_fb = *(Framebuffer*)pool_ptr; pool_ptr += GRID_SZ;
    auto& text_fb = *(Framebuffer*)pool_ptr; pool_ptr += TEXT_SZ;
    
    render_grid_bg(grid_fb, time_seconds);   // inline
    apply_blur(grid_fb, 2.0f);               // inline  
    composite_normal_premul(text_fb, grid_fb, output_rgba);  // inline SIMD
}
```

**Prossimi passi:**
- [ ] Definire l'IR del compiler (Instruction base + nodi concreti)
- [ ] Implementare `LifetimeAnalysis` — calcola quando ogni FB viene allocato/deallocato
- [ ] Generare codice C++ da una composition (es. LilDirkClean → `render_lildirk.cpp`)
- [ ] Integrare `system()` call per compilare all'avvio se il file non esiste
- [ ] LoadLibrary + function pointer call nel render loop

---

### M2. ISPC per il Blur Gaussiano

**Problema:** L'attuale `apply_blur` è box-filter sequenziale (scalar) — processa 1 pixel per istruzione.
**Soluzione:** Riscrivere in ISPC → 8 pixel per istruzione AVX2, 16 per AVX-512.
**Dove:** `src/backends/software/utils/effects/effect_blur.ispc` (nuovo).
**Guadagno stimato:** ~3.5x rispetto a C++ scalar su Zen4.
**Nota:** Highway è già presente nel progetto. Ma Highway non elimina i branch e il masking ai bordi — ISPC lo fa meglio per kernel complessi.
**Prossimi passi:**
- [ ] Installare ISPC (Intel ISPC compiler)
- [ ] Scrivere `blur.ispc` — kernel separable Gaussian con masking automatico dei bordi
- [ ] Aggiungere al CMake: `find_package(ISPC)` + compilazione del target `.ispc`
- [ ] Wrappare il kernel ISPC in una funzione C++ callable da `effect_blur.cpp`

---

### M3. SPSC Lock-Free Queue per la Pipe FFmpeg

**Problema:** Il writer thread e il render thread si sincronizzano con mutex + condition variable → context switch costosi.
**Soluzione:** Circular buffer SPSC (Single Producer Single Consumer) lock-free — zero mutex, zero syscalls.
**Dove:** Nuovo file `include/chronon3d/core/spsc_queue.hpp` + uso in `FfmpegPipeEncoder`.
**Guadagno stimato:** Elimina la latenza di sincronizzazione, il render thread non aspetta mai il writer.
**Struttura:**
```cpp
template<typename T>
class SpscQueue {
    static constexpr size_t CAPACITY = 64;  // chunk di 64 frame
    std::atomic<int> head{0}, tail{0};
    T buffer[CAPACITY];  // allineato a cache line
public:
    bool push(T&& item);   // solo producer chiama
    bool pop(T& item);     // solo consumer chiama
    // Nessun mutex — solo atomic head/tail
};
```
**Prossimi passi:**
- [ ] Implementare `SpscQueue` con atomic e cache line alignment
- [ ] Sostituire il `deque<shared_ptr<Framebuffer>>` + mutex con la coda lock-free
- [ ] Il render thread fa `push()` → il writer thread fa `pop()` → connect a FFmpeg stdin
- [ ] Backpressure: se la coda è piena, il render thread aspetta (ma senza syscalls)

---

### M4. Render Speculativo — Multi-Frame Ahead

**Problema:** Solo il frame N viene renderizzato → i core N+1..N+16 restano idle.
**Soluzione:** Mentre il main thread renderizza frame N, 15 worker thread renderizzano N+1..N+15 in anticipo.
**Dove:** Nuovo file `src/runtime/speculative_render.cpp` + modifica a `render_pipeline.cpp`.
**Guadagno stimato:** Se 16 core → 16x frame throughput teorico (limite: dependencies).
**Nota:** Funziona solo se il composition graph è deterministic e fully resolved in anticipo.
**Prossimi passi:**
- [ ] Implementare `FrameDependencyGraph` — sa già quali frame servono per il rendering
- [ ] Worker pool con 16 thread dedicati a frame indipendenti
- [ ] Il main thread aspetta il frame N dal worker pool, non lo renderizza
- [ ] Invalidazione: se l'animazione cambia traiettoria, butta via i frame speculativi e ricomincia

---

## 🌀 LONG-TERM — Mesi (grandi architetture)

### L1. GPU Backend per EffectStack (Vulkan Compute)

**Problema:** I filtri (blur, glow, color grading) sono CPU-bound → 10-20x più lenti della GPU.
**Soluzione:** Mandare EffectStack e Composite su Vulkan compute. Zero-copy con memoria condivisa. Il resto resta CPU.
**Dove:** Nuovo target `chronon3d_backend_vulkan` + `src/backends/vulkan/compute_effects.cpp`.
**Guadagno stimato:** Blur gaussiano → 10-20x più veloce. Glow → 15x.
**Note:** Questo trasforma Chronon3D da pure-CPU a hybrid CPU/GPU. È il cambio architetturale più grosso.
**Sotto-passi:**
- [ ] InizializzareVkInstance +VkDevice con VK_KHR_external_memory +
- [ ] Allocare VKBuffer condiviso tra CPU e GPU (VK_KHR_dedicated_allocation)
- [ ] Compilare compute shader per Gaussian blur (usa un已有的 HLSL/GLSL)
- [ ] Bind del buffer → dispatch → readback del risultato
- [ ] Fallback: se GPU non disponibile, usa la path CPU attuale

---

### L2. ECS Architecture — Entity Component System

**Problema:** Gli oggetti sono ancora orientati agli oggetti (AoS) → cache misses quando si itera su tutti i layer.
**Soluzione:** Riscrivere il layer system come ECS — Entities = IDs, Components = dati puri, Systems = funzioni batch.
**Dove:** Nuovo file `include/chronon3d/ecs/entity.hpp` + riscrittura di `src/scene/layer.cpp`.
**Guadagno stimato:** Pipeline di update dei layer → ~2-3x più veloce, meno RAM bandwidth.
**Prossimi passi:**
- [ ] Scegliere EnTT (già dipendenza) come ECS framework
- [ ] Definire i componenti: `Position`, `Transform`, `Opacity`, `EffectStack`
- [ ] I sistemi: `AnimationSystem`, `CullingSystem`, `RenderNodeSystem`
- [ ] Riscrivere `Scene::evaluate()` per usare l'ECS invece dei loop OOP

---

### L3. Frame Graph con Resource Barriers (RDG-style)

**Problema:** Non c'è un'analisi formale delle dipendenze read/write tra i pass → memcpy e allocazioni ridondanti.
**Soluzione:** Ogni pass dichiara esplicitamente read/write su buffer. Il builder inserisce automaticamente aliasing + prune.
**Dove:** `src/render_graph/graph_builder.cpp` + `include/chronon3d/render_graph/rdg.hpp` (nuovo).
**Pattern:** Come Frostbite Engine e Unreal Engine 5 Render Graph.
**Guadagno stimato:** -30% memcpy, pool preallocato staticamente per frame.
**Struttura:
```cpp
// Ogni pass dichiara:
Pass* add_pass(“blur_horizontal”, read: {src_fb}, write: {tmp_fb});
Pass* add_pass(“blur_vertical”,   read: {tmp_fb}, write: {dst_fb});

// Il builder:
// 1. Analizza i lifetime — tmp_fb non serve dopo frame 3
// 2. Aliasing — tmp_fb e grid_fb non si sovrappongono mai → stesso puntatore
// 3. Prune — se opacity=0, skip il pass completamente
```
**Prossimi passi:**
- [ ] Definire `RenderPass` con `{name, inputs[], outputs[], barrier_type}`
- [ ] Implementare `TransientResourceAnalysis` — calcola l'footprint max per frame
- [ ] Integrare nel graph builder esistente — ogni nodo diventa un pass
- [ ] Automatic buffer aliasing quando i lifetime non si sovrappongono

---

### L4. Persistent Daemon Mode — Hot Server

**Problema:** Ogni run paga il tempo di startup (init, warmup, alloc).
**Soluzione:** Tenere Chronon3D come daemon in ascolto su una socket. Zero startup time.
**Dove:** Nuovo target `chronon3d_daemon` + IPC via Unix socket o TCP localhost.
**Guadagno stimato:** Il primo frame è già caldo → 0ms di startup.
**Protocollo:
```json
// Client → Daemon
{ “cmd”: “render”, “comp”: “LilDirkClean”, “frames”: [0, 900], “output”: “/tmp/out.mp4” }

// Daemon → Client
{ “status”: “rendering”, “progress”: 0.45, “fps”: 12.3 }
```
**Prossimi passi:**
- [ ] Creare `daemon.cpp` con main loop che accetta connessioni socket
- [ ] Definire JSON protocollo (o usare FlatBuffers per zero-parse)
- [ ] Il daemon mantiene il renderer warm e il pool hot permanentemente
- [ ] `chronon3d_cli render ...` diventa un client leggero che parla col daemon

---

### L5. Render Farm Distribuito — Multi-Host

**Problema:** Una macchina sola → limitata ai suoi core.
**Soluzione:** Spezzare la timeline in segmenti, distribuirli su più macchine, merge finale.
**Dove:** Nuovo file `src/runtime/distributed_scheduler.cpp` + protocollo di rete.
**Guadagno stimato:** Scalabilità lineare — 4 macchine = 4x throughput.
**Architettura:**
```
Host A: frames 0-225   → /tmp/seg0.mp4
Host B: frames 225-450 → /tmp/seg1.mp4
Host C: frames 450-675 → /tmp/seg2.mp4
Host D: frames 675-900 → /tmp/seg3.mp4

Merge: ffmpeg -f concat -i list.txt -c copy output.mp4
```
**Prossimi passi:**
- [ ] Implementare `TimelinePartitioner` — divide in N segmenti uguali
- [ ] Creare `render_slave` che riceve un range e renderizza
- [ ] RPC layer (gRPC o HTTP REST) per orchestrare i slave
- [ ] `render_master` coordina e fa merge finale

---

## 📋 RIEPILOGO MATRICE

| ID | Improvement | Quando | Complessità | Impatto | Stato |
|----|------------|--------|-------------|---------|-------|
| I1 | Bake grid EXR + mmap | Oggi | 🟢 Bassa | 🔴 Alto | Da fare |
| I2 | Huge pages | Oggi | 🟢 Bassa | 🔴 Alto | Da fare |
| I3 | Dirty rect bitmask | Oggi | 🟢 Bassa | 🔴 Alto | Da fare |
| I4 | Thread affinity + NUMA | Questa settimana | 🟢 Bassa | 🟡 Medio | Da fare |
| S1 | io_uring pipe | Questa settimana | 🟡 Media | 🟡 Medio | Da fare |
| S2 | Temporal hashing | Questa settimana | 🟡 Media | 🔴 Alto | Da fare |
| S3 | L1/L2 prefetch | Questa settimana | 🟢 Bassa | 🟡 Medio | Da fare |
| S4 | OpenEXR DWAA bake | Questa settimana | 🟡 Media | 🟡 Medio | Da fare |
| M1 | Graph compiler | Questo mese | 🔴 Alta | 🔴 Alto | Da fare |
| M2 | ISPC blur | Questo mese | 🔴 Alta | 🟡 Medio | Da fare |
| M3 | SPSC lock-free queue | Questo mese | 🟡 Media | 🟡 Medio | Da fare |
| M4 | Speculative render | Questo mese | 🔴 Alta | 🔴 Alto | Da fare |
| L1 | GPU Vulkan compute | Mesi | ⚫ Molto Alta | 🔴 Alto | Da fare |
| L2 | ECS architecture | Mesi | 🔴 Alta | 🟡 Medio | Da fare |
| L3 | Frame Graph RDG | Mesi | 🔴 Alta | 🔴 Alto | Da fare |
| L4 | Persistent daemon | Mesi | 🟡 Media | 🔴 Alto | Da fare |
| L5 | Distributed render farm | Mesi | ⚫ Molto Alta | 🔴 Alto | Da fare |

---

## 🎯 Priorità Raccomandata

Se dovessi scegliere **una sola cosa** da implementare oggi: **I1 (Bake grid EXR)**.
Se potessi implementare **3 cose** questa settimana: **I1 + I2 + I3** insieme.
Il guadagno combinato è ~25-35% faster overall, principalmente sul primo frame.

La più divertente a lungo termine: **M1 (Graph Compiler)** — è come passare da una ricetta letta ogni volta a un robot che sa già tutti i movimenti a memoria.