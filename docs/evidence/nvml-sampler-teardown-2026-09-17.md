# NVML sampler — il tail di teardown che nessun timer vedeva (2026-09-17)

Ambito: [`TICKET-DAEMON-JOB-LIFECYCLE`](../tickets/TICKET-DAEMON-JOB-LIFECYCLE.md)
§4 leva 1 (l'~1 s intermittente) e §6 (attribuzione con i timer esistenti).
Host: RTX A4000 16 GB, driver 595.91.07. Ogni numero è **misurato**.

## 1. Metodo: cercare la classe di difetto, non il sospetto

Il ticket lasciava un residuo: «~1 s **intermittente** che nessun timer
attribuisce», da cercare sul percorso di produzione con i timer già esistenti.
Il metodo di questa sessione è stato diverso e più economico: cercare **la classe
di difetto già trovata una volta** — un worker periodico con un
`sleep_for(interval)` non interrompibile, `join()`-ato da un distruttore sul
percorso serializzato del job. È esattamente come è stato trovato
`SystemMetricsCollector` (che join-ava un `sleep_for(1s)`), e la ricerca statica
su `src/core`, `src/media` e `apps/chronon3d_cli` (grep di `sleep_for`,
`wait_for`, `.join()`, `waitIdle`) ha trovato **un secondo esemplare**:

```cpp
// apps/chronon3d_cli/utils/telemetry/nvml_sampler.hpp (prima)
void stop() {
    if (!m_running.exchange(false)) return;
    if (m_worker.joinable()) m_worker.join();      // <- attende la fine del tick
}
// worker: while (running) { sample_once(); sleep_for(interval); }
```

`NvmlSampler` è una **variabile locale di `video_export_pipe`**
(`commands/video/exporters/video_export_pipe.cpp:165`), avviata con
`interval = 250 ms` e fermata a riga 216, cioè dentro il servizio serializzato
del job. Ogni job pagava quindi:

* il **resto dell'intervallo** (0–250 ms, uniforme, a seconda di dove cade il
  tick) — invisibile a ogni timer, perché accade dentro il join;
* un eventuale **sample già in volo** (una query NVML è una chiamata al driver, e
  la famiglia delle utilization è rate-limited dal driver stesso).

## 2. Fix (due parti, una authority)

**(a) L'attesa è ora interrompibile.** Il `sleep_for` è sostituito da una
funzione nominata e testabile, `wait_for_next_tick` (condition_variable +
`notify_all` dal `stop()`), così il teardown non attende mai il resto del tick.
È la stessa forma del fix già atterrato su `SystemMetricsCollector`.

**(b) Il costo residuo è MISURATO, non inferito.** `NvmlStats::sampler_stop_ms`
riporta la durata reale di `stop()`, ed è stampato nella riga di log che il
ticket già usa per attribuire il servizio:

```
[video] render_ms=… encode_ms=… writer_encode_ms=… wall_ms=… nvml_stop_ms=…
```

Un valore grande ora è quindi **una chiamata al driver in volo**, e non più un
costo senza nome.

## 3. Before/after, misurato sullo stesso host

**(a) Il tail rimosso** — la forma vecchia (worker che dorme l'intervallo e
controlla il flag solo dopo), fermata subito dopo l'ingresso nel tick,
`interval = 250 ms`, n = 20:

| | min | p50 | max |
|---|---|---|---|
| forma vecchia, `join` di un `sleep_for(250ms)` | 5.5 ms | **250.0 ms** | 250.1 ms |

**(b) Dopo il fix** — `NvmlSampler` reale, stesso host, n = 20 per caso:

| caso | min | p50 | max |
|---|---|---|---|
| `stop()` con il primo sample in volo | 0.09 ms | **0.12 ms** | 0.21 ms |
| `stop()` a 249 ms dentro un intervallo da 250 ms | 0.10 ms | **0.14 ms** | 0.26 ms |

Il valore riportato coincide con quello misurato esternamente
(`reported_stop_ms` 0.13–0.18 contro `measured_stop_ms` 0.12–0.18, 5 run),
cioè la telemetria dice la verità sulla propria coda.

## 4. Conclusione onesta: cosa questo spiega e cosa no

* **Spiega**: un costo variabile per job, non attribuito, **fino a 250 ms** —
  esattamente la forma «costante che non scala con i frame» descritta nel ticket
  (misurata su un job da 937 ms e su uno da 1 980 ms).
* **Non spiega**: l'~1 s intermittente. I valori misurati qui sono 0.12–0.26 ms,
  cioè su questo host le query NVML non sono lente. Il residuo da ~1 s resta
  **non attribuito** e va cercato sul percorso di produzione con i timer
  esistenti (`encoder_reset` / `full_graph_reset` erano i candidati nominati).
  La differenza rispetto a prima è che ora ogni sospetto di questa classe è
  **strumentato e pinnato da un test**, invece di essere silenzioso.

## 5. Regressione

`tests/cli/test_nvml_sampler_lifecycle.cpp` (registrato in
`tests/cli_tests.cmake`, suite `chronon3d_cli_tests`) pinna:

1. l'attesa è interrotta da uno stop e non scade mai: con un intervallo di 5 s
   il ritorno deve avvenire in <250 ms (la forma vecchia impiegherebbe ~5 s);
2. un intervallo scaduto restituisce il controllo al loop (nessun hang);
3. su un host senza NVML il sampler è inerte e **non fabbrica** una misura
   (`sampler_stop_ms == -1`); con il driver presente il costo è riportato
   (`>= 0`) e un secondo `stop()` non produce una seconda misura.

Esito: `chronon3d_cli_tests` **75/75 casi, 1012/1012 assertion**, 0 failed
(`check_cli_tests.sh`, ccache + mold, `-j16`).

## 6. Ripro

```bash
# la suite (contiene i tre casi nuovi)
cd Chronon3d && JOBS=16 bash tools/check_cli_tests.sh

# la coda in isolamento: start → stop, e il costo riportato
g++ -std=c++20 -O1 -I include -I apps/chronon3d_cli \
    -isystem vcpkg_installed/linux-fast-dev/x64-linux/include probe.cpp -lfmt -ldl
```
