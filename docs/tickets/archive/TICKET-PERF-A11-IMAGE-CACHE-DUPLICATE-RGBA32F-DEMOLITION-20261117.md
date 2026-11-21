# TICKET-PERF-A11-IMAGE-CACHE-DUPLICATE-RGBA32F-DEMOLITION-20261117 — A11: Rimozione del buffer duplicato `entry->gpu_rgba` (`std::vector<float>`) in `ImageCache` (-50% RAM per immagine)

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P1/P2 (Violazione Cat-3 anti-duplication di memoria: ogni immagine alloca due copie identiche `RGBA32F` da 16 byte/pixel in RAM)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `src/backends/assets/image_cache.cpp` (righe 253–285)
  - `include/chronon3d/backends/assets/image_cache.hpp`
- **Owner:** C3D Assets / Memory maintainer

---

## 1. Diagnosi Esatta del Problema

In `src/backends/assets/image_cache.cpp:253-282`:

```cpp
entry->fb_img = std::make_shared<Framebuffer>(entry->width, entry->height);
if (entry->fb_img) {
    entry->gpu_rgba.resize(static_cast<std::size_t>(entry->width) * entry->height * 4);
    ...
    tbb::parallel_for(tbb::blocked_range<int>(0, entry->height),
        [&](const tbb::blocked_range<int>& rows) {
            for (int y = rows.begin(); y < rows.end(); ++y) {
                Color* dst_row = entry->fb_img->pixels_row(y);
                float* gpu_row = entry->gpu_rgba.data() +
                    static_cast<std::size_t>(y) * entry->width * 4;
                for (int x = 0; x < entry->width; ++x) {
                    const auto color = decode_image_pixel(...);
                    dst_row[x] = color;
                    gpu_row[x * 4 + 0] = color.r;
                    gpu_row[x * 4 + 1] = color.g;
                    gpu_row[x * 4 + 2] = color.b;
                    gpu_row[x * 4 + 3] = color.a;
                }
            }
        });
}
```

**Difetto verificato**:
`Framebuffer` memorizza un array contiguo di `Color` (`struct Color { float r, g, b, a; }`, `sizeof(Color) == 16`, `alignof(Color) == 4` o `16`).
`entry->gpu_rgba` è un `std::vector<float>` di dimensione `width * height * 4` (anch'esso 16 byte per pixel!) che contiene **esattamente gli stessi identici 4 float `r, g, b, a` nello stesso identico ordine** di `entry->fb_img`!
- Per una singola immagine 4K (`3840 × 2160`), `entry->fb_img` alloca **132,71 MB** e `entry->gpu_rgba` alloca **altri 132,71 MB** contenenti gli stessi byte!
- Inoltre, il ciclo `tbb::parallel_for` scrive il doppio della banda RAM (265 MB scritti invece di 132 MB), dimezzando la velocità di decodifica/conversione colore dell'immagine!

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo 1 — Esporre una vista `std::span<const float>` direttamente su `entry->fb_img`
In `include/chronon3d/backends/assets/image_cache.hpp` e `src/backends/assets/image_cache.cpp`:
1. Verificare con `static_assert(sizeof(Color) == 4 * sizeof(float) && std::is_standard_layout_v<Color>);` che `Color` sia layout-compatibile con `float[4]`.
2. In `ImageCacheEntry`:
   - Rimuovere il membro `std::vector<float> gpu_rgba;` (o lasciarlo vuoto come alias deprecato se esposto in header, ma verificare i call site con `git grep gpu_rgba`).
   - Fornire un metodo accessor `std::span<const float> gpu_rgba_span() const noexcept` che restituisce:
     ```cpp
     if (!fb_img || !fb_img->data()) return {};
     return std::span<const float>(
         reinterpret_cast<const float*>(fb_img->data()),
         static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
     ```
3. Nel `tbb::parallel_for` di `src/backends/assets/image_cache.cpp:261-282`:
   - Scrivere **solo** `dst_row[x] = color;`, eliminando `entry->gpu_rgba.resize(...)` e le 4 scritture `gpu_row[x * 4 + ...]`.

---

## 3. Demolition Debt

Nessuno: `entry->gpu_rgba` viene eliminato direttamente in favore dello storage unico `entry->fb_img`.

---

## 4. Verifica e Criteri di Chiusura

1. **Test Unitari Asset e GPU Upload**:
   - `chronon3d_core_tests` (casi `ImageCache` e asset manifest) e `chronon3d_backend_registry_tests` (upload texture Vulkan e CUDA) 100% PASS.
2. **Dimezzamento RAM e Scritture**:
   - `git grep gpu_rgba` non mostra più alcun `std::vector<float>` duplicato in `ImageCacheEntry`.
