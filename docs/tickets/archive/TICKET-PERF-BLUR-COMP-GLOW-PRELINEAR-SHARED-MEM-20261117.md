# TICKET-PERF-BLUR-COMP-GLOW-PRELINEAR-SHARED-MEM-20261117 — Eliminazione del 99% delle chiamate `pow(..., 2.4)` dal loop dei tap e pre-caricamento in shared memory in `blur.comp` (Glow)

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P1 (Ottimizzazione del pass GPU più costoso del FullGraph: Glow `blur.comp`)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `src/backends/vulkan/shaders/blur.comp` (righe 19–68)
- **Owner:** C3D Vulkan Shaders / Effects maintainer

---

## 1. Diagnosi Esatta del Problema

In `src/backends/vulkan/shaders/blur.comp:34-54`, quando `params.box_kernel == 2` (modalità Glow display-referred), il ciclo interno sui tap del filtro esegue:

```glsl
for (int offset = -extent; offset <= extent; ++offset) {
    ivec2 sample_pixel = pixel + (params.horizontal != 0
        ? ivec2(offset, 0) : ivec2(0, offset));
    weight_sum += weight;
    if (sample_pixel.x >= 0 && sample_pixel.x < size.x &&
        sample_pixel.y >= 0 && sample_pixel.y < size.y) {
        vec4 sample_value = imageLoad(source_image, sample_pixel);
        float alpha = clamp(sample_value.a, 0.0, 1.0);
        if (alpha > 1.0e-8) {
            vec3 straight_srgb = max(sample_value.rgb / alpha, vec3(0.0));
            vec3 straight_linear = mix(
                straight_srgb / 12.92,
                pow((straight_srgb + vec3(0.055)) / 1.055, vec3(2.4)),
                greaterThan(straight_srgb, vec3(0.04045)));
            accumulated.rgb += straight_linear * alpha * weight;
            accumulated.a += alpha * weight;
        }
    }
    weight *= weight_ratio;
    weight_ratio *= ratio_step;
}
```

**Difetto verificato**:
1. Per un raggio ampio (`extent` fino a 128 → 257 tap per passata × 2 passate = 514 tap per pixel), ogni singolo thread rilegge da `source_image` gli stessi pixel che i thread adiacenti del workgroup stanno leggendo, ed esegue **3 chiamate `pow(..., 2.4)` sulla Special Function Unit (SFU) della GPU per ogni singolo tap** (fino a $3 \times 514 \times 2.073.600 \approx 3,2 \times 10^9$ `pow()` per frame 1080p!).
2. La conversione `sample_value -> vec4(straight_linear * alpha, alpha)` dipende **unicamente dal pixel sorgente `sample_pixel`**, non da `offset` né dal pixel di destinazione! Ricalcolarla 257 volte per lo stesso `sample_pixel` è puro spreco computazionale.

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo 1 — Funzione helper di decodifica + Caching in `shared` memory in `blur.comp`
In `src/backends/vulkan/shaders/blur.comp`:
1. Estrarre la funzione pura:
   ```glsl
   vec4 decode_glow_linear_premul(ivec2 coord, ivec2 size) {
       if (coord.x < 0 || coord.x >= size.x || coord.y < 0 || coord.y >= size.y)
           return vec4(0.0);
       vec4 sample_value = imageLoad(source_image, coord);
       float alpha = clamp(sample_value.a, 0.0, 1.0);
       if (alpha <= 1.0e-8) return vec4(0.0);
       vec3 straight_srgb = max(sample_value.rgb / alpha, vec3(0.0));
       vec3 straight_linear = mix(
           straight_srgb / 12.92,
           pow((straight_srgb + vec3(0.055)) / 1.055, vec3(2.4)),
           greaterThan(straight_srgb, vec3(0.04045)));
       return vec4(straight_linear * alpha, alpha);
   }
   ```
2. Nel workgroup `16×16`, dichiarare un buffer in shared memory per la finestra di campioni lungo l'asse attivo (a tile/chunk se `extent > 32`, oppure con preload cooperativo della riga/colonna del workgroup `16 × (16 + 2 * chunk)`):
   - Ogni texel nella finestra viene caricato con `decode_glow_linear_premul(...)` **una sola volta** in `shared vec4 s_line[...]` prima di `barrier()`.
   - Nel loop dei tap, i 16 thread lungo l'asse del blur leggono direttamente `s_line[...]` ed eseguono **solo `accumulated += s_val * weight;` (1 singola FMA in shared memory invece di `imageLoad` + divisione + 3 `pow()`!)**.

---

## 3. Demolition Debt

Nessuno: modifica interna a `blur.comp` che preserva al 100% la matematica IEEE-754 e il contratto dell'oracolo CPU.

---

## 4. Verifica e Criteri di Chiusura

1. **Parità Pixel Assoluta**:
   - `chronon3d_effects_tests` (`Vulkan*` Glow / Blur cases) e `chronon3d_backend_registry_tests` 100% PASS con gli stessi identici valori pixel.
2. **Misura Guadagno GPU**:
   - Confrontare il tempo pass GPU di `blur.comp` a `radius = 12, 24, 48` a 1080p prima e dopo.
