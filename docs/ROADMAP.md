# Chronon3D Roadmap Tecnica Unica

> Documento unico di riferimento per il refactor di Chronon3D.
> Obiettivo: portare il motore verso una pipeline producer/consumer industry-grade, con telemetria chiara e ottimizzazioni reali sui colli di bottiglia.

## Stato Attuale (verificato su codice reale al commit 926d2b6)

### Completato

- **Priorità 0-2**: telemetria stabilizzata, pipeline producer/consumer disaccoppiata, dirty rects geometrici introdotti ✓
- **Priorità 3 (Grafo)**: optimizer completo con dead-node elimination, fusion EffectStack/Adjustment, fusion Transform, branch pruning, static bake counting. Testato. ✓
- **Priorità 4 (Memoria)**: FramebufferPool con bucket, warm_up, preallocate, arena; PersistentBakeCache formato CFB2 binario con mmap, I/O riga-per-riga; FramebufferArena con huge pages già implementata e usata in video_export_pipe; TripleBufferArena per concorrenza render/encode ✓
- **Priorità 5 (SIMD)**: frame_converter API unificata, ConvertedFrameCache LRU, telemetria frame_conversion_copy_ms + converted_frame_cache_hits, SIMD luma (Y) vettorizzato con Highway ✓
- **Priorità 5 - Chroma UV SIMD**: `convert_uv_pair_rows_vectorized_impl` re-abilitato con implementazione ibrida (scalar 2×2 averaging + SIMD gamma LUT GatherIndex + SIMD UV matrix) — stabile con GCC 14.2, evita TableLookupLanes/SetTableIndices. Test SIMD e video passano. ✓
- **Priorità 7 (Test)**: 11 suite di test (core, scene, renderer, CLI, optimizer, cache, video, IO, animation, golden) ✓
- **Priorità 7 - Test coverage**: video diff (identità tra conversioni successive, Y420P vs NV12 Y match, gamma on/off differ), micro benchmark (clear_ms, conversion_ms YUV420P/NV12, frame_cache hit_rate), long export (1000 frame cache + pool reuse con 99.9%), near-static frames test. 27 test video totali, tutti passanti. ✓

## Obiettivo Finale

Arrivare a un motore in cui:

1. il thread di render produce frame e non aspetta l'encoder  ✅
2. la conversione pixel e la scrittura FFmpeg vivono fuori dal render thread  ✅
3. le aree sporche vengono calcolate davvero per frame e non solo contate  ✅ (compute_dirty_rect + clipping nei nodi)
4. il grafo viene ottimizzato prima dell'esecuzione  ✅ (optimize_graph con 5 pass)
5. la memoria viene gestita con zero allocazioni inutili nel path caldo  ✅ (arena + pool attivi, validato su 1000 frame con hit rate 99.9%, memoria stabile)
6. la telemetria distingue chiaramente costo di render, clear, copy, queue wait e pipe write  ✅

---

## Cosa Manca Davvero (task aperti)

### 1. Pressione Memoria Export Lunghi (Priorità 4) — ✅ Validato

**Validazione completata:**
- Pool non cresce indefinitamente: test con 500 cicli × 5 dimensioni diverse mostra riutilizzo stabile (stesso numero di allocazioni tra prima e seconda tornata, bytes invariati)
- Hit rate 99.9% dopo warmup di 1 ciclo (misurato su 1000 acquire/release 1920×1080)
- Arena / huge pages non servono al momento: il pool si stabilizza dopo l'allocazione iniziale e riusa lo stesso buffer senza crescita

**Rimane aperto solo se emergano evidenze di degrado su scene reali (export > 10000 frame con cache rate basso).**

---

### 2. Hardware e Backend Futuri (Priorità 6)

**Stato:** CPU SIMD già coperto da Highway (AVX2, AVX-512, SSE4, NEON via dynamic dispatch). Chroma UV ora vectorizzato. Pipeline disaccoppiata pronta per encoder asincroni.

**Prossimi passi concreti:**
1. **Encoder hardware via FFmpeg** — `ffmpeg_pipe_encoder.cpp` può passare `-hwaccel` e codec HW (`h264_nvenc`, `hevc_vaapi`, `h264_amf`) tramite pipe. La pipeline producer/consumer già separa conversione da scrittura, quindi l'unica modifica è nel comando ffmpeg. Basso sforzo, alto impatto.
2. **GPU compute per conversione pixel** — Vulkan/Metal compute shader per RGBA→YUV. Sostituire `frame_converter.cpp` dispatcher con backend GPU. Impatto alto, necessario solo per >4K.
3. **GPU compute per effect stack** — Sostituire `EffectStack::apply` con shader. Richiede architettura render graph già pronta. Futuro medio-lungo termine.

---

## Regola Di Priorita

Se una modifica:

- migliora solo un caso speciale ma complica troppo il runtime
- rende la telemetria meno chiara
- rischia di reintrodurre glitch sui video

allora non e` prioritaria rispetto a:

- disaccoppiamento della pipeline
- affidabilita` dei buffer
- chiarezza dei contatori

---

## Note Finali

Questo documento deve restare vivo.

Quando un task viene completato:

- rimuovilo dalla lista operativa
- lascia il dettaglio solo nei changelog o nei commit
- mantieni qui solo cio` che manca davvero

L'obiettivo non e` fare piu` micro-ottimizzazioni sparse.
L'obiettivo e` trasformare Chronon3D in una pipeline leggibile, misurabile e scalabile.
