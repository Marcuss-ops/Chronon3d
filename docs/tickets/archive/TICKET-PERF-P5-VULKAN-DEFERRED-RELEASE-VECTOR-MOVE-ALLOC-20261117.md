# TICKET-PERF-P5-VULKAN-DEFERRED-RELEASE-VECTOR-MOVE-ALLOC-20261117 — P5: Eliminazione delle riallocazioni heap in `flush_deferred_surface_releases` causate da `std::move` + `reserve`

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P2 (Quick Win zero-allocation nel ciclo di gestione superfici Vulkan)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `src/backends/vulkan/vulkan_surface_store_private.cpp` (righe 140–152)
  - `src/backends/vulkan/vulkan_surface_authority.hpp`
- **Owner:** C3D Vulkan backend maintainer

---

## 1. Diagnosi Esatta del Problema

In `src/backends/vulkan/vulkan_surface_store_private.cpp:140-142`:

```cpp
auto pending = std::move(surfaces.deferred_surface_releases);
surfaces.deferred_surface_releases.clear();
surfaces.deferred_surface_releases.reserve(pending.size());
```

**Difetto verificato**:
In C++, `auto pending = std::move(surfaces.deferred_surface_releases);` trasferisce il puntatore al buffer heap da `surfaces.deferred_surface_releases` alla variabile locale `pending`, lasciando `surfaces.deferred_surface_releases` con `capacity() == 0`.
La riga immediatamente successiva `surfaces.deferred_surface_releases.reserve(pending.size());` è quindi costretta ad allocare un **nuovo blocco sull'heap (`operator new`)** ad ogni chiamata di `flush_deferred_surface_releases()`, mentre il vecchio blocco posseduto da `pending` viene deallocato (`operator delete`) alla riga 175 all'uscita dalla funzione!

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo 1 — Aggiungere un vector scratch persistente in `VulkanSurfaceAuthority`
In `src/backends/vulkan/vulkan_surface_authority.hpp`:
- Accanto a `std::vector<DeferredSurfaceRelease> deferred_surface_releases;`, aggiungere:
  ```cpp
  std::vector<DeferredSurfaceRelease> deferred_surface_releases_scratch;
  ```
- Nel metodo `destroy_all()` (`vulkan_surface_store_private.cpp:190`), pulire anche `deferred_surface_releases_scratch.clear();`.

### Passo 2 — Sostituire `std::move` + `reserve` con `std::swap`
In `src/backends/vulkan/vulkan_surface_store_private.cpp:140-142`:
```cpp
auto& pending = surfaces.deferred_surface_releases_scratch;
pending.clear();
std::swap(pending, surfaces.deferred_surface_releases);
```
Con `std::swap`, entrambi i vector mantengono il proprio buffer heap allocato al high-water mark:
- `pending` contiene gli elementi da processare;
- `surfaces.deferred_surface_releases` è vuoto ma conserva la `capacity()` del giro precedente per ricevere eventuali `surfaces.enqueue_deferred_release(item)` alla riga 150 senza riallocare;
- a fine funzione non viene chiamato alcun `free` / `delete`.

---

## 3. Demolition Debt

Nessuno.

---

## 4. Verifica e Criteri di Chiusura

1. **Zero Allocazioni in Steady State**:
   - Nessuna allocazione heap in `flush_deferred_surface_releases()` dopo il warmup dei primi 2 flush.
2. **Suite di Test**:
   - `chronon3d_backend_registry_tests` e `chronon3d_render_graph_tests` verdi.
