# TICKET-PERF-SDF-BEVEL-PASS-FUSION-AND-OCCUPANCY-20261117 — Fusione dei pass 1D (Mode 0+1 e Mode 2+3) e parallelismo workgroup cooperativo in `sdf_bevel.comp`

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P2 (Eliminazione sotto-utilizzo SM e dimezzamento dei dispatch/barriere in `sdf_bevel.comp`)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `src/backends/vulkan/shaders/sdf_bevel.comp` (righe 23, 58–215)
  - `src/backends/vulkan/` (sito di registrazione dei 5 dispatch di `sdf_bevel`)
- **Owner:** C3D Vulkan Shaders / Effects maintainer

---

## 1. Diagnosi Esatta del Problema

In `src/backends/vulkan/shaders/sdf_bevel.comp:23` e `:58-120`:

```glsl
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
...
if (mode == 0) {
    const int y = int(gl_GlobalInvocationID.x);
    if (y >= size.y) return;
    ...
    for (int x = 0; x < size.x; ++x) { ... }
}
```

**Due difetti verificati**:
1. **Bassa Occupancy Hardware**: in `mode == 0` e `mode == 1`, c'è 1 solo thread per intera riga `y` (`1.080` thread totali a 1080p = 17 workgroup da 64 thread, insufficienti a saturare una GPU con 48+ SM), e ogni thread esegue un loop seriale di `1.920` iterazioni con `imageLoad` e `imageStore`.
2. **Passate Left (mode 0) e Right (mode 1) separate con round-trip completo in VRAM da 33 MB**:
   - `mode 0` scansiona la riga `y` da sinistra a destra (`x = 0 .. width - 1`) e scrive l'intera immagine `destination_image` in VRAM (`rgba32f`, 33 MB a 1080p);
   - Il backend emette una barriera di memoria Vulkan;
   - `mode 1` rilegge dalla VRAM l'intera immagine appena scritta da `mode 0` (`source_image`) **sulla stessa identica riga `y`** scansionandola da destra a sinistra (`x = width - 1 .. 0`)!
   Poiché la riga `y` in `mode 1` dipende **esclusivamente** dalla stessa riga `y` di `mode 0`, separarle in due dispatch con barriera globale e 66 MB di traffico VRAM (33 MB write + 33 MB read) è completamente superfluo!

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo 1 — Fondere Mode 0 + Mode 1 in un singolo pass (senza cambiare l'esattezza matematica dell'EDT!)
1. **Nota fondamentale sulla parità**: non sostituire la Euclidean Distance Transform (EDT) esatta con un Jump Flooding Algorithm (JFA) approssimato che romperebbe la parità con l'oracolo software `effect_sdf_bevel_impl.cpp`.
2. Invece, **fondere la scansione Left + Right della riga `y` in un unico dispatch**:
   - Assegnando 1 workgroup (es. `local_size_x = 256`) a ciascuna riga `y` (`1.080` workgroup = **276.480 thread**, saturando al 100% tutti i 48 SM della GPU!):
   - I 256 thread della riga caricano `imageLoad(source_image, ivec2(x, y)).a` una sola volta in `shared` memory;
   - Calcolano in shared memory sia il seed sinistro (`mode 0`) sia il seed destro (`mode 1`) con una riduzione parallela (parallel prefix min/max in 8 step logaritmici in shared memory);
   - Scrivono direttamente l'output combinato di `mode 1` in `destination_image` in **1 solo dispatch invece di 2**, eliminando 1 dispatch, 1 barriera Vulkan, 1 lettura completa di `aux_image` e 66 MB di traffico VRAM!

### Passo 2 — Fondere Mode 2 + Mode 3 (Colonne Forward + Backward) con lo stesso schema
- Analogamente, per la trasformata verticale (colonne `x = 0 .. width - 1`), eseguire forward + backward in un singolo pass cooperativo per colonna (o su blocchi trasposti in shared memory `16×16` per avere accessi coalescenti), riducendo l'intero effetto SDF Bevel da **5 dispatch a 3 dispatch** con parità matematica **100% identica (`MAE = 0`)**!

---

## 3. Demolition Debt

Nessuno: i mode fusi sostituiscono direttamente la coppia di dispatch sequenziali senza cambiare i parametri dell'effetto.

---

## 4. Verifica e Criteri di Chiusura

1. **Parità Pixel SDF Bevel**:
   - Il test di parità Vulkan `SDF Bevel` in `chronon3d_effects_tests` deve passare con gli stessi identici valori pixel (`MAE = 0` rispetto alla versione a 5 pass).
2. **Riduzione Pass e Tempo GPU**:
   - Numero di dispatch per SDF Bevel ridotto da 5 a 3; tempo GPU misurato via timestamp query a 1080p.
