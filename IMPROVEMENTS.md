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
**Note dal codice:** Il sistema di tracking esiste già (`dirty_rect_count`, `dirty_pixels`, `dirty_full_fallbacks`, `dirty_full_fallback_reasons` in `counters.hpp`), ma vengono solo incrementati come contatori — non vengono usati per fare skip regioni. La `DirtyFallbackReason` enum ha solo 4 valori, expandable.
**Prossimi passi:**
- [ ] Definire `DirtyRectMask` con bitmask compact
- [ ] Integrare in `RenderNode::resolve()` — ogni nodo marca la propria bbox nella mask
- [ ] Nel compositing finale, saltare i quadrati non dirty
- [ ] Wire up in `render_pipeline.cpp` — la mask vive tra graph build e execute

---

### I4. Thread Affinity per TBB Arena + NUMA

**Problema:** I thread TBB (`tbb::parallel_for` in `graph_executor.cpp`) vagano liberamente tra core → cache L1/L2 non hot, NUMA cross-socket.
**Soluzione:** `GraphExecutor` ha già un `m_arena` (task_arena), ma non setta affinity. Pin dei thread ai core fisici.
**Dove:** `src/render_graph/graph_executor.cpp` — costruttore `GraphExecutor()` e `m_arena.execute()`.
**Guadagno stimato:** 5-10% throughput su sistemi multi-socket (Threadripper, Xeon).
**Codice minimo:**
```cpp
// Nel costruttore di GraphExecutor
GraphExecutor::GraphExecutor()
    : m_arena(std::max(1u, std::thread::hardware_concurrency())) 
{
    // Su Windows: SetThreadAffinityMask per ogni thread hardware
    // Su Linux: sched_setaffinity
    // L'arena TBB può essere affinitized al costruttore
}
```
**Prossimi passi:**
- [ ] Aggiungere `pin_current_thread_to_core(core_id)` in un helper
- [ ] Nel `execute()` di GraphExecutor, ogni thread worker chiama `SetThreadAffinityMask`
- [ ] Allocare il framebuffer pool sul nodo NUMA locale con `VirtualAllocExNuma`
- [ ] Disabilitare hyperthreading per i task blur/composite (prefetches più puliti)

---

### I5. Usa FrameArena nel Render Pipeline

**Problema:** `FrameArena` esiste (`include/chronon3d/core/arena.hpp`) ma è usato solo in `FrameContext::memory`. Non è sfruttato per le allocazioni temporanee nel render loop.
**Soluzione:** Il `GraphExecutor` alloca i vettori temporanei (inputs, input_bboxes, resolved_*) sulla heap ogni frame. Dovrebbero usare la `FrameArena`.
**Dove:** `src/render_graph/graph_executor.cpp` — la lambda dentro `execute()` e i `PreResolvedNode` vectors.
**Guadagno stimato:** Eliminare malloc/free nel hot path — ~1-2% speedup.
**Codice minimo:**
```cpp
// Dentro execute(), passare m_arena.resource() ai vettori temporanei
FrameArena frame_arena;
std::vector<PreResolvedNode, FrameArena::vector<PreResolvedNode>> level_resolved(frame_arena.resource());
// Tutti i std::vector dentro execute() che non servono dopo il frame → usano arena
```
**Prossimi passi:**
- [ ] Passare `FrameArena&` dentro `execute()` 
- [ ] Cambiare i `std::vector` temporanei in `FrameArena::vector`
- [ ] Alla fine del frame: `frame_arena.reset()` invece di deallocare

---

### I6. Disk Cache per Nodi Statici (PersistentDisk Lifetime)

**Problema:** I nodi con `CacheLifetime::PersistentDisk` (già definito in `cache_policy.hpp`) non hanno implementazione. Il campo `disk_cacheable` è sempre `false`.
**Soluzione:** Implementare un layer di cache su disco per nodi statici come `grid_bg` — hashed su params+input, salvato come file binario.
**Dove:** Nuovo file `src/cache/disk_node_cache.cpp` + modifica a `graph_executor.cpp`.
**Guadagno stimato:** Skip rendering completo di nodi statici tra sessioni.
**Struttura esistente da sfruttare:**
```cpp
// In cache_policy.hpp già esiste:
enum class CacheLifetime { PerFrame, PerComposition, PersistentDisk };

// Il RenderNodeCachePolicy ha già:
bool disk_cacheable{false};  // mai usato, sempre false

// Servono:
// 1. Hash della key → path su disco
// 2. Formato binario (raw RGBA floats o compressed)
// 3. Load/save atomici (rename dopo write)
```
**Prossimi passi:**
- [ ] Definire `DiskNodeCache` class con `put(key, fb)` e `get(key)` 
- [ ] Metadata: `{digest, params_hash, source_hash, input_hash, width, height, timestamp}`
- [ ] `load(path)` / `save(path, framebuffer)` — usare `XXH3_64bits` per generare path unico
- [ ] In `GraphExecutor::execute()`, se `policy.disk_cacheable && disk_cache.exists(key)` → load from disk

---

### I7. Unificare `hash_string` Duplicato

**Problema:** `hash_string(string_view)` è definito identicamente in `node_cache.cpp` e `frame_cache.cpp`. Violazione DRY, rischio di divergenza.
**Soluzione:** Spostare in un header comune (`render_hash_utils.hpp` esiste già) ed eliminare le copie.
**Dove:** `src/cache/node_cache.cpp`, `src/cache/frame_cache.cpp` → `include/chronon3d/render_graph/render_hash_utils.hpp`.
**Guadagno stimato:** Manutenibilità, zero overhead.
**Prossimi passi:**
- [ ] Spostare `hash_string` in `render_hash_utils.hpp` come `inline`
- [ ] Rimuovere le definizioni duplicate da `node_cache.cpp` e `frame_cache.cpp`
- [ ] Verificare che non ci siano altre copie nascoste

---

### I8. Ridurre Boilerplate dei RenderCounters

**Problema:** `RenderCounters` in `counters.hpp` ha 30+ campi `std::atomic` e il costruttore `reset()` ha 100+ linee ripetitive. Ogni nuovo counter richiede modifiche in 4-5 punti.
**Soluzione:** Usare X-macro o un generatore di codice per definire i campi una volta sola. Template reflection-like con macro.
**Dove:** `include/chronon3d/core/counters.hpp`.
**Guadagno stimato:** Manutenibilità, meno errori di copia-incolla, aggiungere un counter scende da 5 minuti a 30 secondi.
**Prossimi passi:**
- [ ] Definire una X-macro `CHRONON_COUNTERS(X)` con tutti i campi
- [ ] Generare dichiarazioni, reset, merge, e serializzazione da un'unica lista
- [ ] Verificare che i test esistenti passino invariati

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
- [ ] Registrare i buffer YUV/NV12 come registered buffers
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
**Soluzione:** EXR tiled 256×256 con compressione DWAA (lossless, molto più veloce di ZIP).
**Dove:** `apps/chronon3d_cli/commands/command_bake_layer.cpp` + `src/backends/image/exr_writer.cpp` (da creare).
**Guadagno stimato:** Bake 16-bit più veloci di PNG, mmap read parziale, nessuna perdita cromatica.
**Dipendenza:** Il progetto sembra avere OpenEXR già come deps (vcpkg.json → `openexr`).
**Prossimi passi:**
- [ ] Verificare che OpenEXR sia nel vcpkg.json
- [ ] Creare `exr_writer.cpp` con tiled writes (256×256 tile, DWAA)
- [ ] Modificare `command_bake_layer` per salvare in EXR invece di PNG di default
- [ ] Aggiungere `--exr-bake` flag

---

### S5. Shard Lock per Path Flatten Cache

**Problema:** In `path_rasterizer.cpp` c'è un mutex globale (`g_flatten_cache_mutex`) che protegge l'intera mappa di path flattened. Tutti i thread aspettano su questo lock.
**Soluzione:** Sostituire l'`std::unordered_map` globale + mutex con 16 shard, ognuno col suo mutex. Pattern identico a `LruCache`.
**Dove:** `src/backends/software/rasterizers/path_rasterizer.cpp`.
**Guadagno stimato:** Elimina contention su mutex nel hot path — ~2-5% speedup in scene con molti path.
**Codice esistente da modificare:**
```cpp
// Il pattern LruCache sharded è già implementato in lru_cache.hpp
// Basta fare lo stesso per g_flatten_cache:
using PathCache = cache::LruCache<CacheKey, std::shared_ptr<const std::vector<PathContour>>>;
static PathCache g_path_cache(64 * 1024 * 1024, 16);  // 64MB, 16 shard
// Sostituire tutti gli accessi: get → cache.get(), put → cache.put()
```
**Prossimi passi:**
- [ ] Sostituire `g_flatten_cache` + mutex con `LruCache` sharded
- [ ] Verificare che `hash_path()` generi chiavi stabili
- [ ] Benchmark: misurare prima/dopo su scene con 100+ path

---

### S6. SIMD Point-in-Polygon per Path Rasterizer

**Problema:** `point_in_polygon_even_odd()` in `path_rasterizer.cpp` è chiamato per ogni pixel dentro la bbox. È interamente scalar.
**Soluzione:** Batch processing — per 8 pixel contemporaneamente, calcola se sono dentro il poligono usando SIMD. Oppure: salvare il polygon in una texture e usare `pointInPolygon` vettorizzato.
**Dove:** `src/backends/software/rasterizers/path_rasterizer.cpp` — funzione `point_in_polygon_even_odd`.
**Guadagno stimato:** Se il 60% del tempo è in questo loop → 3-5x speedup su quel 60%.
**Prossimi passi:**
- [ ] Identificare se è effettivamente il collo di bottiglia (profiling con `perf`)
- [ ] Riscrivere il loop in stile SIMD: processare 8 x-coord alla volta
- [ ] Alternativa: rasterizzare il polygon shape una volta su una mask texture, poi campionare

---

### S7. Eliminare `std::shared_ptr<Framebuffer>` nel Hot Path

**Problema:** Il codebase usa `shared_ptr<Framebuffer>` in 162+ posizioni, incluso dentro `execute()` di ogni `RenderNode`. Ogni copy aggiunge overhead di atomic reference count (lock prefix x86) in pieno hot path di rendering.
**Soluzione:** Sostituire con `Framebuffer*` raw pointers gestiti dal pool. Il `FramebufferPool` già garantisce lifetime — non serve ref counting.
**Dove:** Tutti i nodi in `include/chronon3d/render_graph/nodes/*.hpp` + `graph_executor.cpp`.
**Guadagno stimato:** -3-5% overhead frame, eliminazione di ~50 atomic ops per nodo.
**Prossimi passi:**
- [ ] Cambiare la signature di `RenderNode::execute()`: `vector<shared_ptr<FB>>` → `vector<FB*>`
- [ ] Aggiornare tutti i nodi concreti (source, blur, composite, transform, mask, shadow, text, ecc.)
- [ ] Rimuovere `shared_ptr` dove possibile, tenendolo solo per ownership nel pool
- [ ] Benchmark prima/dopo su una composition complessa per validare il guadagno

---

### S8. RenderCounters Thread-Local Accumulator

**Problema:** `RenderCounters` ha **30+ `std::atomic`** campi. Ogni `fetch_add` costa anche con `memory_order_relaxed`. In scenari multi-thread, la contention sulla cache line degli atomics è misurabile.
**Soluzione:** Usare counter thread-local (`thread_local RenderCountersRaw`) che vengono mergiati a fine frame con un singolo passaggio atomico. Pattern già usato in motori di gioco (Unreal, Unity).
**Dove:** `include/chronon3d/core/counters.hpp`.
**Guadagno stimato:** Elimina 30+ operazioni atomiche per frame. -0.5-1% overhead su frame medi.
**Prossimi passi:**
- [ ] Definire `RenderCountersRaw` senza atomics (struct POD)
- [ ] Creare `thread_local RenderCountersRaw tls_counters`
- [ ] Alla fine del frame: `global_counters.merge(tls_counters)` con un solo passaggio
- [ ] Verificare che i test di telemetry passino invariati

---

### S9. ImageCache Sharding + Read-Write Lock

**Problema:** `ImageCache` usa un singolo `std::mutex m_mutex` per tutte le operazioni. Durante il preload multi-thread delle immagini, tutti i thread aspettano su questo lock.
**Soluzione:** Trasformare in cache sharded con `std::shared_mutex` (pattern `LruCache` già esistente nel progetto).
**Dove:** `include/chronon3d/backends/assets/image_cache.hpp` + `src/backends/assets/image_cache.cpp`.
**Guadagno stimato:** Preload parallelo di immagini 2-3x più veloce.
**Prossimi passi:**
- [ ] Sostituire `std::mutex m_mutex` con 16 shard di `std::shared_mutex`
- [ ] `get_or_load()` usa `shared_lock` (read), `insert()` usa `unique_lock` (write)
- [ ] Mergiare questa modifica con M6 (LRU con memory budget)

---

### S10. SIMD Alpha Premultiplication in ImageCache

**Problema:** `ImageCache::get_or_load()` ha un loop pixel-by-pixel scalare per premoltiplicare alpha. Su immagini 4K sono 33M iterazioni.
**Soluzione:** Usare Highway SIMD (già dipendenza del progetto) per processare 4/8 pixel per istruzione.
**Dove:** `src/backends/assets/image_cache.cpp`.
**Guadagno stimato:** Caricamento immagini 2-3x più veloce, startup ridotto.
**Prossimi passi:**
- [ ] Scrivere `premultiply_alpha_simd(uint8_t* rgba, size_t pixel_count)` con Highway
- [ ] Sostituire il loop scalare in `get_or_load()` con la versione SIMD
- [ ] Benchmark su immagini 4K: confrontare tempi di load prima/dopo

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

### M5. Transform Cache — Risultati Animazione Baked

**Problema:** `TransformNode` ricalcola la matrice ogni frame anche per animazioni identiche (es. opacity fade lineare, position animation tra keyframe).
**Soluzione:** Cache della matrice transform risolta per ogni frame. Se la sequence di keyframe non cambia tra frame consecutivi → skip recompute.
**Dove:** `src/scene/layer.cpp` — `m_layer.anim_transform.evaluate()` già esiste, il risultato non è cacheato.
**Guadagno stimato:** Per scene con molti layer animati → ~5-10% speedup.
**Prossimi passi:**
- [ ] Aggiungere `TransformCache` in `Layer` — mappa frame → Transform baked
- [ ] Nel `LayerBuilder::resolve()`, controllare la cache prima di valutare
- [ ] Invalutare quando keyframe list cambia
- [ ] Wire up in `scene.cpp` — `Scene::resolve_layer_at_frame()`

---

### M6. ImageCache LRU con Memory Budget

**Problema:** `ImageCache` (`src/backends/assets/image_cache.cpp`) è un semplice `unordered_map` — nessuna eviction policy, nessun memory limit. Le immagini caricate restano in RAM per sempre.
**Soluzione:** Trasformare in `LruCache<string, CachedImage>` usando il pattern già esistente in `lru_cache.hpp`.
**Dove:** `src/backends/assets/image_cache.cpp` — sostituire `std::unordered_map` con `cache::LruCache`.
**Guadagno stimato:** Memory bounded, nessun OOM, reuse ottimo per texture ripetute.
**Prossimi passi:**
- [ ] Sostituire `m_cache` (unordered_map) con `cache::LruCache<string, CachedImage>`
- [ ] Implementare weight come `width * height * 4` byte per immagine
- [ ] Aggiungere `CHRONON_IMAGE_CACHE_MAX_MB` env var (esiste già in node_cache e text_cache)
- [ ] Aggiungere preload hint: `ImageCache::preload_async()` per iniziare a caricare prima che serva

---

### M7. Aggiungere Cache Hit/Miss Telemetry nel Render Report

**Problema:** Il sistema di cache (node, text, image) ha `Stats` strutture con hits/misses/evictions, ma non vengono riportate nel benchmark JSON finale.
**Soluzione:** Aggregare tutte le cache stats e includerle nel `BenchmarkReport` JSON.
**Dove:** `include/chronon3d/core/benchmark_report.hpp` + `src/core/benchmark_report.cpp`.
**Guadagno stimato:** Visibilità completa sulle performance — permette di identificare bottleneck di cache.
**Prossimi passi:**
- [ ] Aggiungere `CacheStatsReport` struttura con hits/misses/evictions per ogni cache type
- [ ] In `BenchmarkReport`, aggiungere `cache_stats: CacheStatsReport`
- [ ] In `command_bench`, popolare le stats da `node_cache.stats()`, `text_cache.stats()`, `image_cache.stats()`
- [ ] Stampare nel JSON finale sotto `render.cache`

---

### M8. CI Static Analysis con clang-tidy

**Problema:** Nessun controllo automatico di qualità del codice. Bug come `reinterpret_cast` non necessari, variabili unused, o potenziali memory leak passano inosservati.
**Soluzione:** Aggiungere un job `clang-tidy` alla CI esistente (`.github/workflows/ci.yml`) con regole selezionate.
**Dove:** `.github/workflows/ci.yml` + eventuale `.clang-tidy` config file.
**Guadagno stimato:** Qualità del codice più alta, meno bug in produzione, enforcement automatico delle best practice.
**Prossimi passi:**
- [ ] Creare `.clang-tidy` con regole: `modernize-*`, `performance-*`, `readability-*` (escludendo quelle troppo pedanti)
- [ ] Aggiungere step `clang-tidy` al workflow CI esistente
- [ ] Fixare i warning esistenti (o aggiungere `// NOLINT` giustificati)
- [ ] Opzionale: aggiungere `clang-format --check` per forzare lo stile

---

### M9. CancellationToken per Shutdown Graceful

**Problema:** Durante un render lungo (es. 900+ frame), un SIGINT/Ctrl-C causa terminazione immediata. Nessuna pulizia ordinata di risorse (file temporanei, ffmpeg pipe, telemetry).
**Soluzione:** `CancellationToken` passato al render loop — ogni frame controlla `is_cancelled()` e fa cleanup prima di uscire.
**Dove:** Nuovo file `include/chronon3d/core/cancellation_token.hpp` + modifica a `render_pipeline.cpp`.
**Guadagno stimato:** Nessuna perdita di risorse su interrupt, telemetry salvato anche in caso di stop.
**Prossimi passi:**
- [ ] Definire `CancellationToken` con `atomic<bool>` e metodo `cancel()`
- [ ] Passarlo a `render_pipeline::render_range()` e `command_video()`
- [ ] Installare signal handler (`sigaction` su Linux, `SetConsoleCtrlHandler` su Windows)
- [ ] Nel loop: `if (token.is_cancelled()) { flush_telemetry(); close_pipe(); return; }`

---

### M10. CLI `--dry-run` per Validazione Pre-Render

**Problema:** L'unico modo per sapere se una composition funziona è avviare un render completo. Errori di configurazione (asset mancanti, parametri errati) vengono scoperti solo a runtime.
**Soluzione:** Aggiungere flag `--dry-run` che esegue tutto il setup (caricamento asset, build del grafo, validazione parametri) ma NON renderizza.
**Dove:** `apps/chronon3d_cli/commands/command_video.cpp` + `command_bake_layer.cpp`.
**Guadagno stimato:** Feedback immediato (1-2s invece di minuti) su errori di configurazione.
**Prossimi passi:**
- [ ] Aggiungere flag `--dry-run` ai comandi `video` e `bake-layer`
- [ ] Eseguire `RenderPreflight::validate_or_throw()` + `GraphBuilder::build()` + validazione parametri
- [ ] Stampare report: "N nodi, M layer, K effetti, X MB stimati — OK"
- [ ] Uscire con codice 0 se tutto valido, 1 con errori

---

### M11. Test Coverage per Nodi Render Graph Mancanti

**Problema:** Solo una parte dei `RenderNode` ha test dedicati. Nodi complessi come `ShadowNode`, `GlowNode`, `GradientNode` non hanno test unitari. Bug in questi nodi sono silenziosi fino al render visivo.
**Soluzione:** Aggiungere test per ogni nodo non coperto, con input sintetici e output verificati via hash/pixel.
**Dove:** `tests/render_graph/` — nuovi file test per ogni nodo mancante.
**Guadagno stimato:** Regression detection automatica, refactoring sicuro, meno bug visivi.
**Prossimi passi:**
- [ ] Identificare tutti i nodi senza test (confrontare `nodes/*.hpp` con `tests/render_graph/*.cpp`)
- [ ] Scrivere test per: `ShadowNode`, `GlowNode`, `BloomNode`, `GradientNode`, `DoFNode`, `MaskNode`
- [ ] Usare pattern esistente: `RenderFixtures` + `pixel_assertions` + golden references
- [ ] Integrare nel `test_registry` e far girare in CI

---

### M12. `std::pmr` Allocator nei Comandi CLI

**Problema:** I comandi CLI (`command_video.cpp`, `command_bench.cpp`) allocano `std::string`, `std::vector`, `nlohmann::json` sulla heap globale. In un contesto batch, la frammentazione si accumula.
**Soluzione:** Usare `std::pmr::string` e `std::pmr::vector` con un `monotonic_buffer_resource` scoped al comando. Pattern già usato da `FrameArena`.
**Dove:** `apps/chronon3d_cli/commands/command_video.cpp`, `command_bench.cpp`, `command_bake_layer.cpp`.
**Guadagno stimato:** Meno frammentazione heap in esecuzioni batch, cleanup immediato a fine comando.
**Prossimi passi:**
- [ ] Creare `CliArena` wrapper intorno a `monotonic_buffer_resource` (1MB)
- [ ] Sostituire `std::string` → `std::pmr::string`, `std::vector` → `std::pmr::vector` nei comandi
- [ ] Per `nlohmann::json`, usare allocator polimorfico se supportato
- [ ] Benchmark: verificare che non ci sia regressione di performance

---

## 🌀 LONG-TERM — Mesi (grandi architetture)

### L1. GPU Backend per EffectStack (Vulkan Compute)

**Problema:** I filtri (blur, glow, color grading) sono CPU-bound → 10-20x più lenti della GPU.
**Soluzione:** Mandare EffectStack e Composite su Vulkan compute. Zero-copy con memoria condivisa. Il resto resta CPU.
**Dove:** Nuovo target `chronon3d_backend_vulkan` + `src/backends/vulkan/compute_effects.cpp`.
**Guadagno stimato:** Blur gaussiano → 10-20x più veloce. Glow → 15x.
**Note:** Questo trasforma Chronon3D da pure-CPU a hybrid CPU/GPU. È il cambio architetturale più grosso.
**Sotto-passi:**
- [ ] Inizializzare VkInstance + VkDevice con VK_KHR_external_memory +
- [ ] Allocare VKBuffer condiviso tra CPU e GPU (VK_KHR_dedicated_allocation)
- [ ] Compilare compute shader per Gaussian blur (usa un già esistente HLSL/GLSL)
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
**Struttura:**
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

### L6. HarfBuzz Text Shaping Integration

**Problema:** Il text rendering usa il layout engine custom (`TextLayoutEngine::layout`). Supporto limitato per lingue non-latine, legature, e forme complicate.
**Soluzione:** Integrare HarfBuzz per lo shaping dei glifi — standard industriale, usato da Chrome, Firefox, Android.
**Dove:** `src/backends/text/text_layout_engine.cpp` — sostituire o estendere il layout engine attuale.
**Guadagno stimato:** Supporto completo Unicode, shaping corretto per Arabo, Hindi, Thai, etc.
**Prossimi passi:**
- [ ] Aggiungere HarfBuzz come dipendenza vcpkg
- [ ] Creare `hb_shape(text, font)` → glyph positions
- [ ] Integrare con `TextLayoutEngine` esistente
- [ ] Benchmark: verificare che lo shaping non rallenti il text rendering

---

### L7. MSDF Font Atlas per Text Scalability

**Problema:** Text scaling/rotation su textures rasterizzate causa blur o pixelation.
**Soluzione:** Multi-channel Signed Distance Fields (MSDF) — il glypo è memorizzato come distanza dai bordi, non come pixel bitmap. Renderizza perfettamente a qualsiasi scala.
**Dove:** Nuovo file `src/backends/text/msdf_generator.cpp` + modifica a `text_rasterizer_utils.cpp`.
**Guadagno stimato:** Text perfetto a qualsiasi risoluzione, scaling istantaneo senza re-rasterize.
**Prossimi passi:**
- [ ] Integrare `chlumsky/msdfgen` (header-only, MIT license)
- [ ] Generare MSDF atlas per ogni font in uso
- [ ] Modificare `rasterize_text_to_bl_image()` — se MSDF disponibile, usare quello
- [ ] Shader di reconstruction nel composite: `distance → alpha` con smoothing

---

### L8. Parallel Tile Rendering (Bucket-Based)

**Problema:** Il rendering processa l'immagine in modo lineare — se un layer è in alto a sinistra, i core右边 non lavorano.
**Soluzione:** Dividere l'immagine in tile (es. 256×256), processare tile indipendenti in parallelo — come Cycles e RenderMan.
**Dove:** `src/render_graph/graph_executor.cpp` + `src/backends/software/software_renderer.cpp`.
**Guadagno stimato:** Utilizzo 100% dei core — 2-4x speedup su scene complesse.
**Prossimi passi:**
- [ ] Aggiungere `TileScheduler` — divide viewport in tile
- [ ] Ogni tile è un mini-render-graph indipendente
- [ ] Worker pool processa tile in parallelo
- [ ] Risultati mergiati nel buffer finale

---

### L9. CI Multi-Platform (Windows + macOS)

**Problema:** La CI attuale (`.github/workflows/ci.yml`) builda e testa solo su Linux. Bug specifici di piattaforma (encoding path Windows, mmap macOS, ifdef errate) non vengono catturati.
**Soluzione:** Aggiungere matrix build con `ubuntu-latest`, `windows-latest`, `macos-latest` al workflow CI.
**Dove:** `.github/workflows/ci.yml`.
**Guadagno stimato:** Nessun bug di piattaforma in produzione, coverage reale multi-OS.
**Prossimi passi:**
- [ ] Aggiungere `strategy: matrix: { os: [ubuntu-latest, windows-latest, macos-latest] }`
- [ ] Adattare gli step di build per ogni OS (vcpkg setup, CMake presets)
- [ ] Filtrare test Linux-only (es. io_uring tests)
- [ ] Verificare che tutti i test passino su tutte le piattaforme

---

## 📋 RIEPILOGO MATRICE

| ID | Improvement | Quando | Complessità | Impatto | Stato |
|----|------------|--------|-------------|---------|-------|
| I1 | Bake grid EXR + mmap | Oggi | 🟢 Bassa | 🔴 Alto | Da fare |
| I2 | Huge pages | Oggi | 🟢 Bassa | 🔴 Alto | Da fare |
| I3 | Dirty rect bitmask | Oggi | 🟢 Bassa | 🔴 Alto | Da fare |
| I4 | Thread affinity + NUMA | Questa settimana | 🟢 Bassa | 🟡 Medio | Da fare |
| I5 | FrameArena nel render pipeline | Oggi | 🟢 Bassa | 🟡 Medio | Da fare |
| I6 | PersistentDisk cache (disk) | Oggi | 🟡 Media | 🔴 Alto | Da fare |
| I7 | Unificare hash_string | Oggi | 🟢 Bassa | 🟢 Basso | Da fare |
| I8 | Ridurre boilerplate counters | Oggi | 🟢 Bassa | 🟢 Basso | Da fare |
| S1 | io_uring pipe | Questa settimana | 🟡 Media | 🟡 Medio | ✅ Fatto |
| S2 | Temporal hashing | Questa settimana | 🟡 Media | 🔴 Alto | Da fare |
| S3 | L1/L2 prefetch | Questa settimana | 🟢 Bassa | 🟡 Medio | Da fare |
| S4 | OpenEXR DWAA bake | Questa settimana | 🟡 Media | 🟡 Medio | ✅ Fatto |
| S5 | Path flatten cache sharded | Questa settimana | 🟢 Bassa | 🟡 Medio | Da fare |
| S6 | SIMD point-in-polygon | Questa settimana | 🟡 Media | 🟡 Medio | Da fare |
| S7 | Eliminare shared_ptr nel hot path | Questa settimana | 🟡 Media | 🔴 Alto | Da fare |
| S8 | RenderCounters thread-local | Questa settimana | 🟢 Bassa | 🟡 Medio | Da fare |
| S9 | ImageCache sharding | Questa settimana | 🟢 Bassa | 🟡 Medio | Da fare |
| S10 | SIMD alpha premultiply | Questa settimana | 🟢 Bassa | 🟡 Medio | Da fare |
| M1 | Graph compiler | Questo mese | 🔴 Alta | 🔴 Alto | Da fare |
| M2 | ISPC blur | Questo mese | 🔴 Alta | 🟡 Medio | Da fare |
| M3 | SPSC lock-free queue | Questo mese | 🟡 Media | 🟡 Medio | Da fare |
| M4 | Speculative render | Questo mese | 🔴 Alta | 🔴 Alto | Da fare |
| M5 | Transform cache | Questo mese | 🟡 Media | 🟡 Medio | Da fare |
| M6 | ImageCache LRU | Questo mese | 🟢 Bassa | 🟡 Medio | Da fare |
| M7 | Cache telemetry nel report | Questo mese | 🟢 Bassa | 🟡 Medio | Da fare |
| M8 | CI clang-tidy | Questo mese | 🟢 Bassa | 🟡 Medio | Da fare |
| M9 | CancellationToken | Questo mese | 🟡 Media | 🟡 Medio | Da fare |
| M10 | CLI --dry-run | Questo mese | 🟢 Bassa | 🟡 Medio | Da fare |
| M11 | Test coverage nodi mancanti | Questo mese | 🟡 Media | 🔴 Alto | Da fare |
| M12 | std::pmr nei comandi CLI | Questo mese | 🟡 Media | 🟡 Medio | Da fare |
| L1 | GPU Vulkan compute | Mesi | ⚫ Molto Alta | 🔴 Alto | Da fare |
| L2 | ECS architecture | Mesi | 🔴 Alta | 🟡 Medio | Da fare |
| L3 | Frame Graph RDG | Mesi | 🔴 Alta | 🔴 Alto | Da fare |
| L4 | Persistent daemon | Mesi | 🟡 Media | 🔴 Alto | Da fare |
| L5 | Distributed render farm | Mesi | ⚫ Molto Alta | 🔴 Alto | Da fare |
| L6 | HarfBuzz text shaping | Mesi | 🔴 Alta | 🟡 Medio | Da fare |
| L7 | MSDF font atlas | Mesi | 🔴 Alta | 🟡 Medio | Da fare |
| L8 | Parallel tile rendering | Mesi | 🔴 Alta | 🔴 Alto | Da fare |
| L9 | CI multi-platform | Mesi | 🟡 Media | 🟡 Medio | Da fare |

---

## 🔍 Cose Già Implementate nel Codebase (non toccare)

| Feature | Dove |
|---|---|
| **TBB parallel_for** | `graph_executor.cpp` (execute level), `software_compositor.cpp` (composite_layer) |
| **TBB task_arena** | `GraphExecutor::m_arena` — esiste ma senza affinity |
| **LRU Cache sharded** | `include/chronon3d/cache/lru_cache.hpp` — 16 shard, mutex per shard |
| **xxHash (XXH3_64bits)** | `node_cache.cpp`, `text_rasterizer_utils.cpp`, `render_hash_utils.cpp` |
| **FrameArena** | `include/chronon3d/core/arena.hpp` — monotonic_buffer_resource, 1MB default |
| **CachePolicy completo** | `include/chronon3d/render_graph/cache_policy.hpp` — PerFrame/PerComposition/PersistentDisk |
| **static_memory_cache()** | Helper già esiste in `cache_policy.hpp` |
| **FrameInvariant policy** | `frame_invariant{true}` già supportato nel GraphExecutor |
| **Consumer reference counting** | `consumer_remaining` atomic in `graph_executor.cpp` — ottimo pattern |
| **Dirty rect tracking** | Contatori in counters.hpp (dirty_rect_count, dirty_pixels, dirty_full_fallbacks) |
| **DirtyFallbackReason enum** | 4 motivi di fallback, expandable |
| **NodeCache con xxHash** | `src/cache/node_cache.cpp` — disk_cacheable mai usato però |
| **Text raster LRU cache** | `text_rasterizer_utils.cpp` — sharded LruCache, env var `CHRONON_TEXT_CACHE_MAX_MB` |
| **NodeCache env var** | `CHRONON_NODE_CACHE_MAX_MB` — configurabile |
| **Path flatten cache** | `path_rasterizer.cpp` — globale mutex però, va sharded |
| **Compute_path_bbox** | Con flatten cache — bounding box preciso |
| **ImageCache singleton** | `ImageCache::instance()` — ma senza eviction, memory unbounded |
| **FramebufferPool preallocato** | `preallocate()` con 3 risoluzioni, stats, touch_memory |
| **Renderer warmup** | `warmup_renderer()` — dummy frame + prealloc |
| **Color space pipeline** | `color_space.hpp` — sRGB↔Linear IEC 61966-2-1 |
| **YUV420P/NV12 in pipe** | `ffmpeg_pipe_encoder.cpp` — BT.601 coeffs, default yuv420p |
| **Highway SIMD kernels** | `highway_kernels.cpp` — composite_normal_premul |
| **Effect stack splittato** | `effects/` dir — blur, color, wave, stack |
| **Blend2D JIT bridge** | `blend2d_bridge.cpp` — rasterizzazione JIT |

---

## 🎯 Priorità Raccomandata

Se dovessi scegliere **una sola cosa** da implementare oggi: **I6 (PersistentDisk cache)** — sfrutta il campo `disk_cacheable` che è già definito ma mai usato, e dà beneficio enorme sui nodi statici.

Se potessi implementare **3 cose** questa settimana: **I1 + I2 + I3** insieme.
Il guadagno combinato è ~25-35% faster overall, principalmente sul primo frame.

Quick win da non trascurare: **I7** e **I8** — sono da 30 minuti ciascuno, puliscono il codebase senza rischi.

La più divertente a lungo termine: **M1 (Graph Compiler)** — è come passare da una ricetta letta ogni volta a un robot che sa già tutti i movimenti a memoria.

✅ **Già completati in questa iterazione:**
- S1 (io_uring pipe), S4 (OpenEXR DWAA bake).
- I7 (Unificazione hash_string).
- I8 (Riduzione boilerplate RenderCounters).
- A1 (Ottimizzazione Umbrella Headers).
- A2 (Decoupling RenderNode shape state).
- C1 (SIMD Rect Rasterizer).
- E1 (Unificazione Effect System std::any).