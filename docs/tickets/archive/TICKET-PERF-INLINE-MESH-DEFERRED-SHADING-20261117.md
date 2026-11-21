# TICKET-PERF-INLINE-MESH-DEFERRED-SHADING-20261117 — Deferred shading e campionamento texture fuori dal loop dei triangoli in `inline_mesh.comp`

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P2 (Quick Win GPU Shader — Alto guadagno su scene 3D/glTF, zero cambi ABI)
- **Ambito:** `src/backends/vulkan/shaders/inline_mesh.comp`
- **Owner:** C3D Vulkan shader / 3D rendering maintainer
- **Allineamento piano:** `docs/CHRONON_PLAN.md` P2-01 (`Profile inline_mesh.comp at 10/100/1k/10k triangles`)

---

## 1. Problema e Evidenza nel Codice

In `src/backends/vulkan/shaders/inline_mesh.comp:189-235`, ogni thread (1 pixel del bounding box della mesh) itera su tutti i triangoli della mesh per trovare il frammento più vicino (`nearest_depth`).
Attualmente, **all'interno del corpo del ciclo `for`**, per ogni singolo triangolo che copre il pixel `(p.x, p.y)` e ha `depth < nearest_depth`, lo shader esegue immediatamente:

1. L'interpolazione prospettica di `uv`, `light` e `normal` (`inline_mesh.comp:209-217`);
2. Il campionamento bilineare manuale della texture base (`sample_texture(uv)` a riga 218, pari a **4 chiamate `imageLoad`**);
3. Il calcolo completo dell'illuminazione PBR per-pixel (`evaluate_material_shading(uv, normal)` a riga 224), che a sua volta:
   - normalizza vettori e calcola `pow(ndoth, shininess)` e `pow(1.0 - ndotv, 2.2)`;
   - invoca `sample_metallic_roughness(uv)` (`inline_mesh.comp:149`), eseguendo **altre 4 chiamate `imageLoad`** sulla texture metallic-roughness!

### Conseguenza prestazionale
Se per un dato pixel della mesh ci sono $K$ triangoli sovrapposti (es. schiena, busto, braccio di un personaggio 3D) e l'ordine degli indici nel buffer non è strettamente front-to-back rispetto alla camera corrente, lo shader esegue fino a $8 \times K$ letture `imageLoad` e $K$ valutazioni PBR complete per poi sovrascrivere `nearest_color` quando incontra un triangolo più vicino. Anche con `has_alpha_cutoff != 0`, il calcolo `evaluate_material_shading` (inclusi i 4 `imageLoad` della mappa metallic-roughness) viene eseguito dentro il loop nonostante non influenzi in alcun modo il test di alpha cutoff (`alpha < params.alpha_cutoff` dipende solo da `texel.a * params.tint_a`).

---

## 2. Come Chiudere Completamente il Ticket (Step-by-Step)

### Passo 1 — Separare la ricerca di visibilità (Hit Record) dallo Shading in `inline_mesh.comp`

In `src/backends/vulkan/shaders/inline_mesh.comp:185-236`:

1. **Caso A: Mesh senza Alpha Cutoff (`params.has_alpha_cutoff == 0` e `params.tint_a > 1e-6`)**:
   - Se la texture base è opaca o se vogliamo comunque testare `alpha <= 1e-6` solo sul vincitore (attenzione: verificare se esistono fixture senza `has_alpha_cutoff` che usano texel con `alpha == 0` come buchi trasparenti attraverso cui si vede un triangolo posteriore della *stessa* mesh!):
   - **Soluzione 100% equivalente anche con buchi `alpha <= 1e-6`**:
     - Nel loop, posticipare **sempre** `evaluate_material_shading(uv, normal)`, l'interpolazione di `light` e `normal`, e l'applicazione di `emissive` **fuori dal loop**!
     - Nel loop dei triangoli, quando `depth < nearest_depth`:
       - Calcolare solo `uv` e `vec4 texel = sample_texture(uv);`
       - Eseguire i due test di scarto (`if (params.has_alpha_cutoff != 0 && alpha < params.alpha_cutoff) continue;` e `if (alpha <= 1e-6) continue;`).
       - Se il frammento è valido, salvare solo il **record del triangolo vincente**:
         ```glsl
         nearest_depth = depth;
         best_ia = ia;
         best_ib = ib;
         best_ic = ic;
         best_w0 = w0;
         best_w1 = w1;
         best_w2 = w2;
         best_inv_depth = inverse_depth;
         best_uv = uv;
         best_texel = texel;
         covered = true;
         ```
2. **Fuori dal loop (`if (covered)`)**:
   - Calcolare **una sola volta** per il pixel:
     - `best_light` (se `(params.shading_flags & 1) == 0`) oppure `best_normal` + `evaluate_material_shading(best_uv, best_normal)` (se `(params.shading_flags & 1) != 0`).
     - In questo modo **i 4 `imageLoad` di `metallic_roughness_image`, le normalizzazioni, i `pow()` PBR e l'interpolazione delle normali vengono eseguiti al massimo 1 volta per pixel coperto**, mai per triangoli occlusi!
3. **Ottimizzazione aggiuntiva per mesh opache certificabili**:
   - Se una mesh non ha alpha cutoff e la texture è opaca (flag nei `shading_flags`, es. bit `64 = kShadingOpaqueTexture`), anche `sample_texture(uv)` e l'interpolazione `uv` escono interamente dal loop `for`, lasciando nel loop esclusivamente il test barycentrico 2D e il calcolo `1.0 / inverse_depth`.

### Passo 2 — Ricompilazione SPIR-V e Verifica ABI
- Compilare il target degli shader (`glslangValidator → spirv-val → spirv-opt`) tramite build `linux-fast-dev`.
- Verificare che `tests/test_shader_layout_contract.py` passi senza modifiche (la dimensione e i binding dei push constant a 128 byte e dei 5 descriptor rimangono identici).

### Passo 3 — Benchmark prima/dopo (P2-01)
- Misurare i tempi GPU di `inline_mesh.comp` (tramite i timestamp per-pass già registrati in `VulkanBackend::Impl::emit_pass_sync` / `completed_frame_timings`) su scene con 10, 100, 1.000 e 5.088 triangoli (`low-poly_man` fixture E3 e `inline-mesh-pbr-vulkan` fixture E1).

---

## 3. Demolition Debt

Nessuno: modifica interna al kernel `inline_mesh.comp` senza alterare ABI, descriptor layout o contratti host.

---

## 4. Verifica e Criteri di Accettazione (Definition of Done)

1. **Parità Pixel (Bit-Exact / Golden Parity)**:
   - La suite `chronon3d_backend_registry_tests` (inclusi i test PBR inline mesh Vulkan E1 e camera/mesh P0-04, P0-05, P0-06, P0-07) passa con `MAE = 0` rispetto all'output precedente.
2. **Riduzione Istruzioni / Letture Memoria**:
   - `sample_metallic_roughness` e `evaluate_material_shading` non compaiono più dentro il loop `for (int i = 0; i + 2 < params.index_count; i += 3)` di `inline_mesh.comp`.
3. **Evidenza Benchmark**:
   - Tabella numerica allegata in `docs/evidence/` con il delta in millisecondi GPU su mesh multi-triangolo con PBR attivo.
