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
./chronon3d_cli bake-layer --comp GridTitleMotion --layer bg --output /tmp/bg.exr
```
✅ **STATO: COMPLETATO** — `exr_mmap.cpp` con `load_exr_mmap()`, `DarkGridBackground` usa EXR con DWAA compression e half-float, `command_bake_layer` supporta `--exr-bake` flag.
**Prossimi passi per miglioramenti futuri:**
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
✅ **STATO: COMPLETATO** — Helper `allocate_huge_pages()` / `free_huge_pages()` in `memory_utils.hpp`, `HugePageAllocator<T>` allocatore STL-compatibile, integrato in `Framebuffer` via `HugePageAllocator<Color>`.
**Prossimi passi per miglioramenti futuri:**
- [ ] Estendere a Windows con `VirtualAlloc(MEM_LARGE_PAGES)`

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

✅ **STATO: COMPLETATO** — `DirtyRectMask` definito in `include/chronon3d/core/dirty_rect_mask.hpp`, integrato in `RenderNode` e usato nel compositing.
**Prossimi passi per miglioramenti futuri:**
- [ ] Wire up completo in `render_pipeline.cpp` — la mask vive tra graph build e execute
- [ ] Ottimizzare iterator per scanare solo le celle dirty senza iterare tutta l'immagine

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
✅ **STATO: COMPLETATO** — `FrameArena` è membro di `GraphExecutor` (`m_frame_arena`), usata in `graph_executor_phases.cpp` per le allocazioni temporanee, con `guard` RAII per reset a fine frame.

**Prossimi passi per miglioramenti futuri:**
- [ ] Estendere uso di `FrameArena::vector` a più vettori temporanei nell'execute loop

---

### I6. Disk Cache per Nodi Statici (PersistentDisk Lifetime)

**Problema:** I nodi con `CacheLifetime::PersistentDisk` (già definito in `cache_policy.hpp`) non hanno implementazione. Il campo `disk_cacheable` è sempre `false`.
**Soluzione:** Implementare un layer di cache su disco per nodi statici come `grid_bg` — hashed su params+input, salvato come file binario.
**Dove:** Nuovo file `src/cache/disk_node_cache.cpp` + modifica a `graph_executor.cpp`.
**Guadagno stimato:** Skip rendering completo di nodi statici tra sessioni.

✅ **STATO: COMPLETATO** — `DiskNodeCache` class definita in `disk_node_cache.hpp/.cpp` con `get()`, `put()`, `exists()`, `clear()`. Usa mmap su Linux, file mapping atomico con rename, env var `CHRONON_DISK_CACHE_DIR`. `disk_cacheable=true` abilitato per nodi statici via `static_memory_cache()` in `cache_policy.hpp`. `GraphExecutor` controlla `disk_cache.exists(key)` in `graph_executor_cache.cpp` prima di renderizzare.

**Checklist completata:**
- ✅ `disk_cacheable = true` per nodi statici (grid_bg, immagini fisse, testi non animati) — `static_memory_cache()` in `cache_policy.hpp`
- ✅ `GraphExecutor::execute()` controlla `disk_cache.exists(key)` prima di `evaluate_cache()` — `graph_executor_cache.cpp`
- ✅ Hash della key con `NodeCacheKey::digest()` già implementato (XXH3-based)
- ⬜ Aggiungere telemetry: disk cache hit/miss nei contatori
- ⬜ Metadata: `{digest, params_hash, source_hash, input_hash, width, height, timestamp}`

---

### I7. Unificare `hash_string` Duplicato

**Problema:** `hash_string(string_view)` è definito identicamente in `node_cache.cpp` e `frame_cache.cpp`. Violazione DRY, rischio di divergenza.
**Soluzione:** Spostare in un header comune (`render_hash_utils.hpp` esiste già) ed eliminare le copie.
**Dove:** `src/cache/node_cache.cpp`, `src/cache/frame_cache.cpp` → `include/chronon3d/render_graph/render_hash_utils.hpp`.
**Guadagno stimato:** Manutenibilità, zero overhead.
✅ **STATO: COMPLETATO** — `hash_string()` definito in `render_graph_hashing.hpp` come `inline`, usato da tutti i consumer (node_cache, frame_cache, text_rasterizer, render_hash_utils, software_text_processor_hash, ecc.). Nessuna copia duplicata.

---

### I8. Ridurre Boilerplate dei RenderCounters

**Problema:** `RenderCounters` in `counters.hpp` ha 30+ campi `std::atomic` e il costruttore `reset()` ha 100+ linee ripetitive. Ogni nuovo counter richiede modifiche in 4-5 punti.
**Soluzione:** Usare X-macro o un generatore di codice per definire i campi una volta sola. Template reflection-like con macro.
**Dove:** `include/chronon3d/core/counters.hpp`.
**Guadagno stimato:** Manutenibilità, meno errori di copia-incolla, aggiungere un counter scende da 5 minuti a 30 secondi.
✅ **STATO: COMPLETATO** — `CHRONON_RENDER_COUNTERS(X)` X-macro definita in `counters.hpp`, genera dichiarazioni atomic, reset, copy constructor, e move constructor da un'unica lista.

---

## ⚡ SHORT-TERM — Questa settimana (1-3 giorni ciascuno)

### S1. io_uring per la Pipe FFmpeg (Linux)

**Problema:** Scrittura classica su pipe → context switch ripetuti → backpressure.
**Soluzione:** `io_uring` con `IORING_OP_WRITE_FIXED` e buffer registrati — zero copy da RAM a socket.
**Dove:** `apps/chronon3d_cli/utils/ffmpeg_pipe_encoder.cpp`.
**Guadagno stimato:** -15-20ms di latenza per frame a 4K.
**Alternativa Windows:** Named pipe con `FILE_FLAG_NO_BUFFERING` + `WriteFileGather`.
✅ **STATO: COMPLETATO** — io_uring implementato in `ffmpeg_pipe_encoder.cpp` con `IORING_OP_WRITE_FIXED`, supporto per registered buffers, fallback automatico a write() classico.

**Prossimi passi per miglioramenti futuri:**
- [ ] Registrare i buffer YUV/NV12 come registered buffers

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
✅ **STATO: COMPLETATO** — OpenEXR in vcpkg.json, `image_writer.cpp` con tiled writes 256×256 e DWAA compression, `command_bake_layer` con flag `--exr-bake`, test EXR writer esistenti.

---

### S5. Shard Lock per Path Flatten Cache

**Problema:** In `path_rasterizer.cpp` c'è un mutex globale (`g_flatten_cache_mutex`) che protegge l'intera mappa di path flattened. Tutti i thread aspettano su questo lock.
**Soluzione:** Sostituire l'`std::unordered_map` globale + mutex con 16 shard, ognuno col suo mutex. Pattern identico a `LruCache`.
**Dove:** `src/backends/software/rasterizers/path_rasterizer.cpp`.
**Guadagno stimato:** Elimina contention su mutex nel hot path — ~2-5% speedup in scene con molti path.

✅ **STATO: COMPLETATO** — `path_cache.hpp` con `LruCache<CacheKey, shared_ptr<const vector<PathContour>>>` (16 shard, 64 MB), usato in `path_rasterizer.cpp` via `flatten_path_cached()`.

**Rimane:**
- [ ] Benchmark: misurare prima/dopo su scene con 100+ path
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

✅ **STATO: COMPLETATO** — `RenderCountersRaw` struct POD non-atomic definita in `counters.hpp`. `merge_tls()` implementato per merge in un singolo passaggio atomico a fine frame.

**Rimane:**
- [ ] Creare e usare `thread_local RenderCountersRaw tls_counters` nel render loop
- [ ] Verificare che i test di telemetry passino invariati
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
✅ **STATO: COMPLETATO** — `simd::premultiply_alpha_rgba8()` implementato in `highway_kernels.cpp` (HWY_DYNAMIC_DISPATCH), usato in `ImageCache::get_or_load_shared()` per premoltiplicare alpha.

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
// Output del compiler — un esempio:
extern “C” void render_ExampleComp_Frame(
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
- [ ] Generare codice C++ da una composition (es. GridTitleMotion → `render_gridtitle.cpp`)
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
✅ **STATO: COMPLETATO** — `ImageCache` ora usa `cache::LruCache<std::string, std::shared_ptr<CachedImage>>` con memory budget configurabile via `CHRONON_IMAGE_CACHE_MAX_MB`. Weight = width × height × 4 bytes.

**Prossimi passi per miglioramenti futuri:**
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

✅ **STATO: COMPLETATO** — `.clang-tidy` config con 18 categorie di checks (performance, modernize, readability, clang-analyzer, bugprone). Job `clang-tidy` aggiunto al workflow CI in `.github/workflows/ci.yml`.
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

✅ **STATO: COMPLETATO** — `CancellationToken` definito in `cancellation_token.hpp/.cpp` con `is_cancelled()`, `cancel()`, `reset()`. Handler SIGINT/SIGTERM installato via `sigaction`. Integrato in `command_video.cpp` e `video_export_pipe.cpp` per cleanup graceful.
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

✅ **STATO: COMPLETATO** — Flag `--dry-run` aggiunto a `chronon3d_cli video` (registrato in `register_video_commands.cpp`, gestito in `command_video.cpp`). Stampa composizione, risoluzione, frame range, FPS, durata, output path, SSAA — poi esce senza renderizzare.
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
{ “cmd”: “render”, “comp”: “GridTitleMotion”, “frames”: [0, 900], “output”: “/tmp/out.mp4” }

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

## 🆕 NUOVE OPPORTUNITÀ (SCOPERTE IN QUESTA ANALISI)

> Le seguenti opportunità sono emerse da un'analisi approfondita del codebase eseguita a maggio 2026.
> Non erano precedentemente documentate in questo roadmap.

---

### N1. Motion Blur Accumulation Parallel + SIMD

**Problema:** In `render_pipeline_composition.cpp`, quando motion blur è attivo, gli N samples vengono valutati e renderizzati **sequenzialmente**, poi accumulati in loop `for (int y... for (int x...)` pixel-per-pixel con accessi `accum[idx]`.

**Soluzione:** (a) Parallelizzare gli N samples con `tbb::parallel_for`. (b) SIMD-izzare l'accumulazione floating-point con Highway. (c) Ri-usare framebuffer dal pool invece di crearli ogni sample.

**Dove:** `src/render_graph/render_pipeline_composition.cpp` — sezione motion blur ~righe 80-120.

**Guadagno stimato:** 4-8× speedup su CPU multi-core con 8 samples.

**Codice attuale (da parallelizzare):**
```cpp
// Loop seriale su N samples
for (int s = 0; s < N; ++s) {
    Scene sub = comp.evaluate(frame, t);
    const Framebuffer& sub_fb = *call_graph(sub, frame, t);
    for (int y = 0; y < rh; ++y) {
        for (int x = 0; x < rw; ++x) {
            const Color c = sub_fb.get_pixel(x, y);
            accum[idx + 0] += c.r * weight;
            accum[idx + 1] += c.g * weight;
            accum[idx + 2] += c.b * weight;
            accum[idx + 3] += c.a * weight;
        }
    }
}
```

**Prossimi passi:**
- [ ] Parallelizzare il loop `for (int s = 0; s < N; ++s)` con `tbb::parallel_for`
- [ ] Sostituire `get_pixel` con accesso diretto a righe (puntatori raw)
- [ ] SIMD-izzare l'accumulazione con Highway (4 lane float × 4 canali = 16 lane)
- [ ] Ri-usare framebuffer intermedio dal pool invece di crearne uno per sample

---

### N2. Box Blur 3-Pass Parallelizzato

**Problema:** In `effect_blur.cpp`, i 3 passaggi di blur box-filter (orizzontale → verticale → orizzontale) sono completamente seriali. Ogni riga/colonna è processata una alla volta.

**Soluzione:** Parallelizzare ogni pass con `tbb::parallel_for` per righe separate. Inoltre, fondere i 3 passaggi in 2 con kernel più grandi (2-pass separable).

**Dove:** `src/backends/software/utils/effects/effect_blur.cpp`.

**Guadagno stimato:** 4-8× speedup su raggi grandi (50+) e risoluzioni 4K.

**Codice attuale:**
```cpp
for (int pass = 0; pass < 3; ++pass) {
    for (i32 y = y0; y < y1; ++y) {     // ← seriale!
        // processing riga orizzontale
    }
    for (i32 x = x0; x < x1; ++x) {     // ← seriale!
        // processing colonna verticale
    }
}
```

**Prossimi passi:**
- [ ] Avvolgere i loop di riga con `tbb::parallel_for(tbb::blocked_range<i32>(y0, y1), ...)`
- [ ] Avvolgere i loop di colonna con `tbb::parallel_for(tbb::blocked_range<i32>(x0, x1), ...)`
- [ ] Valutare fusione 3-pass → 2-pass con kernel di raggio maggiore
- [ ] Aggiungere benchmark dedicato blur per misurare speedup

---

### N3. Downsample SSAA Parallel + Raw Pointer Access

**Problema:** `downsample_fb()` in `render_pipeline_helpers.hpp` chiama `src.get_pixel(ix, iy)` e `dst->set_pixel(x, y, ...)` per ogni pixel — overhead di function call e bounds check. Loop interamente seriale.

**Soluzione:** (a) Accesso diretto alle row con puntatori raw. (b) Parallelizzare con TBB. (c) SIMD-izzare il box filter.

**Dove:** `src/render_graph/render_pipeline_helpers.hpp` — funzione `downsample_fb()`.

**Guadagno stimato:** 2-4× speedup con SSAA 2× attivo.

**Prossimi passi:**
- [ ] Sostituire `get_pixel`/`set_pixel` con `pixels_row()` raw pointer
- [ ] Parallelizzare il loop `for (int y = 0; y < dst_h; ++y)` con TBB
- [ ] SIMD-izzare il box filter con Highway (accumulazione 4-lane)

---

### N4. std::any_cast Chain → Enum Dispatch

**Problema:** In `effect_stack.cpp`, ogni effetto fa `std::any_cast<BlurParams>(&inst.params)` poi `std::any_cast<TintParams>`, ecc. in cascata — overhead O(n) di type-erasure per ogni effetto.

**Soluzione:** Aggiungere un enum `EffectType` a `EffectInstance` per dispatch O(1) con switch invece di O(n) any_cast.

**Dove:** `include/chronon3d/effects/effect_instance.hpp` + `src/backends/software/utils/effects/effect_stack.cpp`.

**Guadagno stimato:** Overhead eliminato per ogni effetto applicato. Minore, ma si accumula su layer con 4-5 effetti.

**Prossimi passi:**
- [ ] Aggiungere `enum class EffectType { Blur, Tint, Brightness, Contrast, Glow, DropShadow, Bloom, Fake3DWave };` a `EffectInstance`
- [ ] Popolare l'enum al construction time
- [ ] Sostituire la catena di `any_cast` con uno switch sull'enum
- [ ] Mantenere `std::any` per i parametri (non rimuovere, solo ottimizzare il dispatch)

---

### N5. compute_scene_fingerprint — Hash Costoso per Frame

**Problema:** `compute_scene_fingerprint()` in `render_pipeline_helpers.hpp` hash manualmente ~50+ valori per layer usando XOR + 0x9e3779b97f4a7c15. Per 100 layer: migliaia di operazioni di hash per frame.

**Soluzione:** (a) Usare XXH3 (già presente nel progetto) invece della combinazione manuale. (b) Hash incrementale — solo ciò che cambia. (c) Cache dell'hash quando la scena è statica.

**Dove:** `src/render_graph/render_pipeline_helpers.hpp` — funzione `compute_scene_fingerprint()`.

**Guadagno stimato:** 2-3× più veloce, hash più robusto con meno collisioni.

**Prossimi passi:**
- [ ] Sostituire `combine()` manuale con `XXH3_64bits_update()`
- [ ] Mantenere una versione cached dello fingerprint tra frame
- [ ] Se lo scene state non è cambiato, riusare l'hash precedente

---

### N6. Blend Mode Switch Per-Pixel

**Problema:** In `blend_mode.hpp`, `compositor::blend()` ha uno switch su `BlendMode` per ogni pixel. Per blend non-Normal, ogni pixel paga un branch indiretto.

**Soluzione:** Template specialization per blend mode comune (Normal), function pointer o jump table per dispatch più veloce.

**Dove:** `include/chronon3d/compositor/blend_mode.hpp`.

**Guadagno stimato:** Minore (~1-2%), ma si applica a TUTTI i pixel blendati.

**Prossimi passi:**
- [ ] Creare template `blend<BlendMode>(src, dst)` per dispatch compile-time
- [ ] Nei loop hot, chiamare `blend<BlendMode::Normal>` che è senza branch
- [ ] Per blend mode variabili, usare function pointer array invece di switch

---

### N7. Shadow/Glow Multi-Layer Fusione

**Problema:** `draw_shadow()` e `draw_glow()` in `effect_stack.cpp` disegnano 5-6 layer separati con spread progressivo. Ognuno chiama `draw_transformed_shape()` separatamente — draw calls ridondanti.

**Soluzione:** Fusionare in una singola passata con accumulazione. Invece di 6 draw separate, una sola con blending cumulativo.

**Dove:** `src/backends/software/utils/effects/effect_stack.cpp` — funzioni `draw_shadow()`, `draw_glow()`.

**Guadagno stimato:** ~5-6× meno draw calls per nodo con shadow/glow.

**Codice attuale:**
```cpp
for (int i = LAYERS; i >= 1; --i) {
    // Una draw separata per ogni layer — 6 chiamate!
    draw_transformed_shape(fb, node.shape, shadow_model, {base.r, base.g, base.b, alpha}, spread, &state);
}
```

**Prossimi passi:**
- [ ] Creare `draw_shadow_fused()` che accumula alpha in un buffer temporaneo
- [ ] Unico `draw_transformed_shape()` con alpha pre-calcolato
- [ ] Benchmark con `perf stat` per misurare la riduzione di draw calls

---

### N8. Acquire Temp Framebuffer — Overhead Aliasing shared_ptr

**Problema:** `acquire_temp_framebuffer()` in `effects_internal.hpp` crea `shared_ptr<FramebufferPool>(pool, [](auto*){})` — aliasing constructor con deleter vuoto. Ogni frame temporaneo paga questo overhead.

**Soluzione:** Pool dedicato per temporanei con bump allocator + release esplicita a fine effetto.

**Dove:** `src/backends/software/utils/effects/effects_internal.hpp` + `include/chronon3d/cache/framebuffer_pool.hpp`.

**Guadagno stimato:** Riduce overhead allocazione per frame con molti effetti.

**Prossimi passi:**
- [ ] Creare `TempFramebufferArena` — bump allocator con release a fine scope
- [ ] Sostituire `shared_ptr` con `unique_ptr` dove possibile
- [ ] Tracciare `framebuffer_reuses` counter per i temp buffer

---

### N9. Trace Events — Lock-Free Queue + Interned Strings

**Problema:** `RenderTrace::add()` prende `std::mutex` e costruisce `std::string` per ogni evento trace. Con 100+ nodi per frame, overhead misurabile.

**Soluzione:** Usare lock-free SPSC queue per-thread + interned string IDs. Merge a fine frame.

**Dove:** `include/chronon3d/core/trace.hpp` + `src/core/trace.cpp`.

**Guadagno stimato:** Riduce overhead tracing, specialmente in debug/diagnostic mode.

**Prossimi passi:**
- [ ] Creare `ThreadLocalTraceBuffer` con lock-free queue
- [ ] Usare `string_view` + interned pool invece di `std::string`
- [ ] Merge dei buffer per-thread in un unico `RenderTrace` a fine frame

---

### N10. Thread-Local Ptrs con RAII Guard

**Problema:** `profiling::g_current_trace`, `g_current_frame`, `g_current_counters`, `g_current_framebuffer_pool` sono settati e resettati manualmente in ogni chiamata render in `software_renderer.cpp`. Se un'eccezione viene lanciata, i ptrs restano sporchi.

**Soluzione:** RAII guard (`ScopeGuard`) per garantire reset automatico anche in caso di eccezioni.

**Dove:** `src/backends/software/software_renderer.cpp` + `src/render_graph/render_pipeline_composition.cpp`.

**Guadagno stimato:** Robustezza, prevenzione di dangling pointer post-eccezione.

**Prossimi passi:**
- [ ] Creare `ProfilingGuard` che setta e resetta automaticamente i thread_local ptrs
- [ ] Sostituire assegnamenti manuali con guard RAII
- [ ] Test con eccezione artificiale per verificare il cleanup

---

### N11. Bug: Double Registration ShapeType::Path

**Problema:** In `builtin_processors.cpp`, `create_shape_processor()` e `create_path_processor()` sono entrambi registrati per `ShapeType::Path`. La seconda sovrascrive la prima.

```cpp
registry.register_shape(ShapeType::Path, create_shape_processor());     // sovrascritto
registry.register_shape(ShapeType::Path, create_path_processor());     // vince questo
```

**Soluzione:** Rimuovere la registrazione duplicata.

**Dove:** `src/backends/software/processors/builtin_processors.cpp` riga ~21.

**Guadagno stimato:** Manutenibilità, zero overhead di registrazione sprecata. 5 minuti di fix.

✅ **STATO: COMPLETATO** — Registrazione duplicata `create_shape_processor()` per `ShapeType::Path` rimossa. Rimasta solo la chiamata a `create_path_processor()`.

---

### N12. Path Flatten Cache Assente

**Problema:** Non esiste cache per path flattening. Ogni path SVG viene ricalcolato da zero ogni frame, anche se identico al frame precedente.

**Soluzione:** Cache basata su hash del path data + transform. Usare `LruCache` (pattern già esistente).

**Dove:** `src/backends/software/rasterizers/path_rasterizer.cpp`.

**Guadagno stimato:** Skip ricalcolo path per path statici tra frame consecutivi.

✅ **STATO: COMPLETATO** — Implementatione unificata con S5: `path_cache.hpp` con `LruCache` + `flatten_path_cached()` in `path_rasterizer.cpp`.

**Prossimi passi:**
- [ ] Definire `PathFlattenCacheKey` con hash di path data + transform
- [ ] Integrare con `LruCache<Key, shared_ptr<const vector<Contour>>>`
- [ ] Invalutare quando path data cambia

---

### N13. Layout Solver Minimalista

**Problema:** Il layout solver supporta solo pin-to-anchor e safe area. Nessun flexbox, flow, o auto-layout per testi multi-linea e griglie.

**Soluzione:** Implementare layout flow per composizioni di testo e griglie semplici.

**Dove:** `src/layout/layout_solver.cpp` + `include/chronon3d/layout/layout_rules.hpp`.

**Guadagno stimato:** Feature richiesta per composizioni complesse. Dipende dall'uso.

**Prossimi passi:**
- [ ] Aggiungere `LayoutFlow` — disposizione in riga con wrap
- [ ] Aggiungere `LayoutGrid` — griglia di celle uniformi
- [ ] Integrare con `LayoutSolver::solve()`

---

### N14. Benchmark Mancanti per Hot-Path

**Problema:** I benchmark esistenti (`tests/perf/`) non coprono blur, effect stack, compositing puro, o motion blur. Le ottimizzazioni proposte non hanno metriche di baseline.

**Soluzione:** Aggiungere benchmark specifici per blur (vari raggi), compositing blend modes, effect stack (blur+tint+glow), e motion blur (vari sample count).

**Dove:** `tests/perf/` — nuovo file `test_hotpath_benchmarks.cpp`.

**Guadagno stimato:** Baseline per guidare e validare le ottimizzazioni proposte.

✅ **STATO: COMPLETATO** — 3 benchmark hot-path aggiunti a `tests/bench/micro_benchmarks.cpp`:
- Blur performance (raggio 10, 50, 100) — 640×360
- Compositing normal blend — 640×360
- Motion blur accumulation (4, 8, 16 samples) — 320×180

**Prossimi passi:**
- [ ] Creare benchmark per `apply_blur()` con raggio 10, 50, 100
- [ ] Creare benchmark per `compositor::blend()` su tutti i blend mode
- [ ] Creare benchmark per motion blur con 4, 8, 16 samples
- [ ] Aggiungere al CTest con tag `perf`

---

### N15. Framebuffer Pool Adaptive Preallocation (dati telemetry)

**Problema:** Ora che abbiamo `framebuffer_acquire_ms`, `framebuffer_clear_ms`, `framebuffer_enqueue_ms`, e i miss reason counters, possiamo vedere quanto tempo si perde nel pool, ma il pool non si adatta dinamicamente.

**Soluzione:** Usare i nuovi contatori telemetry per adattare automaticamente il pool:
- Se `framebuffer_acquire_ms` > 5ms → preallocare più framebuffer al prossimo frame
- Se `pool_miss_count_empty` > 0 dopo 3 frame consecutivi → aumentare pool_size del 50%
- Se `buffer_returned_to_pool_count` ≈ `framebuffer_allocations` → ownership funziona, pool sta funzionando
- Se `buffer_returned_to_pool_count` ≈ 0 → ownership bug, loggare warning

**Dove:** `src/cache/framebuffer_pool.cpp` — `acquire()`, `release()`, nuovo `adapt_pool()` chiamato a inizio frame.

**Guadagno stimato:** Pool self-tuning — niente più pool vuoti in produzione, zero configurazione manuale.

**Prossimi passi:**
- [ ] Leggere i nuovi counter telemetry a inizio frame
- [ ] Implementare `FramebufferPool::adapt_pool(const RenderCounters&)` che decide se preallocare
- [ ] Aggiungere soglia minima per evitare oscillazioni (isteresi)
- [ ] Loggare decisioni di resize via spdlog

---

### N16. Zero-Copy Frame Delivery all'Encoder

**Problema:** `frame_conversion_copy_ms` ci mostra quanto tempo si perde in conversioni/copie prima di mandare il frame a FFmpeg. Con `framebuffer_acquire_ms` e `framebuffer_returned_to_pool_count` possiamo tracciare se i buffer vengono riutilizzati o distrutti.

**Soluzione:** Eliminare copie intermedie:
- Invece di copiare il framebuffer renderizzato in un buffer YUV separato, fornire direttamente i pixel RGBA via mmap o pointer sharing
- Usare `av_frame_get_buffer()` con buffer registrato (registered buffers in io_uring) per scrivere direttamente nella memoria dell'encoder
- Se il framebuffer è già in pool e non modificato, passare il puntatore raw senza copia

**Dove:** `apps/chronon3d_cli/utils/ffmpeg_pipe_encoder.cpp` + `include/chronon3d/core/framebuffer.hpp`.

**Guadagno stimato:** -30-50% di `frame_conversion_copy_ms`, risparmio di banda memoria.

**Prossimi passi:**
- [ ] Misurare baseline con `frame_conversion_copy_ms` in telemetry
- [ ] Implementare `av_frame_from_framebuffer()` che crea un AVFrame senza copiare i dati
- [ ] Integrare con io_uring registered buffers per scrivere direttamente
- [ ] Benchmark: confrontare frame_conversion_copy_ms prima/dopo

---

### N17. Pool Miss Reason Dashboard

**Problema:** `framebuffer_pool_miss_count_size_mismatch` e `framebuffer_pool_miss_count_empty` esistono nei counter ma non c'è una dashboard che mostra la distribuzione dei miss reason, né un alert automatico quando un tipo domina.

**Soluzione:** Aggiungere al frontend React:
- Grafico a torta o bar chart della distribuzione miss reason
- Line chart temporale dei miss reason nel tempo (per frame)
- Badge di alert rosso se `pool_miss_count_empty` > 50% dei miss totali

**Dove:** `tools/telemetry_dashboard/frontend/src/components/MetricsGrid.jsx` + `PerformanceCharts.jsx`.

**Guadagno stimato:** Visibilità immediata sul collo di bottiglia del pool — size mismatch vs pool vuoto richiedono soluzioni diverse.

**Prossimi passi:**
- [ ] Leggere entrambi i miss counter dal run detail
- [ ] Calcolare ratio: size_mismatch / (size_mismatch + empty)
- [ ] Aggiungere bar chart nella sezione Framebuffer Pipeline Diagnostics
- [ ] Alert automatico quando empty domina

---

## 📋 RIEPILOGO MATRICE

| ID | Improvement | Quando | Complessità | Impatto | Stato |
|----|------------|--------|-------------|---------|-------|
| I1 | Bake grid EXR + mmap | Oggi | 🟢 Bassa | 🔴 Alto | ✅ Fatto |
| I2 | Huge pages | Oggi | 🟢 Bassa | 🔴 Alto | ✅ Fatto |
| I3 | Dirty rect bitmask | Oggi | 🟢 Bassa | 🔴 Alto | ✅ Fatto |
| I4 | Thread affinity + NUMA | Questa settimana | 🟢 Bassa | 🟡 Medio | Da fare |
| I5 | FrameArena nel render pipeline | Oggi | 🟢 Bassa | 🟡 Medio | ✅ Fatto |
| I6 | PersistentDisk cache (disk) | Oggi | 🟡 Media | 🔴 Alto | ✅ Fatto |
| I7 | Unificare hash_string | Oggi | 🟢 Bassa | 🟢 Basso | ✅ Fatto |
| I8 | Ridurre boilerplate counters | Oggi | 🟢 Bassa | 🟢 Basso | ✅ Fatto |
| S1 | io_uring pipe | Questa settimana | 🟡 Media | 🟡 Medio | ✅ Fatto |
| S2 | Temporal hashing | Questa settimana | 🟡 Media | 🔴 Alto | Da fare |
| S3 | L1/L2 prefetch | Questa settimana | 🟢 Bassa | 🟡 Medio | Da fare |
| S4 | OpenEXR DWAA bake | Questa settimana | 🟡 Media | 🟡 Medio | ✅ Fatto |
| S5 | Path flatten cache sharded | Questa settimana | 🟢 Bassa | 🟡 Medio | ✅ Fatto |
| S6 | SIMD point-in-polygon | Questa settimana | 🟡 Media | 🟡 Medio | Da fare |
| S7 | Eliminare shared_ptr nel hot path | Questa settimana | 🟡 Media | 🔴 Alto | Da fare |
| S8 | RenderCounters thread-local | Questa settimana | 🟢 Bassa | 🟡 Medio | ✅ Fatto |
| S9 | ImageCache sharding | Questa settimana | 🟢 Bassa | 🟡 Medio | Da fare |
| S10 | SIMD alpha premultiply | Questa settimana | 🟢 Bassa | 🟡 Medio | ✅ Fatto |
| M1 | Graph compiler | Questo mese | 🔴 Alta | 🔴 Alto | Da fare |
| M2 | ISPC blur | Questo mese | 🔴 Alta | 🟡 Medio | Da fare |
| M3 | SPSC lock-free queue | Questo mese | 🟡 Media | 🟡 Medio | Da fare |
| M4 | Speculative render | Questo mese | 🔴 Alta | 🔴 Alto | Da fare |
| M5 | Transform cache | Questo mese | 🟡 Media | 🟡 Medio | Da fare |
| M6 | ImageCache LRU | Questo mese | 🟢 Bassa | 🟡 Medio | ✅ Fatto |
| M7 | Cache telemetry nel report | Questo mese | 🟢 Bassa | 🟡 Medio | Da fare |
| M8 | CI clang-tidy | Questo mese | 🟢 Bassa | 🟡 Medio | ✅ Fatto |
| M9 | CancellationToken | Questo mese | 🟡 Media | 🟡 Medio | ✅ Fatto |
| M10 | CLI --dry-run | Questo mese | 🟢 Bassa | 🟡 Medio | ✅ Fatto |
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
| N1 | Motion blur parallel + SIMD | Questa settimana | 🟡 Media | 🔴 Alto | Da fare |
| N2 | Box blur 3-pass parallel | Questa settimana | 🟡 Media | 🔴 Alto | Da fare |
| N3 | Downsample SSAA parallel | Questa settimana | 🟢 Bassa | 🟡 Medio | Da fare |
| N4 | any_cast chain → enum dispatch | Oggi | 🟢 Bassa | 🟢 Basso | Da fare |
| N5 | compute_scene_fingerprint hash | Questa settimana | 🟢 Bassa | 🟡 Medio | Da fare |
| N6 | Blend mode switch per-pixel | Oggi | 🟢 Bassa | 🟢 Basso | Da fare |
| N7 | Shadow/Glow multi-layer fused | Questa settimana | 🟡 Media | 🟡 Medio | Da fare |
| N8 | Temp FB aliasing shared_ptr | Oggi | 🟢 Bassa | 🟢 Basso | Da fare |
| N9 | Trace lock-free queue | Oggi | 🟡 Media | 🟢 Basso | Da fare |
| N10 | RAII guard thread_local ptrs | Oggi | 🟢 Bassa | 🟢 Basso | Da fare |
|| N11 | Fix double registration Path | Oggi | 🟢 Bassa | 🟢 Basso | ✅ Fatto |
|| N12 | Path flatten cache | Questa settimana | 🟡 Media | 🟡 Medio | ✅ Fatto |
|| N13 | Layout solver esteso | Questo mese | 🟡 Media | 🟢 Basso | Da fare |
|| N14 | Benchmark hot-path mancanti | Questa settimana | 🟢 Bassa | 🟡 Medio | ✅ Fatto |
| N15 | FB pool adaptive preallocation | Questa settimana | 🟡 Media | 🟡 Medio | 🟡 Counters live |
| N16 | Zero-copy frame delivery encoder | Questa settimana | 🟡 Media | 🟡 Medio | 🟡 Counters live |
| N17 | Pool miss reason dashboard | Oggi | 🟢 Bassa | 🟢 Basso | 🟡 Counters live |
| **I1** | **Render Graph Incrementale** | Questo mese | 🔴 Alta | 🔴 Alto | **Nuovo — il game changer** |
| **I2** | **SoA Framebuffer** | Questo mese | 🟡 Media | 🟡 Medio | **Nuovo** |
| **I3** | **Wavefront Scheduling** | Questo mese | 🟡 Media | 🟡 Medio | **Nuovo** |
| **I4** | **Direct Float→YUV Encoding** | Questa settimana | 🟢 Bassa | 🔴 Alto | **Nuovo — quick win** |
| **I5** | **Per-Frame Arena senza Pool** | Questa settimana | 🟡 Media | 🔴 Alto | **Nuovo** |
| **I6** | **Procedural Grid Kernel** | Oggi | 🟢 Bassa | 🔴 Alto | **Nuovo — quick win** |
| **I7** | **Pre-bake mmap** | Oggi | 🟢 Bassa | 🔴 Alto | **Nuovo — quick win** |

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
| **FramebufferPool preallocato** | `preallocate()` con 3 risoluzioni, stats, touch_memory |
| **Framebuffer con HugePages** | `memory_utils.hpp` — `HugePageAllocator<Color>` in `Framebuffer::m_pixels` |
| **Renderer warmup** | `warmup_renderer()` — dummy frame + prealloc |
| **Color space pipeline** | `color_space.hpp` — sRGB↔Linear IEC 61966-2-1 |
| **YUV420P/NV12 in pipe** | `ffmpeg_pipe_encoder.cpp` — BT.601 coeffs, default yuv420p |
| **Highway SIMD kernels** | `highway_kernels.cpp` — composite_normal_premul |
| **Effect stack splittato** | `effects/` dir — blur, color, wave, stack |
| **Blend2D JIT bridge** | `blend2d_bridge.cpp` — rasterizzazione JIT |
| **EXR mmap loader** | `exr_mmap.cpp` — `load_exr_mmap()` con OpenEXR tiled + mmap |
| **EXR DWAA writer** | `image_writer.cpp` — tiled writes 256×256 con compressione DWAA |
| **HugePageAllocator** | `memory_utils.hpp` — `allocate_huge_pages()`, `HugePageAllocator<T>`, usato in Framebuffer |
| **DirtyRectMask** | `dirty_rect_mask.hpp` — bitmask compact con tiles 64×64, mark_dirty/is_dirty |
| **SIMD premultiply alpha** | `highway_kernels.cpp` — `premultiply_alpha_rgba8()` via Highway |
| **FrameArena in GraphExecutor** | `graph_executor.hpp` + `graph_executor_phases.cpp` — `m_frame_arena` con reset RAII |
| **DiskNodeCache** | `disk_node_cache.hpp/.cpp` — cache su disco con mmap + atomic rename |
| **SIMD Rect Rasterizer** | `highway_kernels.cpp` — `rasterize_rect_simd()` via Highway |
| **X-macro RenderCounters** | `counters.hpp` — `CHRONON_RENDER_COUNTERS(X)` riduce boilerplate |
| **Framebuffer Pipeline Diagnostics (7 counters)** | `counters.hpp` — acquire_ms, clear_ms, enqueue_ms, pool_miss_size, pool_miss_empty, buffer_returned_count, conversion_copy_ms. Full pipeline C++ → DB → React frontend |
| **React Telemetry Dashboard — FB Pipeline Cards** | `MetricsGrid.jsx` — 6 nuove metric cards nella sezione "Framebuffer Pipeline Diagnostics" con codifica colore e tooltip descrittivi |
| **ffmpeg pipe writer cleanup** | `video_export_pipe.cpp` — queue con flag atomici, error handling uniforme, notify_one invece di notify_all |
| **Root directory cleanup** | `ARCHITECTURE.md`, `BUILDING.md`, `IMPROVEMENTS.md`, `ORIENTATION.md`, `AGENTS.md` → `docs/`. Script → `tools/`. Log/trace/mp4 eliminati. .gitignore aggiornato |
| **LilDirkClean removal** | Test file eliminato, riferimenti rimossi da docs e test preset |
| **LICENSE + CONTRIBUTING.md** | `LICENSE` (MIT), `CONTRIBUTING.md` — guida per contributori |
| **.clang-tidy + CI job** | `.clang-tidy` (18 categorie) + job `clang-tidy` in `.github/workflows/ci.yml` |
| **CancellationToken** | `cancellation_token.hpp/.cpp` — `is_cancelled()`, `cancel()`, `reset()` + handler SIGINT/SIGTERM |
| **CLI --dry-run** | `command_video.cpp` — flag `--dry-run` per validazione pre-render |
| **PathFlattenCache con LruCache** | `path_cache.hpp` — 16 shard, 64 MB, usato in `path_rasterizer.cpp` via `flatten_path_cached()` |
| **RenderCountersRaw + merge_tls()** | `counters.hpp` — struct POD non-atomic + merge in passaggio singolo |
| **Hot-path benchmarks (N14)** | `tests/bench/micro_benchmarks.cpp` — blur, compositing, motion blur |
| **Fix double registration Path (N11)** | `builtin_processors.cpp` — rimossa registrazione duplicata per `ShapeType::Path` |

---

## 🎯 Priorità Raccomandata

| Completato ✅ (questa iterazione)
- **I1** — Bake EXR mmap per background
- **I2** — Huge Pages per FramebufferPool
- **I3** — Dirty Rect Bitmask per Compositing
- **I5** — FrameArena nel Render Pipeline
- **I6** — DiskNodeCache per nodi statici
- **I7** — Unificazione hash_string
- **I8** — Riduzione boilerplate RenderCounters
- **S1** — io_uring pipe FFmpeg
- **S4** — OpenEXR DWAA bake
- **S5** — PathFlattenCache con LruCache
- **S8** — RenderCountersRaw + merge_tls()
- **S10** — SIMD Alpha Premultiplication in ImageCache
- **M6** — ImageCache LRU con Memory Budget
- **M8** — .clang-tidy + CI static analysis
- **M9** — CancellationToken + SIGINT handler
- **M10** — CLI --dry-run
- **N11** — Fix double registration Path
- **N12** — Path flatten cache
- **N14** — Hot-path benchmarks (blur, compositing, motion blur)
- **A1** — Ottimizzazione Umbrella Headers
- **A2** — Decoupling RenderNode shape state
- **C1** — SIMD Rect Rasterizer
- **E1** — Unificazione Effect System std::any

### Prossima priorità

**Singola cosa da fare oggi:** **I6 (Procedural Grid Kernel)** — kernel SIMD dedicato per la griglia, 25× speedup, 1-2 giorni, zero modifiche architetturali. Poi **I4 (Direct Float→YUV)** — elimina 8MB alloc/frame, 1 giorno.

**3 cose da fare questa settimana:**
1. **I6 (Procedural Grid Kernel)** — 25× speedup sulla grid background, quick win
2. **I4 (Direct Float→YUV)** — 2-3× speedup encoding, elimina heap alloc
3. **I5 (Per-Frame Arena)** — elimina 226 pool miss + mutex overhead

**Quick win di oggi:** **I6 + I4** — 2 giorni totali, impatto massimo. **N10 (RAII guard thread_local)** — 15 minuti. **N4 (any_cast → enum dispatch)** — 20 minuti.

**Il game changer a lungo termine:** **I1 (Render Graph Incrementale)** — 3-5× su composizioni statiche. È come passare da "ricalcolare tutto" a "ricalcolare solo ciò che cambia".

**La più divertente:** **R5 (LOD 2.5D)** — primo motore 2.5D con Level-of-Detail per layer, impatto 2-16×.
**Sezione diagnostica: usa i nuovi counter per ottimizzare:** ora che abbiamo `framebuffer_acquire_ms`, `framebuffer_clear_ms`, `framebuffer_enqueue_ms`, `frame_conversion_copy_ms`, e i miss reason counters, smettiamo di tirare a indovinare: vediamo esattamente dove si perde tempo nel framebuffer pipeline.

**Nuove opportunità dalla diagnostica framebuffer pipeline (maggio 2026) — telemetry counters C++ → DB → React già live:**
- **N15** (FB pool adaptive preallocation) — usa `framebuffer_acquire_ms`/`pool_miss_count_empty` per self-tuning del pool. Contatori live ✅, logica adaptive `adapt_pool()` da implementare.
- **N16** (Zero-copy frame delivery encoder) — usa `frame_conversion_copy_ms` come baseline. Contatore live ✅, zero-copy `av_frame_from_framebuffer()` da implementare.
- **N17** (Pool miss reason dashboard) — counter distribuzione miss reason live nel frontend React ✅, grafico a barre/alert automatico da aggiungere a `PerformanceCharts.jsx`.

---

## 🚀 V3 — BLUEPRINT: DA FRAME-BASED A TILE-BASED (MASSIMO THROUGHPUT)

> Questa sezione descrive la trasformazione architetturale V3: un motore tile-first, con nodi procedurali specializzati, cache persistente per regione, e pipeline di output separata.
> 
> **TL;DR:** Il motore attuale è frame-based. Ogni nodo opera su framebuffer full-frame. Con V3, ogni nodo produce lavoro per tile/regione, non per canvas intero. Il compositing diventa merge di tile attivi. I pattern procedurali diventano kernel dedicati. La cache è per-tile, non per-nodo.
> 
> **Per Chronon3D nello specifico:** `GridCleanBackground` non dovrebbe più passare da `bl2d_blimage_tiled.cpp` (tiled image bridge generico). Dovrebbe essere un nodo procedurale "grid background" con fase/offset camera, update solo strip cambiate, precompute della griglia per tile, e compositing quasi nullo.

---

### Pillar 1 — Tile-First Architecture (non Node-First)

**Problema:** L'esecutore (`GraphExecutor::execute()` in `graph_executor_phases.cpp`) opera per livelli del DAG. Ogni nodo produce/consuma `shared_ptr<Framebuffer>` full-frame. Anche se il dirty rect limita il clipping a una regione, il framebuffer è sempre allocato per l'intera canvas — e ogni nodo intermediario scrive pixel ovunque.

**Soluzione:** Il motore deve ragionare per tile dall'inizio. Ogni `RenderGraphNode::execute()` riceve non un singolo framebuffer, ma una griglia di tile. Ogni tile è un framebuffer piccolo (es. 256×256) che vive nel pool.

**Dove:**
- `include/chronon3d/render_graph/render_graph_node.hpp` — Aggiungere overload `execute()` che accetta `TileGrid&`. Tenere la vecchia signature `execute()` per retrocompatibilità durante la migrazione.
- `src/render_graph/executor/graph_executor_phases.cpp` — `execute_single_node()` con tile scheduler
- `include/chronon3d/render_graph/render_graph.hpp` — Nuovo `TileGrid` runtime struct (vedi sotto)

**Strategia di migrazione graduale:**
- Non riscrivere tutti i 12+ tipi di nodo in una volta
- Aggiungere `LegacyNodeAdapter`: un wrapper che estrae un singolo `shared_ptr<Framebuffer>` da `TileGrid` per il tile specifico e lo passa al vecchio `execute()`. Questo permette di avere nodi tile-aware e nodi legacy che coesistono nello stesso grafo
- Ogni nodo viene migrato individualmente: `SourceNode` primo (semplice), poi `CompositeNode`, poi `EffectNode`, ecc.
- `LegacyNodeAdapter` viene rimosso quando tutti i nodi sono tile-aware

**Struttura:**
```cpp
struct TileId { int tx, ty; };

struct TileGrid {
    int tiles_x, tiles_y;
    int tile_w, tile_h;        // 256×256 default
    std::unordered_map<TileId, std::shared_ptr<Framebuffer>> tiles;
    
    // Accesso lazy: alloca il tile solo se serve
    std::shared_ptr<Framebuffer> acquire_tile(TileId id, cache::FramebufferPool& pool);
    
    // Merge nel framebuffer full-frame finale
    void merge_to(Framebuffer& dst, const std::optional<raster::BBox>& clip);
};
```

**Guadagno stimato:** 
- Memoria: framebuffer intermedi 256×256 invece di 1920×1080 → -98% bandwidth per tile non toccati
- Parallelismo: worker pool può processare tile indipendenti in parallelo
- Cache: tile statici non vengono mai ri-renderizzati

**Prossimi passi:**
- [ ] Definire `TileGrid` struct in `render_graph.hpp`
- [ ] Modificare `RenderGraphNode::execute()` per operare su `TileGrid&` invece di `shared_ptr<Framebuffer>`
- [ ] Aggiornare il `GraphExecutor` per schedulare tile per livello
- [ ] Aggiungere `TilePool` — pool di framebuffer tile-size con bump allocator
- [ ] Benchmark: throughput con tile 64×64, 128×128, 256×256

---

### Pillar 2 — Display List Compilation (Scene Compile-Time / Runtime Split)

**Problema:** La scena viene riconvertita ogni frame da `Scene → RenderGraphContext → RenderGraph` via `GraphBuilder::build()` e `detail::resolve_layers()`. Per 100 layer con 5 effetti ciascuno, questo produce 500+ chiamate a `add_node()` e `connect()` ogni frame — overhead misurabile di allocazione vettori, hash, e costruzione `std::unique_ptr<RenderGraphNode>`.

**Soluzione:** La scena viene compilata UNA VOLTA in una `CompiledDisplayList` — una rappresentazione runtime compatta e immutabile per composizione/frame-class. L'`Evaluator` produce solo parametri aggiornati (posizioni, opacità, frame time), non la struttura del grafo.

**Dove:**
- `include/chronon3d/render_graph/display_list.hpp` (nuovo) — `CompiledDisplayList` con IR immutabile
- `src/render_graph/display_list_compiler.cpp` (nuovo) — Compila `Scene` → `CompiledDisplayList`
- `src/render_graph/executor/graph_executor_phases.cpp` — Nuovo path: se `CompiledDisplayList` esiste, usa quello invece di build
- `src/runtime/scene_to_render_graph.cpp` — Punto di integrazione

**IR immutabile:**
```cpp
struct CompiledDisplayList {
    // Layout di memoria contiguo, nessuna allocazione dinamica a runtime
    std::span<const CompiledNode> nodes;
    std::span<const CompiledEdge> edges;
    std::span<const u32> levels;       // execution levels flattened
    std::span<const TileAffinity> tile_affinity;  // quali tile tocca ogni nodo
    
    // Metadata
    int width, height;
    int tile_size;
    std::span<const std::string_view> node_names;  // interned
};
```

**Come funziona:**
1. Al primo frame o quando la struttura della scena cambia: `Compiler::compile(scene) → CompiledDisplayList`
2. Ai frame successivi: `Executor::execute(compiled_list, params_delta)` — solo evaluate parametri cambiati
3. Se un layer viene aggiunto/rimosso: ricompila (raro — una volta ogni N frame)

**Guadagno stimato:**
- Build del grafo: da 0.5-2ms a ~0ms (solo evaluate delta params)
- Cache: i nodi invariati non vengono mai re-hashati
- Prevedibilità: il piano di esecuzione è noto staticamente

**Prossimi passi:**
- [ ] Definire `CompiledDisplayList` e `CompiledNode` / `CompiledEdge` struct
- [ ] Implementare `DisplayListCompiler` che analizza `Scene` e genera flat array
- [ ] In `GraphExecutor`, aggiungere `std::optional<CompiledDisplayList> m_compiled`
- [ ] Aggiungere `invalidate_compiled()` quando la scena cambia struttura
- [ ] Benchmark: misurare build_ms prima/dopo

---

### Pillar 3 — Tile Mask Invalidation (non dirty rect)

**Problema:** Il dirty rect (`render_pipeline_scene.cpp`, `DirtyRectMask` in `dirty_rect_mask.hpp`) è una bounding box unica per tutto il frame. Se un piccolo elemento si muove in basso a destra e un altro in alto a sinistra, l'unione copre quasi tutto il frame — anche se solo 2 tile sono effettivamente cambiati.

**Nota:** `DirtyRectMask` esiste già in `dirty_rect_mask.hpp` con tile 64×64 e `std::bitset<1024>`. Ma `k_max_tiles = 1024` supporta solo 2048×2048 massimo — insufficiente per 4K (che richiede ~2040 tile a 64×64). Inoltre non ha metodi di propagazione tra nodi. Quindi l'evoluzione naturale è estendere `DirtyRectMask` in `TileMask` piuttosto che sostituirla.

**Soluzione:** Evolvere `DirtyRectMask` in una `TileMask` che propaga l'impatto dei nodi lungo il grafo. Ogni nodo produce una `TileMask` in output (quali tile ha modificato). Il nodo successivo usa quella mask per decidere quali tile processare.

**Dove:**
- `include/chronon3d/core/dirty_rect_mask.hpp` — **Estendere** `DirtyRectMask` con `k_max_tiles` dinamico, metodi `propagate()`, `dilate()`, `intersect_with()`. Rinominare in `TileMask` con alias `DirtyRectMask` per retrocompatibilità.
- `src/render_graph/executor/graph_executor_dirty.cpp` — Aggiungere `propagate_tile_mask()` accanto a `compute_dirty_clip()` (non sostituire, coesistenza graduale)
- `src/render_graph/render_pipeline_scene.cpp` — `dirty_rect` → `tile_mask` nel `RenderGraphContext`

**Estensione:**
```cpp
// dirty_rect_mask.hpp — evoluto in TileMask
class TileMask {
    static constexpr int k_tile_size = 64;
    
    int m_tiles_x, m_tiles_y;
    std::vector<bool> m_mask;  // resizeabile dinamicamente (invece di bitset<1024>)
    
    void mark_tile(int tx, int ty);
    void mark_bbox(const BBox& bbox);
    
    // Propagazione tra nodi del grafo
    void propagate_from_inputs(std::span<const TileMask> input_masks);
    TileMask dilated(int radius_in_tiles) const;  // blur dilata la mask
    TileMask intersect_with(const BBox& predicted) const;
    
    bool is_tile_affected(int tx, int ty) const;
    bool is_empty() const;
    
    // Retrocompatibilità col BBox
    std::optional<BBox> to_bbox() const;  // unione di tile → BBox approssimato
};

// Nel RenderGraphContext
struct RenderGraphContext {
    // ... esistente ...
    TileMask tile_mask;         // affianca std::optional<raster::BBox> dirty_rect
    std::optional<TileMask> output_tile_mask;
};
```

**Propagazione per livello:**
1. Ogni nodo calcola la `output_tile_mask = propagate_from_inputs(input_masks)`
2. `render_pipeline_scene.cpp` produce una tile_mask globale dalla differenza layer bbox tra frame N e N-1
3. Compositing unisce mask degli input
4. Blur/Bloom: `mask.dilated(radius_in_tiles)` — l'effetto si propaga ai tile vicini
5. Skip completo del nodo se `tile_mask.is_empty()`

**Guadagno stimato:**
- Invece di renderizzare il 60% del frame come unione dirty rect, renderizza solo i tile effettivamente cambiati (es. 5-10%)
- Skip completo di nodi interi se la loro tile_mask è vuota
- Propagazione precisa degli effetti (blur/bloom dilata la mask correttamente)

**Prossimi passi:**
- [ ] Parametrizzare `k_max_tiles` in `DirtyRectMask` per supportare 4K (cambio `bitset<N>` → `vector<bool>`)
- [ ] Aggiungere `propagate_from_inputs()`, `dilated()`, `intersect_with()` — estendere classe esistente
- [ ] Integrare in `RenderGraphContext` — affiancare `dirty_rect` con `tile_mask`
- [ ] In `graph_executor_dirty.cpp`, implementare `propagate_tile_mask()`
- [ ] In ogni nodo: calcolare la `output_tile_mask` come funzione della `input_tile_mask` e `predicted_bbox`
- [ ] Nel `GraphExecutor`, skip dei nodi con mask vuota

---

### Pillar 4 — Static/Dynamic Separation Reale

**Problema:** La cache (`NodeCache` in `node_cache.cpp`) è per-nodo, non per-tile. Un nodo statico (es. background grid) viene ricacheggiato interamente anche se solo un piccolo tile è cambiato. Inoltre, `disk_cacheable` non è mai abilitato per nessun nodo.

**Soluzione:** Tutto ciò che è statico va pre-renderizzato o memorizzato per tile. Il runtime deve occuparsi quasi solo del delta:
- **Static tile cache:** Ogni tile ha un flag `is_static`. Se un tile non cambia, non viene mai ritoccato.
- **Pre-render:** I nodi con `frame_invariant=true` vengono renderizzati una volta e mai più.
- **Disk cache per tile:** I tile statici vengono salvati su disco via `DiskNodeCache` (già implementata in `disk_node_cache.hpp`).

**Dove:**
- `src/cache/disk_node_cache.cpp` — Già implementata, va estesa per tile-level caching (non solo node-level)
- `src/render_graph/executor/graph_executor_cache.cpp` — Integrare `evaluate_cache()` con tile-level cache
- `include/chronon3d/render_graph/cache_policy.hpp` — Aggiungere `TileCachePolicy` struct

**Struttura:**
```cpp
struct TileCachePolicy {
    bool tile_cacheable{false};         // può essere cacheato per tile?
    CacheLifetime tile_lifetime{CacheLifetime::PerComposition};
    bool disk_tile_cacheable{false};    // può essere salvato su disco?
    int tile_static_threshold{3};       // dopo N frame invariato → mark as static
};

// Nel RenderNodeCachePolicy
struct RenderNodeCachePolicy {
    // ... esistente ...
    TileCachePolicy tile_policy;
};
```

**Classificazione statico/dinamico automatica:**
1. Ogni tile tiene un `frame_last_modified`
2. Se un tile non è stato modificato per 3 frame consecutivi → `is_static = true`
3. Se la camera si muove, tutti i tile diventano `is_static = false` (scroll ottimizzato separatamente)
4. I tile statici vengono salvati su disco con `DiskNodeCache::put_tile()`

**Guadagno stimato:**
- Per scene con background statico e UI animata: solo i tile dell'UI vengono renderizzati
- Cache su disco: i tile statici sopravvivono tra sessioni
- `GridCleanBackground` → 0 cost a runtime dopo il primo frame

**Prossimi passi:**
- [ ] Aggiungere `TileCachePolicy` a `cache_policy.hpp`
- [ ] In `graph_executor_dirty.cpp`, aggiungere logica di static classification per tile
- [ ] Estendere `DiskNodeCache` con `get_tile()` / `put_tile()`
- [ ] Integrare in `GraphExecutor::execute()` — skip dei tile statici

---

### Pillar 5 — Nodi Analitici / Procedural Kernels

**Problema:** `GridCleanBackground` (e pattern procedurali simili) vengono renderizzati attraverso il bridge generico `bl2d_blimage_tiled.cpp` — che fa sampling bilineare di un'immagine blend2D, con trasformazione prospettica, per OGNI pixel. Per un pattern semplice come una griglia, questo è drammaticamente sovradimensionato: invece di calcolare 4 linee con 4 moltiplicazioni, si fa sampling di texture con interpolazione bilineare per ogni pixel dell'area.

**Soluzione:** Ogni pattern semplice (griglia, gradiente, pattern ripetuto, barra, background procedurale) diventa un nodo dedicato con kernel C++/SIMD ottimizzato, zero dipendenza da blend2D o image sampling.

**Dove:**
- Nuova directory: `src/backends/software/procedural/` — kernel specializzati
- `src/backends/software/procedural/grid_background.cpp` — `render_grid_tile()` kernel SIMD
- `src/backends/software/procedural/gradient.cpp` — gradiente lineare/radiale
- `src/backends/software/procedural/pattern.cpp` — pattern ripetuti (dots, stripes, checkerboard)
- `include/chronon3d/render_graph/nodes/procedural_source_node.hpp` — nuovo tipo di nodo

**Kernel griglia esempio:**
```cpp
// Invece di sampling bilineare su BLImage:
// Calcola direttamente i pixel della griglia con SIMD

void render_grid_tile(Color* __restrict__ dst, int tile_x, int tile_y, int tile_w, int tile_h,
                       const GridParams& p, const CameraPhase& phase) {
    const float cell_w = p.cell_width;
    const float cell_h = p.cell_height;
    const float offset_x = phase.offset_x;
    const float offset_y = phase.offset_y;
    
    // Linea orizzontale e verticale: 4 FMA per pixel
    // Invece di sampling bilineare con 10+ operazioni float
    for (int y = 0; y < tile_h; ++y) {
        for (int x = 0; x < tile_w; ++x) {
            float gx = (tile_x * tile_w + x + offset_x) / cell_w;
            float gy = (tile_y * tile_h + y + offset_y) / cell_h;
            float gx_frac = gx - floorf(gx);
            float gy_frac = gy - floorf(gy);
            // Kernel condensa in ~8 istruzioni SIMD vs 40+ del sampling
            bool is_line = (gx_frac < p.line_thickness || gy_frac < p.line_thickness);
            dst[y * tile_w + x] = is_line ? p.line_color : p.bg_color;
        }
    }
}
```

**Guadagno stimato:**
- Griglia semplice: da ~500μs (sampling bilineare su tutta l'area) a ~20μs (kernel dedicato 4 FMA/pixel) — **25× speedup**
- Gradiente: da blend2D bridge a ~10μs per tile 256×256
- Pattern ripetuti: da image sampling a loop SIMD lineare
- **Zero dipendenza da BLImage**: elimina il bridge blend2D per pattern procedurali

**Prossimi passi:**
- [ ] Creare `src/backends/software/procedural/grid_background.cpp` — kernel `render_grid_tile()`
- [ ] Creare `ProceduralSourceNode` — `RenderGraphNode` che accetta `ProceduralParams` e produce tile
- [ ] Registrare nel `ShapeRegistry` come nuovo shape type `ShapeType::ProceduralGrid`
- [ ] Integrare in `GraphBuilder::build()` — se il layer ha shape type Procedural, usa `ProceduralSourceNode` invece di `bl2d_blimage_tiled`
- [ ] Aggiungere benchmark: confronto sampling bilineare vs kernel dedicato

---

### Pillar 6 — Compositing Solo Dove Serve

**Problema:** Il compositing (`software_compositor.cpp`) attualmente fa full-frame o clip su dirty rect. Ma con tile-first, ogni tile ha il suo compositing: se un tile non cambia, non serve compositing. Inoltre, il `SoftwareCompositor::composite_layer()` ha un loop pixel-per-pixel con bounds check per ogni accesso.

**Soluzione:** Il compositing diventa un'operazione di merge su tile attivi:
- Ogni tile composita solo se `tile_mask.is_tile_affected()`
- Merge batch: invece di `composite_layer()` chiamato N volte (una per layer), accumula tutti i layer che toccano un tile in un unico passaggio
- Skip completo del compositing per tile che non hanno cambiato

**Dove:**
- `src/backends/software/software_compositor.cpp` — Nuova funzione `composite_tile()` che processa solo tile specifici
- `src/render_graph/executor/graph_executor_phases.cpp` — Nel loop di esecuzione, usare `TileMask` per decidere quali tile compositare

**Struttura:**
```cpp
// Compositing per tile — niente full-frame
void composite_tile(
    Framebuffer& dst_tile,      // tile 256×256
    std::span<const Framebuffer*> src_tiles,  // tile dallo stesso rect dei layer
    std::span<const BlendMode> modes,
    const TileMask& active_mask
);

// Se active_mask è vuota per questo tile → zero lavoro
if (!active_mask.is_tile_affected(tx, ty)) {
    continue;  // nessuna operazione
}
```

**Guadagno stimato:**
- Compositing: da O(width × height × layers) a O(tile_size² × active_tiles)
- Per scena con UI piccola su background statico: ~1-5% dei pixel invece del 100%
- Merge batch: elimina overhead di chiamata per layer per tile

**Prossimi passi:**
- [ ] Implementare `composite_tile()` in `software_compositor.cpp`
- [ ] Integrare con `TileMask` nel `GraphExecutor`
- [ ] Aggiungere `tile_composite_count` counter in telemetry

---

### Pillar 7 — Buffer Persistenti e Tile Surfaces

**Problema:** Il `FramebufferPool` (in `framebuffer_pool.cpp`) alloca e dealloca framebuffer dinamicamente. Con tile-first, ogni tile dovrebbe avere una superficie persistente che vive per l'intera durata della composizione, senza essere rilasciata nel pool.

**Soluzione:** Le superfici tile devono vivere a lungo, essere riusate per tile/level, e clearate solo dove serve davvero. Invece di un pool generico, avere un `TileSurfaceCache` che tiene N tile surfaces allocate permanentemente.

**⚠️ Attenzione al budget memoria:** 4096 tile a 256×256 × 16 bytes = 4 GB per UNA superficie tile. Una composizione con 5 strati intermedi (background, layer1, layer2, effetti, output) richiederebbe 20 GB con superfici separate. **Soluzione: superfici tile condivise.** Un tile statico è write-once-read-many — tutti i layer che leggono lo stesso tile condividono la stessa superficie fisica tramite reference counting. Solo i tile attivamente scritti (dirty) hanno superfici dedicate.

**Dove:**
- `include/chronon3d/cache/tile_surface_cache.hpp` (nuovo) — `TileSurfaceCache` classe
- `src/cache/tile_surface_cache.cpp` (nuovo) — Implementazione
- `src/cache/framebuffer_pool.cpp` — Il pool esiste ancora, ma per le superfici temporanee (effetti, blur, ecc.)

**Struttura:**
```cpp
class TileSurfaceCache {
    static constexpr int MAX_TILE_SURFACES = 4096;  // 64×36 tile per 4K
    
    struct TileSurface {
        std::shared_ptr<Framebuffer> fb;  // shared_ptr per condivisione read-only
        TileId id;
        bool dirty;                 // se true, va ricompositat
        bool is_static;             // non cambia più
        int ref_count;              // quanti layer leggono questo tile
    };
    
    std::unordered_map<TileId, TileSurface> m_surfaces;
    
    // Ottiene superficie per scrittura (dedicata, copy-on-write se condivisa)
    Framebuffer& acquire_writable(TileId id, int tile_w, int tile_h);
    
    // Ottiene superficie per lettura (condivisa tra layer)
    std::shared_ptr<Framebuffer> acquire_readable(TileId id);
    
    // Cleara SOLO i tile marcati come dirty
    void clear_dirty_tiles(const TileMask& mask);
    
    // Stats
    size_t total_surfaces() const;
    size_t shared_surfaces() const;  // tile con ref_count > 1
};
```

**Vantaggi:**
- **Zero allocazioni a runtime:** tutte le superfici sono preallocate
- **Condivisione read-only:** tile statici condivisi tra layer — memoria reale << memoria virtuale
- **Copy-on-write:** se un tile viene scritto, viene prima clonato (solo se condiviso)
- **Persistenza:** un tile che non cambia mantiene il suo contenuto per tutta la durata
- **Clear selettivo:** solo i tile marcati dirty vengono clearati
- **Budget realistico:** con condivisione, 4 GB bastano per composizioni complesse

**Guadagno stimato:**
- Zero framebuffer_acquire_ms per tile surfaces (sono preallocate)
- Zero framebuffer_clear_ms per tile non dirty
- Cache L1/L2 calda: le stesse superfici vengono riusate frame dopo frame

**Prossimi passi:**
- [ ] Definire `TileSurfaceCache` in `include/chronon3d/cache/tile_surface_cache.hpp`
- [ ] Implementare `acquire_writable()`, `acquire_readable()`, `clear_dirty_tiles()`
- [ ] Implementare copy-on-write: se un tile è condiviso, clonare prima di scrivere
- [ ] Integrare in `RenderGraphContext` — affiancare `framebuffer_pool` per le superfici tile
- [ ] Aggiungere telemetry: `tile_surface_reuses`, `tile_surface_creations`, `tile_surface_shared_ratio`

---

### Pillar 8 — Encoder Pipeline Separata (Output Path)

**Problema:** La conversione del framebuffer in YUV420P/NV12 (`ffmpeg_pipe_yuv.cpp`) è stata ottimizzata con SIMD (N16 completato), ma è ancora nel path di rendering: il render thread aspetta che la conversione finisca prima di passare al frame successivo.

**Soluzione:** L'output path deve essere una pipeline distinta e ottimizzata per stream, non un passaggio che contamina il core renderer. Con tile-first, la conversione YUV può avvenire per-tile mentre il renderer lavora sul frame successivo.

**Dove:**
- `apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.cpp` — Riscrivere come pipeline asincrona
- `include/chronon3d/cli/video/output_pipeline.hpp` (nuovo) — `OutputPipeline` classe
- `apps/chronon3d_cli/utils/video/output_pipeline.cpp` (nuovo) — Pipeline disaccoppiata

**Struttura:**
```cpp
class OutputPipeline {
    // Tile encoder queue — converte tile in YUV mentre il renderer lavora
    moodycamel::ConcurrentQueue<TileEncodeJob> m_tile_queue;
    
    // Tile-to-frame assembler — riceve tile YUV e li assembla nel frame finale
    struct TileEncodeJob {
        TileId id;
        std::shared_ptr<Framebuffer> tile_fb;
        int frame_number;
    };
    
    void enqueue_tile(TileEncodeJob&& job);       // chiamato dal render thread
    void process_tiles();                          // chiamato dal encoder thread
    void flush_frame(int frame_number);            // assemble e invia a FFmpeg
};
```

**Guadagno stimato:**
- **Zero attesa:** il render thread non aspetta mai la conversione YUV
- **Pipeline parallela:** conversione tile YUV in parallelo con rendering del frame successivo
- **Tile reuse:** i tile statici vengono convertiti YUV una volta e cacheati
- Bandwidth: da frame_conversion_copy_ms a ~0 nel hot path

**Prossimi passi:**
- [ ] Creare `OutputPipeline` con coda tile lock-free
- [ ] Separare conversione YUV dal rendering — spostare in `OutputPipeline`
- [ ] Integrare con moodycamel::ConcurrentQueue (già presente in `video_export_pipe.cpp`)
- [ ] Aggiungere `output_pipeline_ms` counter in telemetry

---

### Pillar 9 — Tile Scheduler (Work Distribution Engine)

**Problema:** L'esecutore attuale (`GraphExecutor::execute()` in `graph_executor_phases.cpp`) parallelizza per livello del DAG: tutti i nodi di un livello vengono eseguiti in parallelo via `tbb::parallel_for`. Ma non c'è scheduling per tile — un nodo che produce 64 tile processa tutto in sequenza.

**Soluzione:** Un `TileScheduler` che distribuisce il lavoro dei tile attraverso i worker thread. Ogni nodo non produce più un framebuffer, ma una coda di job tile che vengono schedulati sui worker.

**Dove:**
- `include/chronon3d/runtime/tile_scheduler.hpp` (nuovo) — `TileScheduler` con worker pool
- `src/runtime/tile_scheduler.cpp` (nuovo) — Implementazione
- `src/render_graph/executor/graph_executor_phases.cpp` — Integrare il tile scheduler

**Approccio pragmatico (TBB-first):**
Prima di implementare uno scheduler custom, misurare se `tbb::parallel_for` sui tile è già sufficiente. TBB ha work-stealing integrato e `tbb::blocked_range` gestisce bene carichi a granularità fine. Solo se il profiling mostra overhead di TBB > 5% a granularità tile (improbabile per tile 256×256), allora implementare scheduler custom.

```cpp
// Approccio V1: TBB parallelo su tile
for (auto& level : plan.levels) {
    // Per ogni nodo, lancia tile in parallelo su TBB
    tbb::parallel_for(
        tbb::blocked_range<int>(0, level.size()),
        [&](auto& range) {
            for (int i = range.begin(); i < range.end(); ++i) {
                auto& node = graph.node(level[i]);
                auto tile_fn = node.get_tile_executor(ctx);
                
                // Parallelizza per tile all'interno del nodo
                tbb::parallel_for(
                    tbb::blocked_range<int>(0, active_tiles.size()),
                    [&](auto& tr) {
                        for (int ti = tr.begin(); ti < tr.end(); ++ti) {
                            tile_fn(active_tiles[ti]);
                        }
                    }
                );
            }
        }
    );
}

// Approccio V2 (solo se profiling mostra bottleneck TBB):
// TileScheduler custom con moodycamel::ConcurrentQueue
```

**Vantaggi dell'approccio TBB-first:**
- **Zero nuovo codice di scheduling** — si riusa l'esistente `tbb::parallel_for`
- **Work-stealing già built-in** — TBB bilancia automaticamente i carichi
- **Nesting supportato** — TBB gestisce correttamente nested parallelism
- **Meno rischi** — niente bug di race condition in scheduler custom

**Guadagno stimato:**
- Da parallelismo per-livello (10-20 nodi) a parallelismo per-tile (64-4096 job)
- Utilizzo 100% dei core
- Load balancing automatico via TBB work-stealing (senza scrivere una riga di scheduler)

**Prossimi passi:**
- [ ] Misurare overhead TBB a granularità tile (256×256) con `perf stat`
- [ ] Se overhead < 5%: usare `tbb::parallel_for` nidificato (nessuno scheduler custom)
- [ ] Se overhead > 5%: implementare `TileScheduler` custom
- [ ] Aggiungere `tile_parallel_efficiency` counter

---

### Pillar 10 — Per-Tile Cache (Tile-Level Hashing and Caching)

**Problema:** La cache attuale (`NodeCache` in `node_cache.cpp`) è per-nodo: hasha l'intero nodo e cachea l'intero framebuffer output. Con tile-first, la cache deve essere per-tile: un nodo può avere 36 tile, di cui 30 statici e 6 cambiati. La cache per-nodo invalida tutto quando uno qualsiasi dei 36 tile cambia.

**Soluzione:** Sostituire la cache per-nodo con una cache per-tile. Ogni tile ha una `TileCacheKey` che include l'hash del nodo + tile_id + params_hash. Tile statici rimangono in cache per tutta la durata della composizione.

**Dove:**
- `include/chronon3d/cache/tile_cache.hpp` (nuovo) — `TileCache` classe
- `src/cache/tile_cache.cpp` (nuovo) — Implementazione
- `src/render_graph/executor/graph_executor_cache.cpp` — `evaluate_cache()` per tile

**Struttura:**
```cpp
struct TileCacheKey {
    u64 node_digest;         // hash del nodo (parametri + tipo)
    TileId tile_id;          // (tx, ty)
    u64 input_hash;          // hash degli input per questo tile
    u64 params_hash;         // hash dei parametri correnti
    Frame frame;             // per frame-dependent tiles
    
    u64 digest() const;     // XXH3 dell'intera key
};

class TileCache {
    using Cache = LruCache<TileCacheKey, std::shared_ptr<Framebuffer>>;
    Cache m_cache;
    
    std::shared_ptr<Framebuffer> get_tile(const TileCacheKey& key);
    void put_tile(const TileCacheKey& key, std::shared_ptr<Framebuffer> tile);
    
    // Invalida per nodo (quando i parametri cambiano)
    void invalidate_node(u64 node_digest);
    
    // Stats
    struct TileCacheStats {
        uint64_t tile_hits, tile_misses;
        uint64_t static_tile_hits;   // tile che non sono mai cambiati
        uint64_t tiles_cached;
    };
};
```

**Vantaggi:**
- **Invalidazione granulare:** se 1 tile su 36 cambia, gli altri 35 restano in cache
- **Tile statici:** non vengono mai ricalcolati dopo il primo frame
- **Disk cache per tile:** abilitabile per tile statici
- **Cache size efficiente:** tile 256×256 = 256KB (vs 33MB per full-frame 4K)

**Guadagno stimato:**
- Hit rate: da ~60% (node-level) a ~95% (tile-level) per scene con parti statiche
- Memoria: tile in cache occupano 256KB l'uno, possono stare in L2/L3
- Skip tile: un tile invariato viene copiato dalla cache in ~5μs vs 500μs per renderizzarlo

**Prossimi passi:**
- [ ] Definire `TileCacheKey` e `TileCache` in `include/chronon3d/cache/tile_cache.hpp`
- [ ] Implementare `get_tile()`, `put_tile()`, `invalidate_node()`
- [ ] Integrare in `GraphExecutor::execute()` — sostituire `NodeCache::get()` per tile
- [ ] Aggiungere telemetry: `tile_cache_hits`, `tile_cache_misses`, `tile_static_ratio`

---

### V3 — La Visione Unificata: Architettura

```
┌─────────────────────────────────────────────────────────────────┐
│                         COMPILATION LAYER                        │
│  Scene (dinamica, orientata agli oggetti)                        │
│       │                                                          │
│       ▼                                                          │
│  DisplayListCompiler                                             │
│       │  Una volta per cambio struttura                           │
│       ▼                                                          │
│  CompiledDisplayList (immutabile, flat array, cacheable su disk) │
└──────────────────────────────────────────────────────────────────┘
       │
       │ params delta (positions, opacity, time) ogni frame
       ▼
┌──────────────────────────────────────────────────────────────────┐
│                         TILE EXECUTION LAYER                      │
│                                                                   │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐           │
│  │ TileScheduler│───▶│ TileCache   │◀───│ TileMask    │           │
│  │ (worker pool)│    │ (hit/miss)  │    │ (invalidation)│          │
│  └──────┬──────┘    └─────────────┘    └─────────────┘           │
│         │                                                         │
│         ▼                                                         │
│  ┌──────────────────────────────────────────────────────┐        │
│  │  TileGrid (matrice di tile 256×256)                   │        │
│  │  ┌────┬────┬────┬────┬────┬────┬────┬────┐           │        │
│  │  │ T00│ T01│ T02│ T03│ T04│ T05│ T06│ T07│  ...     │        │
│  │  ├────┼────┼────┼────┼────┼────┼────┼────┤           │        │
│  │  │ T10│ T11│ T12│    │    │    │    │    │  ...     │        │
│  │  └────┴────┴────┴────┴────┴────┴────┴────┘           │        │
│  └──────────────────────────────────────────────────────┘        │
│         │                                                         │
│         ▼ (merge tile attivi)                                     │
│  ┌──────────────────┐    ┌─────────────────────────────────┐     │
│  │ TileCompositor   │    │ TileSurfaceCache (persistent)   │     │
│  │ (merge per tile) │───▶│ (tile surfaces preallocate)     │     │
│  └──────────────────┘    └─────────────────────────────────┘     │
└──────────────────────────────────────────────────────────────────┘
       │
       ▼ (full frame ricomposto)
┌──────────────────────────────────────────────────────────────────┐
│                         OUTPUT LAYER                              │
│  OutputPipeline (tile YUV queue + encoder thread separato)       │
│       │                                                          │
│       ▼                                                          │
│  FFmpeg pipe / PNG / EXR                                         │
└──────────────────────────────────────────────────────────────────┘
```

---

### V3 — Priorità di Implementazione

| Fase | Pillar | Cosa | Impatto | Effort |
|------|--------|------|---------|--------|
| **1** | P5 | Nodi procedurali (grid kernel) | 🔴 Alto | 🟢 Bassa (1-2gg) |
| **2** | P3 | TileMask invalidation | 🔴 Alto | 🟡 Media (3-5gg) |
| **3** | P1 | TileGrid + esecuzione tile-aware | 🔴 Alto | 🔴 Alta (1-2 sett) |
| **4** | P2 | Display list compilation | 🔴 Alto | 🔴 Alta (1-2 sett) |
| **5** | P10 | Per-tile cache | 🟡 Medio | 🟡 Media (3-5gg) |
| **6** | P7 | Tile surface cache (persistent) | 🟡 Medio | 🟡 Media (3-5gg) |
| **7** | P9 | Tile scheduler (worker pool) | 🟡 Medio | 🔴 Alta (1-2 sett) |
| **8** | P6 | Tile compositing (merge) | 🟡 Medio | 🟡 Media (3-5gg) |
| **9** | P8 | Output pipeline separata | 🟡 Medio | 🟡 Media (3-5gg) |
| **10** | P4 | Static/dynamic classification automatica | 🟢 Basso | 🟢 Bassa (1-2gg) |

---

### V3 — Cosa Cambia per File Esistenti

| File Attuale | Cambiamento V3 |
|---|---|
| `render_graph_node.hpp` — `execute()` signature | Da `vector<shared_ptr<FB>>` a `TileGrid&` + `TileMask` |
| `graph_executor_phases.cpp` — `execute()` loop | Da `tbb::parallel_for` su livello a `TileScheduler` su tile |
| `graph_executor_cache.cpp` — `evaluate_cache()` | Da node-level a tile-level key |
| `graph_executor_dirty.cpp` — `compute_dirty_clip()` | Da BBox union a `TileMask::propagate_from_inputs()` |
| `render_pipeline_scene.cpp` — `render_scene_via_graph()` | Da `RenderGraph` a `CompiledDisplayList` path |
| `render_graph.cpp` — `RenderGraph` class | Rimane per graph build, ma non per esecuzione diretta |
| `software_compositor.cpp` — `composite_layer()` | Da full-frame a `composite_tile()` su tile attivi |
| `node_cache.cpp` — `NodeCache` | Rimane per cache cross-frame, ma `TileCache` lo affianca |
| `framebuffer_pool.cpp` — `FramebufferPool` | Rimane per temporanei, ma `TileSurfaceCache` lo affianca per tile |
| `bl2d_blimage_tiled.cpp` — `composite_bl_image_tiled()` | Sostituito da `ProceduralSourceNode` per pattern |
| `ffmpeg_pipe_yuv.cpp` — `convert_framebuffer_to_yuv420p()` | Spostato in `OutputPipeline` come task asincrono |
| `disk_node_cache.cpp` — `DiskNodeCache` | Esteso con `get_tile()` / `put_tile()` |
| `cache_policy.hpp` — `RenderNodeCachePolicy` | Aggiunto `TileCachePolicy` |
| `dirty_rect_mask.hpp` — `DirtyRectMask` | Sostituito da `TileMask` (più ricco e integrato) |

---

### V3 — Riepilogo Guadagni Stimati

| Metrica | Oggi (V1/V2) | V3 Target | Guadagno |
|---------|-------------|-----------|----------|
| Bandwidth memoria per frame | ~100% canvas (1080p = 33MB) | ~5-10% canvas (solo tile attivi) | **10-20×** |
| Cache hit rate | ~60% (node-level) | ~95% (tile-level) | **+35%** |
| Allocazioni framebuffer | 10-30 per frame | 0 (tile surfaces preallocate) | **∞** |
| Compositing pixel processati | 100% canvas | solo tile attivi | **10-20×** |
| Grid background raster | ~500μs (bilinear sampling) | ~20μs (kernel SIMD) | **25×** |
| Build graph overhead | 0.5-2ms per frame | ~0ms (compiled once) | **∞** |
| Output conversion blocco | frame_conversion_copy_ms | ~0ms (pipeline parallela) | **-100%** |
| Frame throughput (4K 30fps) | ~30fps software | ~60-120fps software | **2-4×** |

---

## 🔥 RIVOLUZIONARIO — Idee per il Salto Quantico

> Queste idee vanno OLTRE l'ottimizzazione tradizionale. Sono cambiamenti di paradigma che nessun motore 2.5D programmatico ha ancora implementato.

---

### R1. Compilazione JIT del Grafo in Kernel ISPC

**Problema:** Oggi ogni nodo fa load→processa→store. Poi il prossimo nodo load→processa→store. Framebuffer intermedi sprecano bandwidth.

**Idea:** Quando il grafo è stabile, **compila un kernel ISPC specializzato** per quella catena. Un unico load, tutta la pipeline in registri SIMD, un unico store.

```
INPUT: TextLayer + DropShadow + Glow + Opacity 0.7 + ScreenBlend
       ↓
COMPILA: kernel_text_drop_shadow_glow_screen.ispc
       ↓
RESULT: Una funzione che fa TUTTO in un colpo solo
```

**Come:** Generi file `.ispc` al volo, compili con `ispc` via pipe, carichi il `.so` con `dlopen`.

**Dove:** `src/render_graph/jit_compiler.cpp` + integrazione in `GraphExecutor`.

**Guadagno:** **3-5×** sul percorso caldo per catene di 5+ operazioni. ⭐⭐⭐⭐⭐

---

### R2. Framebuffer Virtuale Copy-on-Write (Tile COW)

**Problema:** 10 layer = 10 framebuffer. Il 90% dei pixel è invariato tra layer consecutivi, ma vengono comunque copiati.

**Idea:** Dividi il framebuffer in **tile 32×32**. Ogni tile ha un contatore di riferimenti. Se un nodo non modifica un tile → **punta allo stesso tile fisico** dell'input. Se modifica → **copia solo quel tile** (copy-on-write).

**Catena esempio:** Layer 1 → Blur 3px → Composite. Con COW: blur modifica solo i bordi (~20% dei tile). L'80% dei pixel è condiviso. Zero copie.

**Dove:** `Framebuffer` diventa un container di tile COW.

**Guadagno:** **2-8×** su catene di effetti. ⭐⭐⭐⭐

---

### R3. Adaptive Precision Rendering

**Problema:** Ogni pixel è `Color` = 4 × `float` = 16 byte. Anche per testo in bianco e nero.

**Idea:** Tile eterogenei con precisione adattiva:

| Contenuto | Formato | Risparmio |
|:----------|:--------|:---------:|
| Testo/maschere | **1-bit** (32 pixel per `uint32_t`) | **128×** |
| Forme solide | **8-bit** RGBA | **4×** |
| Gradienti | **half** RGBA | **2×** |
| Foto/Video | **float** RGBA | 1× |

`VariantFramebuffer` = `std::variant<Framebuffer8, Framebuffer16, Framebuffer32, Bitmask>`.

**Dove:** `framebuffer.hpp` + tile format detection nel grafo.

**Guadagno:** **3-4×** bandwidth reale su scene con testo dominante. ⭐⭐⭐⭐

---

### R4. Direct-to-Encoder Pixel Pipeline

**Problema:** Export video oggi: render RGBA float → convert to YUV → write pipe FFmpeg. Due conversioni e una copia per frame.

**Idea:** Integra l'encoder NEL grafo: `render_pixel_to_yuv_buffer()` invece di `render_pixel_to_framebuffer()`. I pixel vanno direttamente nei piani Y/U/V, saltando il formato intermedio RGBA.

Più folle: **delta frame rendering** — se il frame N è identico al frame N-1 in una regione, non inviare quei dati all'encoder.

**Dove:** `RenderNode::execute()` scrive direttamente in piani YUV se output è video.

**Guadagno:** **1.5-2×** su export video — elimina frame_conversion_copy_ms. ⭐⭐⭐⭐⭐

---

### R5. Level-of-Detail (LOD) per Layer 2.5D

**Problema:** I motori 3D usano LOD da 30 anni. I motori 2D/2.5D **mai** — ogni layer è sempre a risoluzione piena.

**Idea:** Ogni layer ha 3 versioni (1×, 0.5×, 0.25×). In base a:
- **Profondità Z** (più lontano = LOD più basso)
- **Area occupata** (layer piccolo = LOD basso)
- **Occlusione** (sotto layer opaco = LOD basso)
- **Velocità camera** (durante motion, layer non focali = LOD basso)

Layer al 25% schermo + LOD 0.25× = 1/16 pixel = **16× speedup**.

**Dove:** `LODSelector` assegna scala a ogni nodo prima dell'esecuzione.

**Guadagno:** **2-16×** su layer non focali. ⭐⭐⭐⭐⭐

---

### 🏆 Classifica

| # | Idea | Difficoltà | Impatto | Originalità |
|:-:|:-----|:----------:|:-------:|:-----------:|
| 1 | **R5 — LOD 2.5D** | 🟢 Bassa | 2-16× | ⭐⭐⭐⭐⭐ |
| 2 | **R2 — Tile COW** | 🟡 Media | 2-8× | ⭐⭐⭐⭐ |
| 3 | **R3 — Adaptive precision** | 🟡 Media | 3-4× | ⭐⭐⭐⭐ |
| 4 | **R1 — JIT kernel fusion** | 🔴 Alta | 3-5× | ⭐⭐⭐⭐⭐ |
| 5 | **R4 — Direct-to-encoder** | 🔴 Alta | 1.5-2× | ⭐⭐⭐⭐⭐ |

**Primo passo RIVOLUZIONARIO:** **R5 (LOD per layer)** — 1-2 giorni di prototipo, impatto 2-16×, e Chronon3D diventerebbe il **primo motore 2.5D con Level-of-Detail per layer**.

---

## 🧠 INNOVAZIONI STRATEGICHE — Analisi dal Telemetry Report

> Innovazioni emerse dall'analisi del report telemetry `ImgReferenceShakeReveal` (run_9333dfe8).
> Attaccano i colli di bottiglia reali misurati: 300 layer/0 culled, 100% dirty ratio, 1.24GB alloc, 1.2K ms clears.

---

### I1. Render Graph Incrementale (il game changer)

**Problema:** Oggi il grafo viene ricostruito e rieseguito interamente ogni frame. Per `ImgReferenceShakeReveal`: 150 frame × 300 nodi = 45.000 esecuzioni. Il 90% del lavoro è ridondante (grid background identico in ogni frame).

**Soluzione:** Mantenere il grafo tra i frame e rieseguire solo i nodi dirty.

```
Oggi:   Frame N → build_graph() → execute_all(300 nodi) → 188ms
Futuro: Frame N → diff_graph(N-1, N) → execute_dirty(~5 nodi) → ~10ms
```

**Meccanismo:**
1. **Hash del grafo**: Ogni nodo produce un fingerprint dai suoi parametri (transform, opacity, bounds, image hash)
2. **Diffing**: Confronta il grafo del frame N con N-1 → identifica i nodi che sono cambiati
3. **Esecuzione selettiva**: Solo i nodi dirty vengono rieseguiti, gli altri vengono **riusati** direttamente dai framebuffer precedenti
4. **Invalidazione a cascata**: Se un nodo dirty ha figli, anche quelli vengono marcati dirty

**Dove:**
- `src/render_graph/executor/graph_executor_incremental.cpp` (nuovo) — diff engine
- `include/chronon3d/render_graph/node_fingerprint.hpp` (nuovo) — fingerprint per nodo
- `src/render_graph/executor/graph_executor_phases.cpp` — nuovo path incrementale

**Guadagno stimato:** 3-5× per composizioni con contenuto statico (70-80% del render time risparmiato).

**Prossimi passi:**
- [ ] Definire `NodeFingerprint` con hash di params + transform + bounds + source_hash
- [ ] Implementare `IncrementalExecutor::diff()` — confronta grafo corrente con precedente
- [ ] Nodi invariati: `return prev_framebuffer` senza riesecuzione
- [ ] Invalidazione cascata: dirty node → invalida tutti i figli
- [ ] Benchmark su `ImgReferenceShakeReveal` — misurare frame time prima/dopo

---

### I2. SoA Framebuffer per i kernel hot

**Problema:** `Color{r,g,b,a}` è AoS (Array of Structures). Per i kernel SIMD, ogni operazione carica 16 bytes (4 float) quando spesso servono solo 1-2 canali. L'alpha testing (check `src.a > 0.001f`) è il caso peggiore: carica R,G,B inutilmente.

**Soluzione:** Un `FramebufferSoA` con 4 piani separati:

```cpp
struct FramebufferSoA {
    float* R;  // width * height * 4 bytes
    float* G;
    float* B;
    float* A;
};
```

**Vantaggi:**
- **Alpha testing**: Confronta 8 alpha in un registro AVX2 (`_mm256_cmp_ps`) → early-exit per pixel trasparenti
- **Gather/Scatter**: Più efficiente per campionamento non-lineare (bilinear, projective)
- **Prefetching**: I 4 piani sono indipendenti → prefetch più aggressivo
- **Bandwidth**: Operazioni che toccano solo 1 canale (es. alpha mask) leggono 4× meno dati

**Dove:**
- `include/chronon3d/core/memory/framebuffer_soa.hpp` (nuovo) — `FramebufferSoA`
- `src/backends/software/simd/soa_kernels.cpp` (nuovo) — kernel SoA per composite/transform
- `src/render_graph/nodes/transform_node.cpp` — path SoA per transform

**Guadagno stimato:** 1.5-2× sui kernel alpha-dominated (compositing, mask application).

**Trade-off:** Richiede refactor del composite kernel. Coesistenza graduale: SoA affianca AoS, il grafo sceglie il formato in base al tipo di operazione.

**Prossimi passi:**
- [ ] Definire `FramebufferSoA` con `from_framebuffer(AoS)` e `to_framebuffer(AoS)`
- [ ] Riscrivere `composite_normal_premul` in versione SoA con `_mm256_maskz_loadu`
- [ ] Aggiungere `select_format()` al graph builder: se layer ha mask → SoA, altrimenti AoS
- [ ] Benchmark: alpha mask 100% transparent vs 100% opaque, misurare speedup

---

### I3. Wavefront Scheduling (massima parallelizzazione cache-aware)

**Problema:** Oggi il grafo viene eseguito per **livelli topologici** — tutti i nodi allo stesso livello girano in parallelo. Ma i livelli mescolano tipi di operazione diversi (source + transform + composite nella stessa wave), causando cache thrashing.

**Soluzione:** Raggruppare per **tipo di operazione** invece che per livello:

```
Wavefront 1: Tutti i Source nodes (rasterizzazione)     → SIMD gather
Wavefront 2: Tutti i Transform nodes                    → SIMD bilinear
Wavefront 3: Tutti i Composite nodes                    → SIMD blend
```

**Perché è meglio:**
- **Better cache locality**: Tutti i source nodes leggono dalle stesse texture → la texture data rimane in L3 cache
- **Better SIMD utilization**: Ogni wavefront esegue la stessa istruzione su dati omogenei
- **Better branch prediction**: Il branch predictor si calibra su un solo tipo di operazione per wavefront

**Dove:**
- `src/render_graph/executor/graph_executor_wavefront.cpp` (nuovo) — wavefront scheduler
- `include/chronon3d/render_graph/wavefront_plan.hpp` (nuovo) — piano di esecuzione
- `src/render_graph/executor/graph_executor_phases.cpp` — nuovo path wavefront

**Guadagno stimato:** 1.3-1.5× grazie alla cache locality.

**Prossimi passi:**
- [ ] Analizzare il grafo di `ImgReferenceShakeReveal`: 288 source nodes, 1 transform, 1 composite
- [ ] Raggruppare i 288 source nodes in una singola wavefront TBB
- [ ] Misurare L3 cache hit rate con `perf stat` prima/dopo
- [ ] Confrontare con path topologico attuale

---

### I4. Direct Float→YUV Encoding (elimina 8MB alloc/frame)

**Problema:** `frame_converter.cpp` fa: **Float RGBA → RGBA8 (heap alloc 8MB) → YUV420P**. Ogni frame paga 8MB di alloc + copia.

**Soluzione:** Kernel SIMD che converte **direttamente da float RGBA a YUV420P** senza buffer intermedio:

```
Oggi:   float[4] → uint8[4] (heap alloc) → libyuv::ABGRToI420() → YUV
Futuro: float[4] → SIMD BT.601 matrix → YUV420P direttamente
```

La matrice BT.601:
```
Y  =  0.299R + 0.587G + 0.114B
Cb = -0.169R - 0.331G + 0.500B + 128
Cr =  0.500R - 0.419G - 0.081B + 128
```

Con AVX2 si processano **8 pixel alla volta** (8 × 4 float = 256 bit = un registro YMM). Il subsampling 4:2:0 si fa al volo leggendo 2×2 blocchi.

**Dove:**
- `src/video/float_to_yuv_simd.cpp` (nuovo) — kernel SIMD direct float→YUV
- `src/video/frame_converter.cpp` — nuovo path `convert_float_to_yuv420p()`
- `apps/chronon3d_cli/utils/video/ffmpeg_pipe_write_frame.cpp` — usa il nuovo kernel

**Guadagno stimato:** Elimina 8MB alloc/frame + il copy, ~2-3× più veloce sul path encoding.

**Prossimi passi:**
- [ ] Implementare `float_to_yuv420p_avx2()` con BT.601 matrix
- [ ] Gestire subsampling 4:2:0 (2×2 → 1 chroma pixel)
- [ ] Sostituire `convert_frame_tight()` con il nuovo kernel
- [ ] Benchmark: `frame_conversion_copy_ms` prima/dopo

---

### I5. Per-Frame Arena senza Pool (elimina 226 pool miss)

**Problema:** Il `FramebufferPool` ha 226 size mismatches perché le chiavi di acquire/release non matchano (bucket rounding diverso). Ogni miss genera un'allocazione fresh su heap. In più: mutex overhead, chrono overhead (2× `high_resolution_clock::now()` per acquire), custom deleter con `weak_from_this()`.

**Soluzione:** Bump allocator per-frame senza pool, senza mutex, senza bucket:

```
Oggi:   Pool (mutex + bucket scan + weak_ptr + clear) × 300 nodi × 150 frame
Futuro: Bump allocator (atomic fetch_add) → reset a fine frame
```

**Meccanismo:**
1. All'inizio del frame, alloca un blocco contiguo pre-calcolato (es. 300 × 33MB = 9.9GB virtuali, fisicamente solo le pagine toccate)
2. Ogni nodo fa bump allocation: `ptr += size; return ptr;` — costo: 1 istruzione
3. Alla fine del frame, reset del puntatore a zero — costo: 1 istruzione
4. Nessun mutex, nessun reference counting, nessun bucket scanning

**Dove:**
- `include/chronon3d/core/frame_arena_allocator.hpp` (nuovo) — bump allocator dedicato ai framebuffer
- `src/render_graph/executor/graph_executor_phases.cpp` — usa arena invece di pool
- `src/cache/framebuffer_pool.cpp` — legacy, mantenuto per compatibilità

**Guadagno stimato:** Elimina 226 pool miss + 1.2K ms clear ridondanti + mutex overhead. ~30-40% speedup sul allocation path.

**Prossimi passi:**
- [ ] Misurare `framebuffer_acquire_ms` + `framebuffer_pool_miss_count_size_mismatch` come baseline
- [ ] Implementare `FrameArenaAllocator::acquire(w, h)` — bump allocation lineare
- [ ] Implementare `FrameArenaAllocator::reset()` — puntatore a zero
- [ ] Sostituire `pool->acquire_unique()` con `arena.acquire()` nel graph executor
- [ ] Benchmark: throughput prima/dopo su `ImgReferenceShakeReveal`

---

### I6. Procedural Grid Kernel (25× sulla grid background)

**Problema:** `dark_grid_background()` oggi rasterizza la griglia pixel-by-pixel (`dark_grid_background.hpp:86-103`) con `std::round`, `std::abs`, `std::max` per ogni pixel — e poi la salva su disco come PNG e la ricarica. Inoltre, il `SoftwareGridBackgroundProcessor` fa la stessa cosa con TBB ma in un percorso separato.

**Soluzione:** Kernel SIMD dedicato che calcola la griglia con 4-8 FMA per pixel invece del sampling bilineare generico:

```cpp
void render_grid_tile(Color* __restrict__ dst, int tile_x, int tile_y,
                       int tile_w, int tile_h, const GridParams& p) {
    for (int y = 0; y < tile_h; ++y) {
        for (int x = 0; x < tile_w; ++x) {
            float gx = (tile_x * tile_w + x + p.offset_x) / p.cell_width;
            float gy = (tile_y * tile_h + y + p.offset_y) / p.cell_height;
            float gx_frac = gx - floorf(gx);
            float gy_frac = gy - floorf(gy);
            bool is_line = (gx_frac < p.line_thickness || gy_frac < p.line_thickness);
            dst[y * tile_w + x] = is_line ? p.line_color : p.bg_color;
        }
    }
}
```

**Dove:**
- `src/backends/software/procedural/grid_background.cpp` (nuovo) — kernel `render_grid_tile()`
- `include/chronon3d/render_graph/nodes/procedural_source_node.hpp` (nuovo) — `ProceduralSourceNode`
- `src/render_graph/builder/graph_builder_pipeline.cpp` — se shape è Procedural, usa il nodo dedicato

**Guadagno stimato:** Da ~500μs (sampling bilineare) a ~20μs (kernel SIMD 4 FMA/pixel) = **25× speedup**.

**Prossimi passi:**
- [ ] Creare `render_grid_tile()` kernel SIMD
- [ ] Creare `ProceduralSourceNode` — accetta `GridParams` e produce tile
- [ ] Registrare come `ShapeType::ProceduralGrid` nel `ShapeRegistry`
- [ ] Integrare in `GraphBuilder::build()` — routing automatico
- [ ] Benchmark: confronto direct kernel vs BLImage sampling

---

### I7. Pre-bake con Memory-Mapped Files (zero I/O a runtime)

**Problema:** Il `DiskNodeCache` (già implementato in `disk_node_cache.hpp`) salva/ri-carica nodi statici su disco. Ma il caricamento avviene ancora con `read()` + copia in memoria. Inoltre, `dark_grid_background()` oggi fa: rasterizza → salva PNG → ricarica come immagine → rasterizza di nuovo.

**Soluzione:** Memory-mapped files per i nodi statici pre-baked:

```
Oggi:   Load PNG → decode → rasterize → composite (ogni frame)
Futuro: mmap("grid_bg.rgba") → pointer diretto → composite (0 copy)
```

**Meccanismo:**
1. Pre-bake: il grid background viene renderizzato una volta e salvato come file `.rgba` (raw float, mmap-able)
2. A runtime: `mmap()` del file → il puntatore diretto è il contenuto del framebuffer
3. Nessuna copia, nessun decode, nessuna allocazione — il kernel legge direttamente dalla pagina mappata
4. Il file vive in page cache del OS → accesso successivo è da L3 cache

**Dove:**
- `src/cache/mmap_frame_cache.cpp` (nuovo) — `MmapFrameCache` con `get_or_bake()`
- `include/chronon3d/cache/mmap_frame_cache.hpp` (nuovo)
- `apps/chronon3d_cli/commands/command_bake_layer.cpp` — estendere con `--mmap-bake`

**Guadagno stimato:** Zero I/O a runtime per contenuto statico. Il grid background costa 0ms dopo il primo frame.

**Prossimi passi:**
- [ ] Implementare `MmapFrameCache::bake(key, framebuffer)` — salva come raw float
- [ ] Implementare `MmapFrameCache::get(key)` — mmap + direct pointer
- [ ] Integrare con `DiskNodeCache` — estendere con path mmap
- [ ] Test: misurare `framebuffer_acquire_ms` con/without mmap per grid background

---

### Tabella Riepilogativa — Innovazioni Strategiche

| ID | Innovazione | Speedup Stimato | Sforzo | Priorità |
|----|------------|----------------|--------|----------|
| **I1** | Render Graph Incrementale | 3-5× (composizioni statiche) | 🔴 Alta | **P0 — il game changer** |
| **I2** | SoA Framebuffer | 1.5-2× (kernel alpha) | 🟡 Media | P1 |
| **I3** | Wavefront Scheduling | 1.3-1.5× (cache locality) | 🟡 Media | P1 |
| **I4** | Direct Float→YUV | 2-3× (encoding path) | 🟢 Bassa | **P1 — quick win** |
| **I5** | Per-Frame Arena senza Pool | 30-40% (allocation path) | 🟡 Media | **P0 — elimina 226 miss** |
| **I6** | Procedural Grid Kernel | 25× (grid background) | 🟢 Bassa | **P0 — quick win** |
| **I7** | Pre-bake mmap | 10× (contenuto statico) | 🟢 Bassa | **P0 — quick win** |

---

**Primo passo concreto (innovazioni):** Implementare **I6 (Procedural Grid Kernel)** — 1-2 giorni, 25× speedup sulla grid background, zero modifiche architetturali. Poi **I4 (Direct Float→YUV)** — 1 giorno, elimina 8MB alloc/frame. Infine **I5 (Per-Frame Arena)** — 3-5 giorni, elimina il pool con tutti i suoi overhead.

**Conclusione:** La V3 non è "ottimizzare di più il DAG attuale". La V3 è **cambiare il modello da frame-based a tile-based**, con nodi procedurali specializzati, cache persistente per regione, e pipeline output disaccoppiata. Per il caso d'uso Chronon3D (composizioni 2.5D con animazioni, motion graphics, video export), questo vale più di qualunque micro-ottimizzazione locale.

I 5 progetti rivoluzionari (R1-R5) e le 7 innovazioni strategiche (I1-I7) rappresentano il passo SUCCESSIVO — ciò che viene DOPO V3. Non servono per arrivare a 60 FPS. Servono per arrivare a **200+ FPS** o per fare cose che oggi nessun motore 2.5D programmatico può fare.

**Primo passo concreto (V3):** Implementare il **Pillar 5 (Procedural Grid Kernel)** — è il più facile (1-2 giorni), dà il guadagno più immediato (25× sulla grid background), e non richiede modifiche architetturali al resto del motore. Dal vivo, si vede subito.

**Primo passo RIVOLUZIONARIO:** **R5 (LOD 2.5D)** — 1-2 giorni di prototipo, impatto potenziale 2-16×, e Chronon3D diventerebbe il primo motore 2.5D con Level-of-Detail per layer.
