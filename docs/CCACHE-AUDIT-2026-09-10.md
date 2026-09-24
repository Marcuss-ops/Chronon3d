# Chronon3d ccache audit — 2026-09-10

## Result

The active shell did not define `CCACHE_DIR`. The active ccache configuration
therefore resolves to the user cache:

- cache directory: `/home/pierone/.cache/ccache`
- configured maximum: `30.0G`
- observed size: `27.28G` (`90.92%`)
- observed storage hit rate: `24.90%`
- cache reset/zeroed: **no**

The 30 GiB cap is already enforced by `/home/pierone/.config/ccache/ccache.conf`.
No statistics or cache entries were removed during this audit. The active
cache is close to its cap; operators should monitor eviction pressure before
raising the limit.

## Project-local policy

`build-fast.sh` continues to use the project-local `.ccache` when the operator
does not provide `CCACHE_DIR`, with a 20 GiB cap and compression enabled. An
explicit `CCACHE_DIR` is never rewritten by the script. CMake presets used by
the checked-in build and CLI gate select `ccache` as both C and C++ launcher.

## Configure-tree check

The stale `build/chronon/linux-video-test/` directory was absent at audit
time, so no useful configure tree was deleted. The active CLI gate was
configured under `.tmp/chronon-builds/linux-fast-dev` through the `ccache`
launcher preset; the compatibility symlink is
`build/chronon/linux-fast-dev`.

## Follow-up 2026-09-16 — a corrupt `.ninja_deps` was silently disabling incremental builds

### Sintomo

Ogni invocazione di `ninja` ri-eseguiva **tutte** le edge dell'albero
(`ninja -n` elencava ~340/873 step dopo un build riuscito) e stampava
`ninja: warning: premature end of file; recovering`. Il build "no-op" costava
~10 s per albero e, con la cache fredda o insufficiente, un rebuild completo
a ogni invocazione. Il `ninja` installato è
`1.13.0.git.kitware.jobserver-pipe-1` (`~/.local/bin/ninja`).

### Causa

Il `.ninja_deps` di ogni albero di build era troncato: ninja entrava in
modalità *recovering*, non riusciva a persistere le deps e quindi, a ogni
run, confrontava le deps salvate (di giorni prima) con l'mtime corrente degli
oggetti e le dichiarava stale (`ninja explain: stored deps info out of date`).
Gli `.o.d` (depfile) venivano cancellati da ninja dopo la lettura, come da
comportamento normale, quindi la corruzione non si auto-riparava. ccache **non
ha responsabilità**: con una cache temporanea pulita il depfile viene scritto
sia sul miss sia sull'hit (verificato con la stessa command line di
`build.ninja`).

### Rimedio (una volta per albero)

```bash
rm -f build/chronon/<lane>/.ninja_deps        # + .tmp/chronon-builds/<lane>/.ninja_deps
ninja -C build/chronon/<lane> -j18 <target>   # ri-esegue le edge (ccache hit: ~30 s) e riscrive le deps
ninja -C build/chronon/<lane> -n <target>     # atteso: "ninja: no work to do."
```

Misura il rimedio, non fidarti della sensazione: se dopo il primo run il
`-n` continua a elencare edge, il `.ninja_deps` è ancora corrotto.

### Evidenza (2026-09-16)

| Albero | Prima | Dopo `rm .ninja_deps` |
|---|---|---|
| `build/chronon/linux-video-release` | `-n` = 340 step dopo un build RC=0; run ripetuto 10.7 s | 386 edge in 30 s (ccache hit), poi `ninja: no work to do.` |
| `build/chronon/linux-video-test` (ri-configurato) | — | 645 edge (lane video+tests) |

### Altri due reperti di configurazione

1. **`.ccache/ccache.conf` aveva `max_size = 1G`**, mentre `build-fast.sh`
bootstrappa `20G` e questo documento registra la policy a 20 GiB: un cap da 1 GiB
per una lane video C++ (2874 entry mancanti) evitta in continuazione. Riportato a
`20G` (il file è gitignored, quindi è configurazione locale dell'host).
2. **Symlink pendente `build/chronon/linux-video-fast-dev`**: il target
(`.tmp/chronon-builds/linux-video-fast-dev`) era stato liberato ma il link era
rimasto, e i tool GPU lo cercano come candidato CLI. Rimosso; la lane
`build/chronon/linux-video-test` è stata ricreata come
`.tmp/chronon-builds/linux-video-test` ed è il **primo** candidato nella
discovery di `tools/verify_zero_copy_end_to_end.sh`. Tutti i tool GPU
accettano comunque `CHRONON3D_CLI=<path>` come override esplicito.

### Comando di build usato (massima velocità **entro il budget di risorse**)

```bash
cd Chronon3d
export CCACHE_DIR="$PWD/.ccache"
ninja -C build/chronon/<lane> -j12 <target>   # mold è auto-rilevato da CMakeLists.txt
```

`CMakeLists.txt` è l'unica autorità del linker (`find_program(mold)` →
`-fuse-ld=mold`, con fallback lld/gold) e i preset sono la sola autorità di
`ccache`: non passare `-fuse-ld=` a mano né un launcher diverso, altrimenti la
cache perde hit.

### Perché `-j12` e non di più (misurato 2026-09-16)

L'host ha **24 core logici e 29 GiB di RAM, condivisi** con altri processi
(`pipelinegen`, `qdrant`, `llama-server`, `dockerd`) e con altri agenti che
compilano nella stessa gerarchia. Una build a `-j18` ha portato il sistema a:
`load average 55–69` (su 24 core) e **1.7 GiB di swap in uso** su 4 GiB — cioè
thrashing, con il rischio concreto di OOM kill che uccide i build altrui oltre
al proprio. A `-j6/-j12` le stesse build completano con la RAM disponibile
stabile sopra i 20 GiB.

Regole operative:

1. `-j12` è il **massimo**; per alberi Debug senza unity (es. `linux-ci`, che è
   `CHRONON3D_UNITY_BUILD=OFF`) usa `-j6`.
2. Prima di una build lunga controlla `free -h` e `uptime`: se `available` è
   sotto ~8 GiB o il load è già alto, scendi di paralleli invece di sperare.
3. Non usare `ninja` in `-k 0` o senza `-j`: la build rispetta il numero di
   core e ignora il budget di memoria, che è la vera risorsa scarsa qui (un TU
   C++20 di questo motore con unity da 32 tocca diversi GB).
4. Un albero Debug di questo repo produce binari da ~1 GB per target di test:
   verifica `df -h` prima di configurare un'altra lane (al 2026-09-16: 145 GiB
   liberi).

## Follow-up 2026-09-21 — `-j20` diventa un opt-in esplicito (ceiling JOBS 14 → 20)

`build-fast.sh` rifiutava in modo hard `JOBS > 14`. Il ceiling ora è una sola
variabile, `CHRONON3D_MAX_JOBS` (default `20`): `JOBS=20 ./build-fast.sh …`
viene accettato, mentre il **default resta 14**, cioè il valore misurato come
sicuro su questo host condiviso. Un guard sul budget di RAM avverte (senza mai
fallire) quando la parallelisità richiesta eccede la memoria disponibile —
~1.6 GiB per unità di compilazione unity C++20 in parallelo:

```text
$ JOBS=20 ./build-fast.sh cli
⚠️  JOBS=20 needs ~31 GiB but only 22 GiB is available; expect swap pressure.
```

Il guard si silenzia con `CHRONON3D_SKIP_MEMORY_GUARD=1`.

### Verifica sul campo (host 24 core / 29 GiB, condiviso)

```text
cmake --build build/chronon/linux-video-test --target chronon3d_cli -j 20
→ 12 edge in 69.5 s, Exit status 0, Swaps: 0
```

Nessun thrashing alla dimensione di target provata. Per alberi Debug completi
(unity 16-32) resta valido il tetto `-j12` della nota 2026-09-16 sopra, a meno
che l'host non abbia davvero il budget di RAM.

### ccache + mold restano attivi (verificato, non assunto)

La lane configurata espone `CMAKE_CXX_COMPILER_LAUNCHER:UNINITIALIZED=ccache`
e `CMAKE_C_COMPILER_LAUNCHER:UNINITIALIZED=ccache` in `CMakeCache.txt`, e
`-fuse-ld=mold` nelle regole di link generate in `build.ninja`. `CMakeLists.txt`
resta l'unica autorità del linker; i preset restano l'unica autorità del
launcher ccache.

### 2026-09-24 — verifica funzionale + split del cache (host 24 core / 29 GiB)

Entrambi gli strumenti sono stati verificati **funzionalmente** (non solo
«presenti»):

- **ccache 4.5.1** — test isolato su due compilazioni identiche: run 1 `MISS`,
  run 2 `HIT`. La cache conteneva 2.05 GB / 5.00 GB nel percorso di default.
- **mold 1.0.3** — `-fuse-ld=mold` presente nelle regole di link; il linker
  effettivamente usato è `/usr/bin/ld.mold`; il binario compilato viene eseguito
  correttamente.

**Finding operativo (non corruzione):** `CCACHE_DIR` è *unset* nella shell di
questo host. `build-fast.sh` lo esporta verso `<repo>/.ccache` (20 GiB), ma una
chiamata diretta `ninja` (senza lo script) eredita l'ambiente senza
`CCACHE_DIR`: ccache ricade sul default `~/.cache/ccache` (5 GiB). Risultato: il
cache **project-local resta a 12 KB (mai popolato)** mentre i build usano quello
di default — due cache che non si parlano e hit-rate dimezzato. Per usare il
budget da 20 GiB previsto: compilare via `./build-fast.sh` oppure esportare
`CCACHE_DIR=<repo>/.ccache` prima di `ninja`.
