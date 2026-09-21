# Evidenza — P0-2 contratto colore (TICKET-TEXT-QUALITY-V2)

Data: 2026-09-17 · SHA di partenza: `c6f4c69d1` · Lane: `linux-fast-dev`
(preset `linux-fast-dev`, `-j16`, ccache, `-fuse-ld=mold`).

Consuma P0-2 di [`docs/tickets/TICKET-TEXT-QUALITY-V2.md`](../tickets/TICKET-TEXT-QUALITY-V2.md)
§5 e l'ordine vincolante §5.1 voce 3: *«risolto alla boundary di authoring/lowering,
non dentro gli shader … deve diventare linear una volta sola prima di arrivare ai
backend (niente `to_linear()` separato in Vulkan e CUDA)»*.

## 1. Difetto riprodotto (F2) e secondo authority scoperta

`render_plan::parse_hex_color` (`include/chronon3d/render_plan/color_utils.hpp`)
è l'unico parser hex→`Color` del percorso `style` (text materializer
`render_plan_compiler_text.cpp` + subtitle `subtitle_style.hpp`).  Restituiva le
canali CSS grezzi (`/255`), cioè **sRGB**, che finivano direttamente in tile
**linear premultiplied** (`text_tile_raster.comp:5,9`) senza conversione.

La ricognizione ha però trovato una **seconda authority di conversione** non
dichiarata nel ticket: `src/media/video/direct_yuv/direct_yuv_program.cpp`
aveva un proprio `pack_text_color` con `to_linear()`, e commentava
«Text colours are authored in the public/sRGB representation … encode back …
exactly once in the CUDA boundary».  Le due lane quindi **non erano coerenti**:

| Lane | Prima di P0-2 | Effetto su un colore mid-tone |
|---|---|---|
| FullGraph (Vulkan) | nessuna linearizzazione | sRGB numerico dentro tile linear ⇒ difetto F2 |
| DirectYUV (CUDA) | `to_linear()` nel pack | già lineare, ma authority duplicata |

Conseguenza operativa: linearizzare *solo* alla boundary (la prescrizione di
§5.1) rende il `to_linear()` di DirectYUV una **doppia conversione**.  Entrambe
le metà vanno spostate/rimosse nella stessa consegna, altrimenti si introduce un
regressione latente.

### 1bis. Censimento `pack_text_color` e verifica del percorso software

Dopo P0-2 le tre chiamate backend usano una sola authority condivisa:
`include/chronon3d/text/text_color_packing.hpp`. Il corpo è una quantizzazione
pura del dominio lineare; nessun backend esegue `to_linear()`.

| Sede | Dopo | Lane |
|---|---|---|
| `include/chronon3d/text/text_color_packing.hpp` | **authority unica**: quantizzazione pura | tutti i backend |
| `src/media/video/direct_yuv/direct_yuv_program.cpp` | delega all'authority condivisa | DirectYUV |
| `src/render_graph/executor/node_runner_fused_batch.cpp` | delega all'authority condivisa | FullGraph |
| `src/render_graph/nodes/text_run/gpu_text_run.cpp` | delega all'authority condivisa | FullGraph |

**Il percorso software NON era già corretto.**  Questa è la differenza decisiva
rispetto a una lettura che mette la conversione nel packer GPU.  Il testo
software non usa `pack_text_color`: passa dal bridge Blend2D, e
`to_bl_rgba` (`blend2d/blend2d_paint.hpp`) converte un `Color` **lineare** in 8 bit
senza ri-linearizzare — coerente con il resto del software path, dove i
chiamanti fanno `.to_linear()` prima di `to_bl_rgba`.

Il ritorno in framebuffer però **non decodifica il transfer**:

* `blend2d_bridge_core.cpp::composite_bl_image` — `((p>>16)&0xFF) * inv255`
  scritto direttamente in `Color` (framebuffer lineare);
* `blend2d_bridge_transforms.cpp` → `sample_bilinear_prgb32` — stessa cosa per il
  path scalato/trasformato (quello usato da `composite_text_run`).

Nessun `srgb_to_linear`/`linear_from_srgb` in `src/backends/software/blend2d/`.
Quindi con `#808080` sRGB (0.502) il software scriveva byte 128 e lo componeva
come **lineare 0.502** ⇒ uscita ~187, **esattamente come FullGraph GPU**.

Conseguenza: mettere `to_linear()` nel packer GPU (e lasciare `parse_hex_color`
in sRGB) avrebbe corretto Vulkan e DirectYUV ma **lasciato sbagliato il software**,
spostando il drift GPU≠software invece di chiuderlo.  La boundary di authoring è
l'unico punto che copre **tutti e tre** i backend: nessuno dei tre converte
al proprio interno.

Nota residua: `highlight_color` (`vulkan_backend_text_ops_private.cpp` →
`text_run.comp` `params.highlight_color.rgb`) entra raw senza linearizzazione.
Non passa da `parse_hex_color`, quindi appartiene al residuo «colori non-hex»
(§6), non a P0-2.

## 2. Modifica

1. `include/chronon3d/render_plan/color_utils.hpp` — `parse_hex_color` applica
   `chronon3d::Color::to_linear()` (authority canonica in `math/color.hpp`) e
   documenta il **COLOR DOMAIN**: la conversione sRGB→linear avviene una sola
   volta qui; alpha è un fattore di copertura, non un canale, e resta invariato.
2. `include/chronon3d/render_plan/subtitle_style.hpp` — il commento di parity
   dichiara il dominio lineare condiviso e il divieto di ri-linearizzare.
3. `src/media/video/direct_yuv/direct_yuv_program.cpp` — rimosso il `to_linear()`
   dal `pack_text_color` DirectYUV: ora è pura quantizzazione 8-bit, identica
   al packer Vulkan (`node_runner_fused_batch.cpp` / `gpu_text_run.cpp`).
4. `tests/c_abi/test_prepared_render_plan.cpp` — ri-pin delle attese alla
   proiezione lineare + gate sul grigio medio.

## 3. Gate P0-2 — «golden re-pinnato + test sul grigio medio (#808080)»

| Gate | Esito | Evidenza |
|---|---|---|
| Test `#808080` (sRGB 0.501961 → linear 0.2158605) sulla boundary | PASS | `parse_hex_color lowers CSS sRGB hex to the linear working domain` |
| Test `#808080` sul percorso subtitle | PASS | `resolve_subtitle_style lowers mid-gray to the linear domain` |
| Alpha non convertito | PASS | stesso caso con `alpha=0.5` ⇒ `a == 0.5` esatto |
| Punti fissi `#FFFFFF`/`#000000` invariati (nessun golden di produzione si muove) | PASS | asserzioni dedicate nel caso boundary |
| Parity subtitle ↔ text materializer | PASS | caso `#FFC107` ri-pinnato alla proiezione lineare (`g == 0.53326`) |
| Golden ri-eseguiti (nessuna deriva) | PASS | 14 suite, 2460 casi, 0 failed (tabella §4) |
| Compilazione lane DirectYUV | PASS | `direct_yuv_program.cpp.o` ricostruito in `linux-video-release` |
| Certificazione runtime DirectYUV | **NOT RUN** | i preset video hanno `CHRONON3D_BUILD_TESTS=OFF`: la suite video non è costruibile in questa sessione; 0 job usano la lane su questo host (§4.1 del ticket) |

**Sanity sul raggio d'azione.** Tutti i piani di produzione/riferimento usano
`fill: #FFFFFF` e `shadow: #000000` (`refactored/ops/...`, `scripts/`), che sono
punti fissi della conversione: nessun golden di produzione cambia.  L'unico
consumatore che si muove è l'attesa ri-pinnata `#FFC107`.

## 4. Verifica di build e suite

Build: `cmake --build build/chronon/linux-fast-dev --target chronon3d_dev_fast -j16`
→ **333/333, 0 errori**.  RAM di picco osservata ≈ 19.7 GiB usati (host 29.4 GiB),
nessun OOM; swap non cresciuto oltre lo stato preesistente.

| Suite | Esito |
|---|---|
| `chronon3d_c_abi_tests` | 45/45 passed (276 assertion) — contiene i nuovi test P0-2 |
| `chronon3d_core_tests` | 1480/1480 passed |
| `chronon3d_scene_tests` | 422/422 passed (8 skipped) |
| `chronon3d_cache_tests` | 146/146 passed |
| `chronon3d_deterministic_tests` | 85/85 passed (3 skipped) |
| `chronon3d_cli_tests` | 75/75 passed |
| `chronon3d_compositor_tests` | 69/69 passed |
| `chronon3d_timeline_tests` | 40/40 passed |
| `chronon3d_optimizer_tests` | 22/22 passed |
| `chronon3d_pixel_program_tests` | 20/20 passed |
| `chronon3d_render_job_contract_tests` | 19/19 passed |
| `chronon3d_animation_helpers_tests` | 17/17 passed |
| `chronon3d_precomp_tests` | 14/14 passed |
| `chronon3d_text_health_tests` | 6/6 passed |

Nota operativa (lint staleness/CWD): i binari vanno eseguiti dalla **radice del
repo**.  Lanciati da `build/.../tests/` i test text falliscono con «no usable
font» perché alcune fixture usano path letterali relativi alla CWD
(`assets/fonts/Inter-*.ttf`), non perché manchi il font.

## 5. Demolition Debt

Nessuna dipendenza, bridge o shim introdotto.  La modifica **rimuove** una
duplicazione (la seconda conversione DirectYUV) invece di aggiungerne una: il
debito di demolizione è già estinto in questa consegna.

## 6. Residuo dichiarato (fuori dal gate P0-2)

1. **Dominio dei colori non-hex** (preset `Color{...}`, API diretta): non passano
   da `parse_hex_color`.  Dopo P0-2 le due lane li trattano allo stesso modo
   (lineare), quindi F2 è chiuso come *drift*, ma la domanda «un letterale
   `Color{0.86,0.72,0.43}` è sRGB o linear?» resta una decisione di contratto
   da prendere prima di P2-3 (policy Fast/High/Ultra), non da risolvere qui.
2. **F10 DirectYUV** (foreground lineare miscelato contro background encoded)
   resta reale e appartiene alla campagna DirectYUV separata.
3. **Certificazione pixel-level** del guadagno: richiede il campione pre-encode
   (P0-0b) su un artifact di un job reale, non è un gate di P0-2.
4. **Packer unificato**: chiuso in questo pass con
   `include/chronon3d/text/text_color_packing.hpp`; DirectYUV, fused FullGraph
   e GPU text-run delegano tutti alla stessa quantizzazione pura.
5. **`highlight_color`** (Vulkan) e i colori di material (bevel highlight): non
   passano dalla boundary hex, restano nel residuo «colori non-hex» (§6.1).

## 8. Censimento dei punti di ingresso colore nei backend lineari (review)

Prima di considerare il contratto colore "chiuso" va mappato **ogni** punto in
cui un `Color` authored entra in un backend che lavora in luce lineare.

| Sito | Sorgente del colore | Conversione | Verdetto |
|---|---|---|---|
| `parse_hex_color` (boundary) | `#RRGGBB` CSS | `to_linear()` | **P0-2**: unica boundary per i colori `style` |
| `pack_text_color` ×3 (FullGraph ×2, DirectYUV ×1) | `shape.paint.fill` / `stroke_color` / `shadow.color` | nessuna (quantizzazione) | PASS: corretto **dopo** la boundary |
| `to_bl_rgba` (software/Blend2D) | `shape.paint.fill`, `g.fill`, `g.stroke` | nessuna | PASS: attende lineare, e riceve lineare |
| `highlight_color` → `text_run.comp` `params.highlight_color` | `FillColorProperty::color` via `build_timed_highlight_plan` | **nessuna** | **APERTO (residuo non-hex)**: vedi sotto |
| lerp animated fill (`sample_gpu_run_level_animation`) | `run.color` (packed) ↔ `FillColorProperty::color` | nessuna (pack/unpack neutri) | PASS di dominio **solo se** i due operandi hanno lo stesso dominio |

### 8.1 `highlight_color` è un path vivo

`gpu_text_run.cpp:681` chiama `ctx.backend->draw_text_run_surface_timed(...)`
con `highlight_color` costruito da `build_timed_highlight_plan(shape)`, che legge
`FillColorProperty::color`.  Lo shader lo usa come
`params.highlight_color.rgb * src.a` dentro una tile lineare, senza conversione.
**Confermato: il path è vivo e appartiene al censimento**, come da review.

Non è però un difetto di P0-2: `FillColorProperty::color` **non** nasce da
`parse_hex_color`, quindi ricade nel residuo «dominio dei colori non-hex».
Nessun produttore censito oggi passa un accent: `build_semantic_emphasis_animators`
(`subtitle_track_builder.cpp:132`) chiama `make_lightweight_emphasis_animator`
con `std::nullopt`, quindi nessun `FillColorProperty` viene creato su quel path.
Il difetto è **latente**, non attivo sul corpus attuale.

### 8.2 Il lerp del fill animato non può double-convertire (ma il dominio non è tipato)

```cpp
Color base{ unpack(run.color, 0), ... };      // run.color = pack_text_color(paint.fill)
base.r += (fill->color.r - base.r) * w;      // lerp verso l'accent
run.color = pack_text_color(base);           // ripack
```

Con il packer a **quantizzazione pura** (stato P0-2) `unpack`/`pack` sono
neutri al dominio: **non esiste una seconda linearizzazione**, quindi la
preoccupazione «doppia conversione al repack» non si applica a questa forma.
Resta però il rischio di **cross-domain lerp** se `FillColorProperty::color` e
`shape.paint.fill` fossero authored in domini diversi: il tipo `Color` non lo
impedisce.  La correzione della review — API domain-safe
(`pack_authored_/pack_linear_/unpack_linear_`) o tipi `SrgbColor`/`LinearColor`,
con `unpack linear → fill.to_linear() → lerp → pack linear` — è la forma
corretta, ma tocca l'API pubblica: **richiede ADR**, non è raggiungibile in
questa sessione senza una decisione.

## 9. Verifica delle correzioni al passo shadow (PASSO 4)

Entrambe le correzioni della review sono **confermate dal codice**:

1. **Segno dell'unione.**  La coverage corrente è
   `fill_alpha = clamp((msd - 0.5) * range * scale + 0.5, 0, 1)`
   (`text_tile_raster.comp:267`), quindi `d = (msd - 0.5) * range * scale` è
   **positivo dentro**.  L'unione delle silhouette è quindi
   `run_d = max(run_d, d)` (il `min()` proposto in precedenza darebbe
   l'intersezione).  Review corretta.
2. **La shadow traslata non è una sottrazione scalare.**  Serve
   `d_shadow(p) = d_run(p - offset)`: va campionata la silhouette nella
   coordinata traslata.  Il codice attuale lo fa già **per glifo** spostando la
   UV (`shadow_uv = glyph_uv - delta`, `text_tile_raster.comp:305-310`), non
   sottraendo uno scalare alla distanza corrente.  Review corretta.

Conseguenza operativa già annotata: un'unione per-run richiede aggregazione
**per `run_index`** (più TextRun con shadow diverse possono convivere nello
stesso tile) e l'allargamento dell'envelope in `text_tile_bin.comp` per
stroke + shadow traslata + blur; altrimenti `RunMask + Gaussian` per `High`
resta architetturalmente più semplice e verificabile.  Il field-based resta
`Fast`.

## 7. Cronaca

P0-2 chiuso come *contratto*, non come *guadagno visivo misurato*: la boundary
ora emette lineare una volta sola e la duplicazione CUDA è rimossa.  Il prossimo
lavoro reale della scheda è P0-3 (contratto encoder), poi P1-2 (sampling
frazionario + coverage screen-space).
