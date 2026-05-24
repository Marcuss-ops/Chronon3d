# Chronon3D Roadmap Tecnica Unica

> Documento unico di riferimento per il refactor di Chronon3D.
> Obiettivo: portare il motore verso una pipeline producer/consumer piu` industry-grade, con telemetria chiara e ottimizzazioni reali sui colli di bottiglia.

## Stato Attuale

La base del motore e` ormai buona, ma il lavoro ancora aperto e` quello che separa una pipeline "funziona bene" da una pipeline davvero scalabile:

- telemetria e report sono molto piu` leggibili e coerenti
- pipeline video producer/consumer e` gia` disaccoppiata dal render thread
- dirty rects geometrici sono stati introdotti, ma non sono ancora sfruttati ovunque
- il grafo ha una prima fondazione di optimizer, ma non e` ancora compresso abbastanza
- la cache dei bake persistenti e` presente, ma va ancora rifinita l'integrazione
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

### Ancora aperto

- integrare l'optimizer nella pipeline di esecuzione dove serve
- estendere le fusioni a piu` pattern di nodi
- ridurre ulteriormente buffer intermedi e pass ridondanti

### Criterio di done

- il grafo esegue meno nodi per frame
- i nodi statici vengono riusati o saltati
- si riducono buffer intermedi e pass inutili

---

## Priorita 4: Memoria e Cache

### Obiettivo
Ridurre allocazioni, churn e traffico memoria.

### Ancora aperto

- rendere il `FramebufferPool` piu` prevedibile nei casi di export lunghi
- usare meglio arena e huge pages dove il profilo lo giustifica
- decidere quando preferire bake binary rispetto a bake PNG/EXR
- sfruttare meglio la cache dei frame convertiti e i bake statici nei casi ripetitivi

### Criterio di done

- meno allocazioni per frame
- meno overhead di clear inutile
- meno pressione sul memory subsystem

---

## Priorita 5: SIMD, Copy e Conversione

### Obiettivo
Ridurre il costo della conversione pixel e della copia verso encoder.

### Da fare

- spingere la conversione con SIMD piu` aggressivo
- evitare copie inutili quando il frame e` gia` nel formato giusto
- specializzare i path piu` comuni:
  - `RGBA -> YUV420p`
  - `RGBA -> NV12`
  - `RGBA -> RGB24`
- valutare registrazione buffer / zero-copy dove possibile
- tenere separato il costo di conversione da quello di scrittura pipe

### File da toccare

- [`apps/chronon3d_cli/utils/video/ffmpeg_pipe_yuv.cpp`](/home/pierone/Pyt/Chronon3d/apps/chronon3d_cli/utils/video/ffmpeg_pipe_yuv.cpp)
- [`apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.cpp`](/home/pierone/Pyt/Chronon3d/apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.cpp)
- [`src/backends/software/simd/highway_kernels.cpp`](/home/pierone/Pyt/Chronon3d/src/backends/software/simd/highway_kernels.cpp)

### Criterio di done

- `frame_conversion_copy_ms` cala ancora in modo sensibile
- la conversione non domina piu` il pipeline time

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
