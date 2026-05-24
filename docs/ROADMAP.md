# Chronon3D Roadmap Tecnica Unica

> Documento unico di riferimento per il refactor di Chronon3D.
> Obiettivo: portare il motore da pipeline CPU sequenziale a pipeline producer/consumer, piu `industry-grade`, con telemetria chiara e ottimizzazioni reali sui colli di bottiglia.

## Stato Attuale

Chronon3D e gia migliorato in modo significativo:

- background statici baked/cacheati
- skip del clear in casi sicuri
- cache del frame convertito nel path video
- telemetria piu ricca su clear saltato, cache e pipeline
- riduzione forte del costo di conversione frame

Restano pero i colli strutturali:

- pipeline video ancora troppo vicina al loop di render
- dirty rects non ancora veramente geometrici e inter-frame
- clear e copy non separati in modo netto nei report
- ottimizzazione del grafo ancora molto basica
- sfruttamento limitato di SIMD / zero-copy / hardware encode

---

## Obiettivo Finale

Arrivare a un motore in cui:

1. Il thread di render produce frame e non aspetta l'encoder.
2. La conversione pixel e la scrittura FFmpeg vivono fuori dal render thread.
3. Le aree sporche vengono calcolate davvero per frame e non solo contate.
4. Il grafo viene ottimizzato prima dell'esecuzione.
5. La memoria viene gestita con zero allocazioni inutili nel path caldo.
6. La telemetria distingue chiaramente costo di render, clear, copy, queue wait e pipe write.

---

## Priorita 0: Stabilizzare Telemetria e Diagnostica

### Obiettivo
Capire con precisione dove va il tempo, senza ambiguita tra clear, pool, conversione e I/O.

### Da fare

- separare in telemetria:
  - `clear` del grafo
  - `clear` del pool/framebuffer acquire
  - `clear` saltato
  - `copy/conversion`
  - `queue wait`
  - `pipe write blocked`
- tenere i contatori coerenti tra:
  - `RenderCounters`
  - CSV legacy
  - SQLite
  - JSONL
  - report markdown
- aggiungere un report per-run che mostri:
  - `clear_skipped_calls`
  - `clear_skipped_pixels`
  - `framebuffer_clear_ms`
  - `framebuffer_acquire_ms`
  - `frame_conversion_copy_ms`
  - `io_queue_push_blocked_ms`
  - `io_queue_pop_wait_ms`
  - `ffmpeg_pipe_write_blocked_ms`

### File da toccare

- [`include/chronon3d/core/profiling/counters.hpp`](/home/pierone/Pyt/Chronon3d/include/chronon3d/core/profiling/counters.hpp)
- [`include/chronon3d/core/telemetry/render_telemetry.hpp`](/home/pierone/Pyt/Chronon3d/include/chronon3d/core/telemetry/render_telemetry.hpp)
- [`src/core/render_telemetry_detail.cpp`](/home/pierone/Pyt/Chronon3d/src/core/render_telemetry_detail.cpp)
- [`src/runtime/telemetry/sqlite/sqlite_telemetry_store_write.cpp`](/home/pierone/Pyt/Chronon3d/src/runtime/telemetry/sqlite/sqlite_telemetry_store_write.cpp)
- [`src/runtime/telemetry/jsonl/jsonl_serializers.cpp`](/home/pierone/Pyt/Chronon3d/src/runtime/telemetry/jsonl/jsonl_serializers.cpp)
- [`apps/chronon3d_cli/commands/telemetry/command_telemetry_report.cpp`](/home/pierone/Pyt/Chronon3d/apps/chronon3d_cli/commands/telemetry/command_telemetry_report.cpp)

### Criterio di done

- Il report mostra chiaramente quale stadio consuma tempo.
- I numeri non si sovrappongono in modo ambiguo.
- Il CSV e il report markdown hanno lo stesso significato semantico.

---

## Priorita 1: Pipeline Producer/Consumer Vera

### Obiettivo
Disaccoppiare il render thread dal consumer FFmpeg.

### Da fare

- mantenere il render thread responsabile solo di:
  - evaluation
  - graph execute
  - enqueue del frame
- spostare nel consumer:
  - conversione pixel
  - copy verso buffer encoder
  - write su pipe
  - flush finale
- definire una coda bounded con metriche esplicite:
  - profondita massima
  - push blocked ms
  - pop wait ms
  - peak depth
- gestire in modo chiaro la backpressure:
  - se la coda e piena, il producer rallenta
  - se il consumer e lento, la telemetria deve mostrarlo

### File da toccare

- [`apps/chronon3d_cli/commands/video/video_export_pipe.cpp`](/home/pierone/Pyt/Chronon3d/apps/chronon3d_cli/commands/video/video_export_pipe.cpp)
- [`apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.cpp`](/home/pierone/Pyt/Chronon3d/apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.cpp)
- [`apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.hpp`](/home/pierone/Pyt/Chronon3d/apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.hpp)
- [`include/chronon3d/core/profiling/counters.hpp`](/home/pierone/Pyt/Chronon3d/include/chronon3d/core/profiling/counters.hpp)

### Criterio di done

- Il render thread non fa I/O blocccante.
- `render_only` si avvicina al solo costo del grafo.
- `ffmpeg_pipe_write_blocked_ms` e `io_queue_push_blocked_ms` raccontano davvero il collo di bottiglia.

---

## Priorita 2: Dirty Rects Geometrici Veri

### Obiettivo
Ridurre il lavoro quando la scena cambia solo in una porzione del frame.

### Da fare

- calcolare bounding box per nodo e per layer in modo affidabile
- tracciare la differenza tra frame N e frame N-1
- invalidare solo le regioni necessarie
- evitare clear/copy full-frame quando possibile
- usare dirty rects anche per il path video, ma solo quando sono sicuri

### Dove intervenire

- [`src/render_graph/render_pipeline_scene.cpp`](/home/pierone/Pyt/Chronon3d/src/render_graph/render_pipeline_scene.cpp)
- [`src/render_graph/render_pipeline_composition.cpp`](/home/pierone/Pyt/Chronon3d/src/render_graph/render_pipeline_composition.cpp)
- [`src/render_graph/builder/graph_builder_layer_pipeline.cpp`](/home/pierone/Pyt/Chronon3d/src/render_graph/builder/graph_builder_layer_pipeline.cpp)
- [`include/chronon3d/render_graph/nodes/clear_node.hpp`](/home/pierone/Pyt/Chronon3d/include/chronon3d/render_graph/nodes/clear_node.hpp)

### Criterio di done

- i frame statici o quasi statici non ridisegnano tutto lo schermo
- `dirty_pixels` e `dirty_rect_count` diventano metriche operative, non solo diagnostiche

---

## Priorita 3: Ottimizzazione del Grafo

### Obiettivo
Compilare e semplificare il grafo prima del frame loop.

### Da fare

- fusion di nodi semplici dove possibile
- pruning di rami inutili
- early exit per layer completamente coperti
- look-ahead sui frame statici o prevedibili
- opportunita di bake/cache per nodi con output invariato

### Dove intervenire

- [`src/render_graph/builder/graph_builder_pipeline.cpp`](/home/pierone/Pyt/Chronon3d/src/render_graph/builder/graph_builder_pipeline.cpp)
- [`src/render_graph/builder/graph_builder_layer_pipeline.cpp`](/home/pierone/Pyt/Chronon3d/src/render_graph/builder/graph_builder_layer_pipeline.cpp)
- [`src/render_graph/graph_builder.cpp`](/home/pierone/Pyt/Chronon3d/src/render_graph/graph_builder.cpp)
- [`src/render_graph/render_pipeline_scene.cpp`](/home/pierone/Pyt/Chronon3d/src/render_graph/render_pipeline_scene.cpp)

### Criterio di done

- il grafo esegue meno nodi per frame
- i nodi statici vengono riusati o saltati
- si riducono buffer intermedi e pass inutili

---

## Priorita 4: Memoria e Cache

### Obiettivo
Ridurre allocazioni, churn e traffico memoria.

### Da fare

- tenere il `FramebufferPool` piu prevedibile
- distinguere bene:
  - allocazione
  - reuse
  - clear
  - return to pool
- usare meglio:
  - arena per temporanei
  - huge pages dove ha senso
  - allineamento cache-line
- introdurre cache persistenti per asset statici e nodi baked
- valutare meglio il costo del bake PNG/EXR rispetto a un formato intermedio piu diretto

### File da toccare

- [`src/cache/framebuffer_pool.cpp`](/home/pierone/Pyt/Chronon3d/src/cache/framebuffer_pool.cpp)
- [`include/chronon3d/core/framebuffer_arena.hpp`](/home/pierone/Pyt/Chronon3d/include/chronon3d/core/framebuffer_arena.hpp)
- [`include/chronon3d/core/memory/framebuffer.hpp`](/home/pierone/Pyt/Chronon3d/include/chronon3d/core/memory/framebuffer.hpp)
- [`src/cache/disk_node_cache.cpp`](/home/pierone/Pyt/Chronon3d/src/cache/disk_node_cache.cpp)

### Criterio di done

- meno allocazioni per frame
- meno overhead di clear inutile
- meno pressione sul memory subsystem

---

## Priorita 5: SIMD, Copy e Conversione

### Obiettivo
Ridurre il costo della conversione pixel e della copia verso encoder.

### Da fare

- spingere la conversione con SIMD piu aggressivo
- evitare copie inutili quando il frame e gia nel formato giusto
- specializzare i path piu comuni:
  - RGBA -> YUV420p
  - RGBA -> NV12
  - RGBA -> RGB24
- valutare registrazione buffer / zero-copy dove possibile
- tenere separato il costo di conversione da quello di scrittura pipe

### File da toccare

- [`apps/chronon3d_cli/utils/video/ffmpeg_pipe_yuv.cpp`](/home/pierone/Pyt/Chronon3d/apps/chronon3d_cli/utils/video/ffmpeg_pipe_yuv.cpp)
- [`apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.cpp`](/home/pierone/Pyt/Chronon3d/apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.cpp)
- [`src/backends/software/simd/highway_kernels.cpp`](/home/pierone/Pyt/Chronon3d/src/backends/software/simd/highway_kernels.cpp)

### Criterio di done

- `frame_conversion_copy_ms` cala ancora in modo sensibile
- la conversione non domina piu il pipeline time

---

## Priorita 6: Hardware e Backend Futuri

### Obiettivo
Portare Chronon3D oltre la CPU generica, quando il resto della pipeline e pulito.

### Opzioni

- AVX2 / AVX-512 per i path CPU piu caldi
- supporto GPU per conversione e/o effetti
- encoder hardware:
  - NVENC
  - VAAPI
  - AMF
- eventuale backend compute per effet stack e compositing

### Criterio di done

- il motore scala oltre la CPU genericamente disponibile
- il backend viene scelto in base a macchina e workload

---

## Priorita 7: Test, Benchmark e Regresioni

### Obiettivo
Non perdere i miglioramenti mentre si refattorizza.

### Da fare

- golden render test per i preset chiave
- video diff sui file esportati
- benchmark micro per:
  - clear
  - conversion
  - pipe write
  - queue wait
  - frame cache
- test sui casi:
  - frame statici
  - frame quasi statici
  - dirty rect piccoli
  - motion blur
  - export lungo

### File / aree

- [`tests`](/home/pierone/Pyt/Chronon3d/tests)
- [`apps/chronon3d_cli/commands/bench`](/home/pierone/Pyt/Chronon3d/apps/chronon3d_cli/commands/bench)
- [`apps/chronon3d_cli/commands/telemetry`](/home/pierone/Pyt/Chronon3d/apps/chronon3d_cli/commands/telemetry)

### Criterio di done

- ogni refactor importante ha una verifica automatica
- i report mostrano miglioramenti reali, non solo sensazioni

---

## Ordine Consigliato Di Esecuzione

1. Stabilizzare telemetria e report.
2. Finire la pipeline producer/consumer vera.
3. Rifinire il dirty tracking geometrico.
4. Ottimizzare il grafo.
5. Ridurre ancora il costo di memoria e conversione.
6. Solo dopo introdurre ottimizzazioni hardware piu spinte.

---

## Regola Di Priorita

Se una modifica:

- migliora solo un caso speciale ma complica troppo il runtime
- rende la telemetria meno chiara
- rischia di reintrodurre glitch sui video

allora non e prioritaria rispetto a:

- disaccoppiamento della pipeline
- affidabilita dei buffer
- chiarezza dei contatori

---

## Note Finali

Questo documento deve restare vivo.

Quando un task viene completato:

- aggiorna lo stato
- sposta il lavoro finito nelle note
- lascia i file da toccare solo per cio che e ancora aperto

L'obiettivo non e fare piu micro-ottimizzazioni sparse.
L'obiettivo e trasformare Chronon3D in una pipeline leggibile, misurabile e scalabile.
