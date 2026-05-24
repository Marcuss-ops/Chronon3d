# Chronon3D Roadmap Tecnica Unica

> Documento unico di riferimento per il refactor di Chronon3D.
> Obiettivo: portare il motore verso una pipeline producer/consumer piu` industry-grade, con telemetria chiara e ottimizzazioni reali sui colli di bottiglia.

## Stato Attuale

La base del motore e` ormai buona, ma il lavoro ancora aperto e` quello che separa una pipeline "funziona bene" da una pipeline davvero scalabile:

- telemetria e report sono molto piu` leggibili e coerenti
- pipeline video producer/consumer e` gia` disaccoppiata dal render thread
- dirty rects geometrici sono stati introdotti, ma non sono ancora sfruttati ovunque
- il grafo ha gia` un optimizer integrato, con dead-node elimination e fusion di effect stack / adjustment stack
- la cache dei bake persistenti e` presente e ha gia` ricevuto un upgrade sul path di I/O
- resta da chiudere meglio il percorso di validazione: test, benchmark e regressioni

## Obiettivo Finale

Arrivare a un motore in cui:

1. il thread di render produce frame e non aspetta l'encoder
2. la conversione pixel e la scrittura FFmpeg vivono fuori dal render thread
3. le aree sporche vengono calcolate davvero per frame e non solo contate
4. il grafo viene ottimizzato prima dell'esecuzione
5. la memoria viene gestita con zero allocazioni inutili nel path caldo
6. la telemetria distingue chiaramente costo di render, clear, copy, queue wait e pipe write

---

## Priorita 3: Ottimizzazione del Grafo

### Obiettivo
Compilare e semplificare il grafo prima del frame loop.

### Completato

- optimizer integrato nella pipeline di esecuzione
- dead-node elimination gia` attiva
- fusion degli `EffectStackNode` e degli `AdjustmentNode` gia` attiva
- riduzione dei framebuffer intermedi nei chain post-processing compatibili
- controllo di sicurezza basato su `frame_dependent` e consumer count per evitare regressioni sulle scene dinamiche

### Nota

Restano solo eventuali rifiniture future su pattern ancora piu` specifici, ma il blocco principale di ottimizzazione del grafo e` ora da considerare chiuso.

### Criterio di done

- il grafo esegue meno nodi per frame
- i nodi statici vengono riusati o saltati
- si riducono buffer intermedi e pass inutili

---

## Priorita 4: Memoria e Cache

### Obiettivo
Ridurre allocazioni, churn e traffico memoria.

### Completato nel commit d59a9d2

- framebuffer stride-aware in [`include/chronon3d/core/memory/framebuffer.hpp`](/home/pierone/Pyt/Chronon3d/include/chronon3d/core/memory/framebuffer.hpp)
- `PersistentBakeCache` con I/O riga-per-riga piu` efficiente in [`src/cache/persistent_bake_cache.cpp`](/home/pierone/Pyt/Chronon3d/src/cache/persistent_bake_cache.cpp)
- test dedicati per `FramebufferPool` e `PersistentBakeCache`

### Ancora aperto

- rendere il `FramebufferPool` ancora piu` prevedibile nei casi di export lunghi
- usare meglio arena e huge pages dove il profilo lo giustifica
- decidere quando preferire bake binary rispetto a bake PNG/EXR
- sfruttare meglio la cache dei frame convertiti e i bake statici nei casi ripetitivi
- capire se e` possibile ridurre ancora la pressione memoria nei render lunghi senza peggiorare il reuse

### Criterio di done

- meno allocazioni per frame
- meno overhead di clear inutile
- meno pressione sul memory subsystem

---

## Priorita 5: SIMD, Copy e Conversione

### Obiettivo
Ridurre il costo della conversione pixel e della copia verso encoder.

### Completato

- API di conversione unificata in [`include/chronon3d/video/frame_converter.hpp`](/home/pierone/Pyt/Chronon3d/include/chronon3d/video/frame_converter.hpp)
- `ConvertedFrameCache` multi-entry LRU in [`include/chronon3d/video/converted_frame_cache.hpp`](/home/pierone/Pyt/Chronon3d/include/chronon3d/video/converted_frame_cache.hpp)
- separazione dei contatori tra cache-hit, conversione e write
- pipeline writer con cache dei frame convertiti e riuso sui frame statici
- path `RGB24` allineato alla trasformazione colore del software path
- plumbing telemetrico per `converted_frame_cache_hits`

### Ancora aperto

- spingere la conversione UV con SIMD piu` aggressivo
- ridurre ancora il costo del path chroma per `YUV420p` e `NV12`
- valutare registrazione buffer / zero-copy dove possibile
- verificare che il confronto `frame_digest + color_matrix + gamma` copra tutti i casi reali
- misurare il guadagno reale sui run lunghi dopo questi fix

### Nota

Il prototipo di vectorizzazione UV con Highway e` stato provato, ma su questo baseline ha mostrato instabilita` di compilazione con GCC 14.2. Per non introdurre regressioni nella pipeline video, il path e` stato lasciato in fallback scalar sicuro. Il lavoro da chiudere resta quindi il chroma SIMD, ma va rifatto con un approccio piu` semplice o con un baseline compilatore piu` stabile.

### File da toccare

- [`apps/chronon3d_cli/utils/video/ffmpeg_pipe_yuv.cpp`](/home/pierone/Pyt/Chronon3d/apps/chronon3d_cli/utils/video/ffmpeg_pipe_yuv.cpp)
- [`apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.cpp`](/home/pierone/Pyt/Chronon3d/apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.cpp)
- [`src/backends/software/simd/highway_kernels.cpp`](/home/pierone/Pyt/Chronon3d/src/backends/software/simd/highway_kernels.cpp)

### Criterio di done

- `frame_conversion_copy_ms` cala ancora in modo sensibile
- la conversione non domina piu` il pipeline time
- la cache dei frame convertiti registra hit misurabili sui frame statici o ripetuti

---

## Priorita 6: Hardware e Backend Futuri

### Obiettivo
Portare Chronon3D oltre la CPU generica, quando il resto della pipeline e` pulito.

### Opzioni

- AVX2 / AVX-512 per i path CPU piu` caldi
- supporto GPU per conversione e/o effetti
- encoder hardware:
  - NVENC
  - VAAPI
  - AMF
- eventuale backend compute per effect stack e compositing

### Criterio di done

- il motore scala oltre la CPU genericamente disponibile
- il backend viene scelto in base a macchina e workload

---

## Priorita 7: Test, Benchmark e Regresioni

### Obiettivo
Non perdere i miglioramenti mentre si refattorizza.

### Da fare

- compilare e validare la nuova suite di test quando la toolchain e` disponibile
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

### Stato aggiornato

- il commit `d59a9d2` ha aggiunto test su `FramebufferPool` e `PersistentBakeCache`
- il backlog di test resta aperto per benchmark, video diff e golden render

---

## Ordine Consigliato Di Esecuzione

1. Rifinire l'ottimizzazione del grafo dove serve davvero.
2. Ridurre ancora il costo di memoria e conversione.
3. Chiudere la validazione della suite test.
4. Poi passare alle ottimizzazioni hardware piu` spinte.

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
