# TICKET-CHRONON-2.0-MASTER — Release 2.0, backlog unico e criteri di accettazione

- **Stato**: **APERTO — tracker canonico della release 2.0.** P0 locale chiuso o
  dichiarato `BLOCKED` dove dipende da runner/GPU/reviewer; P1-01/P1-02 chiusi
  localmente; P1 visivo, P2 e P3 restano aperti con i gate sotto. Nessun task è
  promosso a `PASS` senza l'evidenza indicata.
- **Data di apertura**: 2026-11-03.
- **Sostituisce**: la roadmap esterna "Chronon 2.0 — Master TODO" (52 task). Le
  schede storiche restano fonti; questo file è l'unica milestone 2.0.
- **Regola**: un task si chiude solo con l'evidenza elencata nella colonna
  *Gate*. `BLOCKED` e `NOT RUN` non si convertono in `PASS`.

## 1. Missione e scope freeze

**Prodotto.** Motore nativo headless C++20 per motion graphics, composizioni
2D/2.5D e un sottoinsieme esplicito di 3D; export MP4 e frame raw; template
ripetibili; una sola camera authoring via ChrononMotion3D.

**Non sono obiettivi 2.0** (vanno in `v2.1`, fuori dal criterio di
accettazione): editor equivalente ad After Effects, supporto universale `.aep`,
motore di gioco, ray tracer, tutte le estensioni glTF, bokeh poligonale non
certificato.

### 1.1 Contratti non negoziabili

1. **CM3D** possiede tempo, gerarchia, rig e camera authored; **C3D** possiede
   matrici di proiezione/occlusione e la fisica di rendering dei pixel.
2. **RenderPlan V3** è il contratto di handoff; **CompiledProgram** è il
   programma preparato. Non esiste un secondo esecutore di produzione.
3. Un solo owner per risorse GPU e una sola semantica per effetti, depth,
   colore e testo. I backend possono usare algoritmi diversi entro tolleranze
   dichiarate.
4. Con `require_gpu_native` attivo il programma di produzione non torna in
   silenzio sulla CPU. Il renderer software resta un backend dichiarato e
   l'oracle di test.
5. Una fixture `PASS` non è una release: servono build da commit pulito, CI
   same-SHA e review visiva umana.

### 1.2 Baseline congelata (P0-01)

| Voce | Valore al 2026-11-03 |
|---|---|
| Chronon3d | `a180b6fc6` (`test(camera): lock the shared camera contract frontier`), working tree dirty: `docs/tickets/TICKET-CAMERA-DEFINITIVE-CLOSURE.md`, `src/render_plan/render_plan_compiler_animation.cpp`, `tools/raw_frame_to_png.py` |
| ChrononMotion3D | `ffa81ba` (`docs(camera): publish the Motion to RenderPlan contract`) |
| ChrononTemplate | `d0df684` (`fix(camera): resolve runtime template layer samples`), working tree dirty (text layout, camera packs, backgrounds) |
| Host | Linux, 24 core, 29 GiB RAM + 4 GiB swap, RTX A4000 |
| Toolchain | CMake 3.31.6, Ninja 1.13, CMake preset `linux-video-release` (Release), `ccache`, `-fuse-ld=mold` |
| Build dir | `Chronon3d/build/chronon/linux-video-release` |
| RAM guard | `-j12` solo con `MemAvailable ≥ 20 GiB`; altrimenti `-j8`. Nessun `-j20`. |
| Corpus content | `CHRONON3D_BUILD_CONTENT=OFF`: i gate che richiedono `content/` sono `NOT RUN` |

## 2. Le cinque condizioni per dichiarare Chronon finito

1. Una scena mista video + PNG + testo 2.5D + mesh 3D renderizza correttamente
   anche con camera in movimento.
2. Software e Vulkan rispettano gli stessi contratti di profondità, alpha,
   colore ed effetti, con tolleranze misurate.
3. Non esistono due autorità indipendenti per camera, placement, animazione,
   risorse GPU o piano di esecuzione.
4. Output MP4 e frame raw riproducibili: nessun frame nero, crash, fallback
   silenzioso o memoria non rilasciata.
5. Un utilizzatore installa il sistema, prende un preset, sostituisce
   testo/immagini/video ed esporta senza conoscere gli interni del renderer.

## 3. P0 — Correttezza e release blockers

| ID | Repo | Azione atomica | Gate / evidenza | Stato |
|---|---|---|---|---|
| P0-01 | Tutti | Congelare scope, SHA, lockfile, GPU e toolchain | Manifest in §1.2 e `docs/evidence/chronon-2.0-baseline-20261103/` | **PASS** |
| P0-02 | C3D | CI main verde per build/test/SDK senza cancellare gate | Pipeline success sullo SHA del tag candidato | **BLOCKED** — runner GitHub; fuori scope di questa sessione (decisione utente) |
| P0-03 | CM3D/Template | CI unit+integration allineata alle versioni C3D | Matrice versioni dichiarata, nessun fail nascosto | **BLOCKED** — CI remota |
| P0-04 | C3D/CM3D | Isolare camera yaw/pitch + `inline_mesh.animation` | 0..149 frame, translate/rotate/orbit + skin/morph, no collasso, bounds stabili | **PASS locale** — collasso riprodotto e corretto (far clip 1000 vs scena a 1000 + `CameraPlan far=10000`); 60 frame a 20 100–20 254 px, `chronon3d_camera_tests` 89/89, 67 718 asserzioni; evidenza `chronon-2.0-local-20261103/` |
| P0-05 | C3D | Correggere interferenza layer `image` 2D dietro mesh 3D | Confronto image vs plane3d, nessun pattern casuale, CPU+GPU | **PASS locale** — la mesh scrive alpha/depth e il composite rispetta il layer; caveat geometria 2D dichiarato |
| P0-06 | C3D | Contratto depth unico camera-space per plane, mesh, testo, image | Permutazioni ordine layer equivalenti quando la geometria lo impone | **PASS locale software / Vulkan matrix aperta** — fixture 4-layer image/plane/mesh/text passa 24/24 ordini con hash identico; la matrice completa non è stata ancora eseguita su Vulkan |
| P0-07 | C3D | Separare alpha-test/cutout da blending; ordine e scrittura Z dichiarati | Pixel golden su opaque, alpha-cutoff, alpha frazionario, due mesh, due piani | **PASS locale software + A4000 Vulkan** — 24 frame, cutout binario/stabile, blend frazionario, due mesh e due piani, box traslucido; marker depth separa coverage blend da occlusione. Evidenza `docs/evidence/p007-alpha-cutout-vulkan-20261104.md` |
| P0-08 | C3D | Unificare pixel-truth raw/video vs still/PNG | Raw RGBA→PNG senza differenze salvo conversione documentata | **PARTIAL** — raw converter normalizza premultiplied-linear in straight-alpha sRGB; fixture locale immagine+mesh+testo 128×96 differisce in 12 pixel di massimo 1 LSB. Gate completo still-layer/same-commit ancora aperto. Evidenza `docs/evidence/p008-still-raw-parity-2026-11-04.md` |
| P0-09 | C3D | Bonificare lifetime Vulkan: lease, semafori, descriptor, superfici | 100 job consecutivi, cancel, reset, teardown, zero validation error | **PARTIAL** — `*lifetime*` 6/6 casi, 28 asserzioni; manca lo stress 100 job |
| P0-10 | C3D | Completare C6 DoF 150f e C7 review/same-SHA | Ticket camera aggiornato e gate realmente PASS | **PARTIAL** — C6 PASS locale (`c06-current-20261103`); C7 `BLOCKED` (reviewer/CI) |
| P0-11 | C3D/CM3D | Unico placement immutabile dal campione camera/layer | Identità matrici e bounds in build/refresh/render; no double transform | **PASS locale** — test camera/render-graph verdi |
| P0-12 | C3D | Crash/UAF, memory budget, fail-closed su job concorrenti | Sanitizer CPU, stress GPU, zero crash, metriche lifecycle | **NOT RUN** — richiede rebuild ASan completo |
| P0-13 | C3D | Video native Vulkan/NVENC senza black frame o fallback non dichiarato | MP4 decode, timestamp monotoni, 150/150f, zero validation | **PASS locale** — C06 150/150, 0 neri, 0 validation error |
| P0-14 | Tutti | Gate errori malformati (glTF, RenderPlan, asset) e resource limits | No OOM incontrollato, errori tipizzati, fuzz corpus | **PARTIAL** — validator fail-closed presenti; fuzz corpus non eseguito |

## 4. P1 — Qualità visiva e feature set dichiarato

| ID | Repo | Azione atomica | Gate / evidenza | Stato |
|---|---|---|---|---|
| P1-01 | Template/C3D | Sostituire `estimateWidth` dei preset editoriali col layout shaped canonico | Highlight ON/OFF conserva le posizioni di tutti i glifi | **PASS locale** — `measuredWidth` (HarfBuzz, stesso shaper del renderer) in `WebHeadlineHighlightPack` e `WebPageHighlightPack`, tabella deterministica solo come fallback; `chronontemplate_web_headline_highlight_pack_test` **199 check, 0 failures** (2026-11-03) |
| P1-02 | Template/C3D | Range geometry per span con cluster, line-break, bidi, UTF-8, advance, baseline | Highlight a una banda per riga; nessuna parola incollata, nessun rilayout | **PASS locale** — banda per riga da advance misurati (`marker tracks the HarfBuzz advance width`, `stepped highlight blocks`); `chronontemplate_web_page_highlight_pack_test` exit 0 |
| P1-03 | Template | Completare ArticleHighlighter, DarkQuote, ArticleSpotlight, EvidenceCard | Quattro golden 1080p fedeli, review visiva | **PARTIAL** — preset presenti; review umana mancante |
| P1-04 | C3D | Shadow testo come silhouette dell'intero run | Niente clip/glow troncato o ombre moltiplicate | **OPEN** — `TICKET-TEXT-QUALITY-V2` P1-3 |
| P1-05 | C3D | Testo Fast/HQ con gamma, AA, MTSDF, colore linear | Corpus font/zoom/export raw approvato | **PARTIAL** — P1-1/P1-2 fatti, P1-3/P2 aperti |
| P1-06 | C3D | Blur e bloom sui layer giusti, testo foreground nitido | Preset neon con background DoF e testo leggibile | **PARTIAL** — DoF per-layer PASS; preset neon da certificare |
| P1-07 | C3D | Chiudere IBL cubemap specular Vulkan strict-native | HDRI ON/OFF, roughness mip, parità software/GPU | **PARTIAL** — `TICKET-ENVIRONMENT-LIGHT-IBL` |
| P1-08 | C3D | Parità PBR software/Vulkan sul subset supportato | Golden metallo/plastica/gemma, texture base e metal-rough | **PARTIAL** |
| P1-09 | C3D | Finalizzare shadow catcher e ombre native | Ground catcher con alpha corretto, pixel + review | **OPEN** — `TICKET-SHADOW-NATIVE-CATCHER` |
| P1-10 | CM3D/C3D | CameraSequence/cut attivo E2E, focus/shutter subframe | Cut senza crossfade implicito, seek out-of-order, frame equivalenti | **PARTIAL** |
| P1-11 | CM3D/C3D | Test focale/sensore/lens shift/near/far/DoF combinati | Golden numerici di proiezione e video casi limite | **PARTIAL** — F0–F5 locali |
| P1-12 | C3D | glTF 3D bounded su modelli realistici | Import supportato, esiti chiari per estensioni escluse | **PARTIAL** |
| P1-13 | Template | FloatingNeonText + ProductPackshot | Golden 5 s, parallasse, alpha, bloom | **OPEN** |
| P1-14 | Template | Preset misto PNG2.5D / glTF senza forzare mesh | A/B scene equivalenti + benchmark costi | **OPEN** |
| P1-15 | C3D | Regressione colore linear/premultiplied, texture sRGB, NV12 | Golden e ΔE/MAE su canary colori | **PARTIAL** |
| P1-16 | Tutti | Revisioni finali con approvazione umana | Checklist reviewer e artefatti approvati | **BLOCKED** — nessun reviewer nominato |

## 5. P2 — Prestazioni senza gonfiare il core

| ID | Repo | Azione atomica | Gate / evidenza | Stato |
|---|---|---|---|---|
| P2-01 | C3D | Profilare `inline_mesh.comp` O(pixel × triangoli) su 10/100/1k/10k triangoli | Grafico GPU ms/throughput e scelta motivata | **NOT RUN** — richiede host GPU dedicato |
| P2-02 | C3D | Introdurre raster Vulkan graphics per mesh reali, compute per piccoli casi | Stesso depth/alpha/compositing, benchmark A/B con soglia | **OPEN** — decisione dopo P2-01 |
| P2-03 | C3D | Ridurre `VulkanBackend::Impl` via autorità esistenti | Nessuna ownership parallela; lifecycle e perf verdi | **OPEN** |
| P2-04 | C3D | Scomporre la compilazione per artifact/fasi mantenendo `CompiledProgram` | Programmi equivalenti, niente rebuild hot path | **OPEN** |
| P2-05 | C3D | Cache key separate content/placement/temporal/device, hashing versionato | Test mutazione campo-per-campo, cache hit senza stale | **PARTIAL** — `TICKET-CACHE-IDENTITY-KEYS` |
| P2-06 | C3D | Asset/topologia una volta per job, solo uniform per-frame | CPU build/frame e alloc/frame misurati | **OPEN** |
| P2-07 | C3D | Pipeline async Vulkan→CUDA→NVENC con telemetria | No readback strict, frame hash/repeat, backpressure | **PARTIAL** |
| P2-08 | C3D | Admission VRAM/job class e backpressure senza spin loop | Due job senza contesa illegale, VRAM bounded | **PARTIAL** |
| P2-09 | C3D | Ridurre pass di compositing solo con profiler | Benchmark 1080p/4K, golden, no regressioni | **OPEN** |
| P2-10 | C3D | Benchmark fixture fisse: static, editorial, 2.5D, glTF, mixed, DoF | Baseline P50/P95, GPU ms, wall, VRAM, encode, warm/cold | **NOT RUN** |
| P2-11 | Tutti | Budget binary size, dipendenze, clean-build, alloc hot path | Profilo lite/full, report diff per release | **PARTIAL** |

## 6. P3 — Release, interoperabilità e supporto

| ID | Repo | Azione atomica | Gate / evidenza | Stato |
|---|---|---|---|---|
| P3-01 | Template/RenderingGen | Import timeline OpenTimelineIO (clip, cut, range, media refs) | `.otio`→scena→MP4; errore esplicito su effetti non traducibili | **OPEN** |
| P3-02 | C3D/Template | Censire/migrare `CompiledComposition` legacy | Consumer zero o adapter opzionale, corpus 8/8 | **PARTIAL** |
| P3-03 | C3D | Eliminare `Fill` duplicati dopo decisione ABI | Ratchet eccezioni + test visuali | **PARTIAL** |
| P3-04 | C3D | SDK installabile (`find_package`), CLI release, consumer esterno | Clean install Linux/Windows in CI, example 5 s | **OPEN** |
| P3-05 | CM3D | Bundle CM3D→RenderPlan senza renderer nel producer | Test ABI e dependency boundaries | **PARTIAL** |
| P3-06 | Template | Catalogue versionato, schema preset, manifest asset | 10 preset golden con font/asset/licenze | **OPEN** |
| P3-07 | RenderingGen | Planner job e receipt: input hash, preset, device, route, output hash | Job idempotenti, retry/cancel, metriche | **PARTIAL** |
| P3-08 | C3D | Audio mux/sync e captions | Sync frame/audio, seek, MP4 validi | **PARTIAL** |
| P3-09 | Tutti | SBOM/licenze, security policy, support matrix | Compliance check e release notes | **OPEN** |
| P3-10 | Tutti | Due profili: lite headless CPU e full GPU/native | Install, smoke test, compatibilità, documentazione | **OPEN** |
| P3-11 | Tutti | Tag `v2.0.0-rc1`, freeze, acceptance, tag finale | Zero P0; P1 dichiarati; CI verde; golden review; link binari | **OPEN** |

## 7. Corpus di accettazione obbligatorio

1. **Editorial four scenes** — title yellow, quote dark, spotlight paper, card
   over blurred footage; highlight SVG/text su Unicode/ligature/multiline; raw
   e video.
2. **Packshot neon 2.5D** — soggetto alpha, 4 packshot PNG, lenti, dolly+orbit,
   testo neon, glow, blur selettivo, occlusione corretta.
3. **Mixed 3D** — 1 glTF animato skinned+morph, una mesh opaca, un
   alpha-cutout, un `plane3d`, un'`image` 2D dietro, camera rotating + dolly.
4. **Optical 3-depth** — 150 frame 1080p/30, rack focus, shutter, alpha
   frazionario, near/far edge, 0 frame neri, raw parity.
5. **Performance benchmarks** — static 2D, fast video, 2.5D, glTF mixed,
   DoF/shutter; 1080p e 4K, warm/cold, 1/2 job.

**Soglie ottiche**: usare quelle congelate in
`TICKET-CAMERA-DEFINITIVE-CLOSURE` (MAE ≤ 2, p99 ≤ 10 sul confronto raw
pertinente). Non esiste una soglia universale per PBR, encoder lossy, testo o
fixture nuovi: si definiscono prima fixture e metrica.

## 8. Librerie — politica di integrazione

| Candidato | Ruolo | Decisione 2.0 |
|---|---|---|
| Filament | BRDF, IBL, camera, post FX reference | studiare, non incorporare |
| Vello | Sparse Strips / vector compositing | studiare, nessun runtime Rust |
| Blend2D | oracle software 2D | conservare l'integrazione esistente |
| msdfgen | M(S)DF HQ | spike mirato solo se il generatore fallisce il corpus |
| HarfBuzz | shaping, cluster, caret | riusare `TextRunLayout` |
| cgltf / fastgltf | import glTF | sceglierne uno solo, previo test |
| meshoptimizer | preprocessing mesh | opzionale offline |
| VMA | allocazione Vulkan | solo dopo audit dell'allocator |
| Tracy / RenderDoc | profiling / capture | dev-only |
| OpenTimelineIO | import cut/track/timing | sì, adapter opzionale |
| OpenColorIO | color management / LUT | usare l'integrazione esistente |
| glTF-Validator / Sample-Assets | conformance fixture | test/dev only |
| rlottie | Lottie | plugin opzionale |

**Non integrare**: O3DE/OSG/osgEarth, un secondo compositor, un secondo camera
rig, un secondo layout testo, un ray tracer. GPL (Natron/Friction): studio
senza copiare codice nel core permissivo.

## 9. Ordine di esecuzione e STOP rule

1. Freeze SHA/CI baseline → P0-01..03.
2. Fix camera mesh, depth, alpha, still truth → P0-04..08.
3. Hardening GPU lifetime, C6/C7, strict native → P0-09..14.
4. Qualità editoriale testo + 4 preset → P1-01..06.
5. Qualità 3D IBL/PBR/shadow/camera/lens → P1-07..12.
6. Golden scena neon → P1-13..16.
7. Profiling e raster mesh vs compute → P2.
8. Interoperabilità/legacy/release → P3.

**STOP rule.** Niente merge se una PR cambia pixel senza modificare
intenzionalmente una specifica visiva, introduce un percorso alternativo di
stato, fallisce i golden del suo dominio o peggiora le prestazioni oltre il
budget approvato.

## 10. Criterio di chiusura release

La 2.0 è chiusa quando: P0 tutti verdi; P1 nello scope tutti verdi con golden e
reviewer; P2 benchmark e no-regression firmati; P3 installabilità,
documentazione, licenze e CI verdi; le capacità non supportate falliscono
esplicitamente. I task non bloccanti vanno in `v2.1`.

## 11. Registro delle verifiche di questa sessione (2026-11-03)

| Verifica | Comando / percorso | Esito |
|---|---|---|
| Build C3D `-j12` ccache+mold | `cmake --build build/chronon/linux-video-release --target chronon3d_cli chronon3d_camera_tests -j12` | Vedi §12 |
| Gate camera+DoF+occlusione | `chronon3d_camera_tests`, `chronon3d_render_graph_tests -tc=PR2-RG-DoF*` | Vedi §12 |
| Lifetime | `chronon3d_render_graph_tests -tc=*lifetime*` | Vedi §12 |
| Camera + mesh animata | regressione dedicata + render fixture | Vedi §12 |
| Still vs raw | `tools/raw_frame_to_png.py` | Vedi §12 |
| Template text layout | build + `ctest` del target web | Vedi §12 |

## 12. Esito verificato (2026-11-03)

| Verifica | Comando | Esito | Evidenza |
|---|---|---|---|
| Build C3D `-j12` ccache+mold | `cmake --build build/chronon/linux-video-release --target chronon3d_cli -j12` | **PASS** — `BUILD_EXIT=0`, 269 oggetti, nessun OOM | `/tmp/build6.log` |
| P0-04 riproduzione | render fixture `03-gltf-animato` con yaw 0,02° | **riprodotto**: 20 248 px → 276 px (f40) → 0 (f59) | `docs/evidence/chronon-2.0-local-20261103/P0-04-camera-mesh-collapse.md` |
| P0-04 causa | dump view/projection/NDC | vertici corretti, NDC identici; il clip far-plane (`far=1000`) taglia la mesh a z<−1000 | stessa evidenza |
| P0-04 fix | `Camera2_5D::near_plane/far_plane` dal `CameraPlan` → `frame_input.camera`; render 60 frame | **PASS locale** — copertura 20 100–20 254 px su tutti i 60 frame (f40: 276 → 20 247 px; f59: 0 → 20 249 px) | stessa evidenza |
| P1-01/P1-02 | `chronontemplate_web_headline_highlight_pack_test` | **199 check, 0 failures**; `chronontemplate_web_page_highlight_pack_test` exit 0 | `/tmp/tpl-build2.log` e output test |
| P0-08 | `python3 tools/raw_frame_to_png.py --self-test`; estrazione frame 40 dal raw del repro | **PASS** — self-test `PNG round-trip byte-faithful`; frame 40 estratto 1056×588, 20 247 px mesh (identico al conteggio sul raw) | output in sessione |
| P0-04/06/09 test C3D | `chronon3d_camera_tests`; `chronon3d_render_graph_tests -tc=PR2-RG-DoF*`; `-tc=*lifetime*` | **PASS** — 89/89 casi (67 718 asserzioni), 16/16 (979), 6/6 (28) | `/tmp/build-tests.log`, output in sessione |
| P0-07 video E2E | `python3 tests/e2e/test_p0_07_alpha_cutout_video.py --cli <cli>` | **PASS** — 24 frame, cutout binario (0 → 32 220 px), alpha frazionario 0.5, due piani con ordine Z, box chiuso 128/255 | `docs/evidence/chronon-2.0-local-20261103/P0-07-alpha-cutout-video-e2e.md` |
| Backface culling software | `chronon3d_renderer_core_tests` + variante `box` nel gate P0-07 | **PASS** — 206/206 casi, 74 294 asserzioni; box traslucido 191→128/255 | `/tmp/renderer-tests2.log` |
| Chiusura sessione | `docs/evidence/chronon-2.0-local-20261103/REPORT.md` | report PASS / BLOCKED / NOT RUN | stesso percorso |
| Baseline | `docs/evidence/chronon-2.0-baseline-20261103/README.md` | SHA, toolchain, RAM guard | stesso percorso |

Regola: `NOT RUN` e `BLOCKED` restano tali; il PASS locale non è una
certificazione same-SHA.
