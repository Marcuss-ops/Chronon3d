# TICKET-PERF-SOFTWARE-BACKDROP-AND-MULTI-SOURCE-BBOX-20261117 — Eliminazione delle 3 allocazioni `std::vector<Color>` full-frame (100 MB a 1080p) in `execute_backdrop_software` e clipping BBox

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P2 (Eliminazione page-fault e allocazioni da 100–400 MB per frame nel backdrop blur software)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `src/render_graph/nodes/composite_backdrop_software.cpp` (righe 25–72)
- **Owner:** C3D RenderGraph / Compositor maintainer

---

## 1. Diagnosi Esatta del Problema

In `src/render_graph/nodes/composite_backdrop_software.cpp:25-32`:

```cpp
const i32 width = bottom.width();
const i32 height = bottom.height();
const std::size_t count = static_cast<std::size_t>(width) * height;
std::vector<Color> source(count), horizontal(count), blurred(count);
for (i32 y = 0; y < height; ++y) {
    const auto* row = bottom.pixels_row(y);
    std::copy_n(row, width, source.data() + static_cast<std::size_t>(y) * width);
}
```

**Tre difetti verificati**:
1. **3 allocazioni `std::vector<Color>` full-canvas ad ogni frame**: a 1080p (`count = 2.073.600`, `sizeof(Color) = 16`), questa singola riga alloca **3 buffer da 33,17 MB ciascuno = 99,5 MB sull'heap** (a 4K sono **398 MB**!), forzando `mmap`/`munmap` e page-fault nel kernel Linux ad ogni singolo frame!
2. **Il buffer `source` è una copia inutile di `bottom`**: `bottom` è passato come `const Framebuffer&` ed è già `is_cpu_authoritative()` con `width × height` (verificato alla riga 19). Il passo orizzontale (riga 54) può leggere direttamente `bottom.pixels_row(y)[x]` senza allocare né copiare i 33 MB di `source`!
3. **Filtro Gaussiano H+V eseguito su tutto lo schermo `width × height` (single-threaded!)**: i due doppi cicli `for (i32 y = 0; y < height; ++y) for (i32 x = 0; x < width; ++x)` alle righe 51–68 sfocano l'intero frame anche quando il layer `top` (la card o il testo con effetto GEL/backdrop) copre solo una piccola porzione dello schermo.

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo 1 — Eliminare `source` e usare `ctx.acquire_owned_fb` (`FramebufferPool`) per `horizontal` e `blurred`
In `src/render_graph/nodes/composite_backdrop_software.cpp`:
1. Eliminare completamente `std::vector<Color> source(count)`: nella passata orizzontale, campionare direttamente da `bottom.pixels_row(y)[std::clamp(x + k, 0, width - 1)]`.
2. Invece di allocare `std::vector<Color> horizontal(count), blurred(count)` sull'heap di sistema, acquisire due framebuffer dal pool canonico tramite `auto horizontal = ctx.acquire_owned_fb(width, height, false);` e `auto blurred = ctx.acquire_owned_fb(width, height, false);` (che riutilizzano la memoria già residente nel `FramebufferPool` con **0 page-fault e 0 chiamate al kernel**!).

### Passo 2 — Restringere il Blur alla Bounding Box Attiva di `top` (espansa per la rifrazione/shift)
1. Calcolare prima il bounding box dei pixel non trasparenti di `top` (`top_x0, top_y0, top_x1, top_y1`), espanso del margine massimo di campionamento `sample_bilinear` (determinato da `refraction_px` + `chromatic_aberration_px` + `1`).
2. Eseguire il pass orizzontale solo per `y in [roi_y0 - extent, roi_y1 + extent]` e `x in [roi_x0, roi_x1]`, e il pass verticale solo per `y in [roi_y0, roi_y1]` e `x in [roi_x0, roi_x1]`.

---

## 3. Demolition Debt

Nessuno: sostituisce allocazioni heap grezze con il `FramebufferPool` canonico (famiglia `ResidencyCache`).

---

## 4. Verifica e Criteri di Chiusura

1. **Parità Pixel GEL / Backdrop**:
   - Tutti i 28 test unitari GEL e i 154 canary text tile (`chronon3d_compositor_tests` / `chronon3d_core_tests`) devono passare con **0 differenze pixel (`MAE = 0`)**.
2. **Zero `std::vector<Color>(count)`**:
   - Nessuna allocazione `std::vector<Color>` full-frame in `composite_backdrop_software.cpp`.
