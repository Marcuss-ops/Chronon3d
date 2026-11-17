# TICKET-PERF-DOF-PASSTHROUGH-ZERO-COPY-20261117 — Eliminazione della copia profonda (`clone_input`) in `PerPixelDofNode` quando il DoF è inattivo o fuori ROI

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P2 (Risparmio di 33 MB/frame a 1080p e 132 MB/frame a 4K nei frame a fuoco o con DoF disabilitato)
- **Ambito:** `src/render_graph/nodes/per_pixel_dof_node.cpp`, `src/render_graph/executor/`
- **Owner:** C3D RenderGraph / Camera maintainer
- **Allineamento piano:** `docs/CHRONON_PLAN.md` P2-09 (`Reduce compositing passes only with profiler evidence`)

---

## 1. Problema e Evidenza nel Codice

In `src/render_graph/nodes/per_pixel_dof_node.cpp:225-352`, `PerPixelDofNode::execute()` azzera incondizionatamente `ctx.node_exec.reusable_bottom.reset()` all'inizio del metodo (riga 230) e definisce una lambda `cloned_result()` che alloca un nuovo framebuffer (`ctx.acquire_owned_fb`) e copia l'intero contenuto dell'input:
- Sul percorso GPU nativo (`per_pixel_dof_node.cpp:291-325`): crea una nuova superficie Vulkan nel `surface_registry` e invoca `copy_surface_auxiliary(native_handle, input.surface_handle())` (copia GPU full-frame).
- Sul percorso software (`per_pixel_dof_node.cpp:327-336`): esegue `std::memcpy` riga per riga per l'intera risoluzione (`1920×1080×16 byte = 33,17 MB` per frame in `RGBA32F`).

Questa clonazione completa viene eseguita anche nei **4 rami di puro passthrough (no-op)** in cui il nodo **non applica alcuna sfocatura e non muta un singolo pixel**:
1. Riga 377: `if (!camera.dof.enabled) return cloned_result();`
2. Riga 404: `if (!coverage.source_bbox || coverage.max_radius < kVisibleBlurThreshold) return cloned_result();` (tutti i livelli sono a fuoco nel frame corrente, es. prima o dopo un rack-focus).
3. Riga 418: `if (roi.is_empty()) return cloned_result();`
4. Riga 487 / 497 / 555: `if (dof_depth.empty()) return cloned_result();` e `if (!clip) return cloned_result();`

### Perché è un problema
Il motivo per cui `PerPixelDofNode` clona l'input quando *effettua* il blur è che il kernel DoF muta il buffer di destinazione in-place e non deve corrompere un buffer condiviso con la cache o altri consumatori. Ma quando il nodo è un **no-op**, il buffer di input non viene toccato! Eseguire una copia di 33 MB (CPU) o un pass `copy_surface_auxiliary` con allocazione di superficie (GPU) per un nodo che non fa nulla spreca banda di memoria, slot del `FramebufferPool` e pass GPU.

---

## 2. Come Chiudere Completamente il Ticket (Step-by-Step)

### Passo 1 — Analizzare i 2 casi di ownership del risultato in passthrough
Quando `PerPixelDofNode::execute()` determina che il frame corrente è un **passthrough** (nessun blur da applicare):

1. **Caso Terminale Video Encode (`ctx.policy.native_video_encode_surface != kInvalidRenderSurfaceHandle`)**:
   - Se `PerPixelDofNode` è il nodo di output finale del grafo (`id == compiled.output`) e l'input `inputs[0]->surface_handle()` **non è già** `ctx.policy.native_video_encode_surface`, allora la copia verso `encode_destination` (righe 258-289) è necessaria per consegnare i pixel nella superficie di encode NVENC dello slot. Se invece `inputs[0]->surface_handle() == encode_destination`, non serve alcuna copia.
2. **Caso Ordinario (Intermedio o Software o già su `reusable_bottom`)**:
   - **Non resettare `ctx.node_exec.reusable_bottom` alla riga 230 prima di sapere se il DoF verrà davvero eseguito!**
   - Spostare `ctx.node_exec.reusable_bottom.reset()` **dopo** i check di early-return passthrough, subito prima di allocare la superficie per il vero blur.
   - Nei rami di passthrough (`!camera.dof.enabled`, `max_radius < kVisibleBlurThreshold`, `roi.is_empty()`, `!clip`):
     - Se `ctx.node_exec.reusable_bottom` è presente e punta allo stesso buffer di `inputs[0]` (perché `consumer_remaining == 1` nel `GraphExecutor` e il buffer non è trattenuto in cache), restituire direttamente `std::move(ctx.node_exec.reusable_bottom)` in **O(1) zero-copy** (zero `memcpy`, zero `create_surface`, zero `copy_surface_auxiliary`)!
     - Solo se `reusable_bottom` è nullo (perché l'input ha altri consumatori a valle o è pinnato nella `NodeCache` e il contratto di `NodeExecResult` richiede un `OwnedFB` distinto), eseguire `cloned_result()`.

### Passo 2 — Pruning statico nel compilatore (Opzionale / Complementare)
- Se nel `RenderPlan` / `CompiledProgram` la camera non ha DoF abilitato né tracce animate su `dof.enabled` / `aperture` per l'intera durata del job, verificare che il graph builder non istanzi `PerPixelDofNode` o lo marchi in `interior_node_skip`.

---

## 3. Demolition Debt

Nessuno: la modifica usa il meccanismo canonico `ctx.node_exec.reusable_bottom` già fornito da `GraphExecutor` a tutti i nodi di compositing/trasformazione.

---

## 4. Verifica e Criteri di Accettazione (Definition of Done)

1. **Test di Regressione Lifetime & Residency**:
   - Aggiungere un test in `tests/renderer/camera/test_per_pixel_dof.cpp` che verifica:
     - (a) Con `dof.enabled = false` e `reusable_bottom` fornito (singolo consumatore), `PerPixelDofNode::execute()` restituisce lo stesso puntatore/handle senza allocare dal `FramebufferPool` né emettere `copy_surface_auxiliary`.
     - (b) Con `dof.enabled = false` ma `reusable_bottom = nullptr` (input condiviso/cachato), l'input originale rimane intatto se un nodo successivo muta l'output di `PerPixelDofNode`.
     - (c) Con `native_video_encode_surface` impostata come target terminale, l'handoff alla superficie di encode funziona senza regressioni.
2. **Suite Obbligatorie Verdi**:
   - `chronon3d_renderer_core_tests` (inclusi tutti i casi DoF e motion blur)
   - `chronon3d_backend_registry_tests`
   - `chronon3d_compositor_tests`
   - `chronon3d_render_graph_tests`
   - Verifica sul caso C6 (150 frame rack-focus) che i frame in cui il soggetto attraversa il piano focale mantengano parità bit-exact.
