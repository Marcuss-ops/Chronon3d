# Text tile raster canary — VERDE 78/78 — 2026-09-16

Evidenza di chiusura del blocco **P0-1** di
[`docs/tickets/TICKET-TEXT-QUALITY-V2.md`](../tickets/TICKET-TEXT-QUALITY-V2.md) e
della riga `Text tile raster canary` di
[`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md).

Stato precedente registrato: **65/78** (13 assertion di pixel a zero:
`full.*`, `half.*`, `mtsdf.*`, `counts[*] >= 1`), attribuito a "dispatch compute
che non scrive", escluso dalla lane headless e marcato "va diagnosticato su
driver/GPU".

Stato misurato oggi: **78/78, 0 failed**, tre run consecutivi identici.

---

## 1. Host e materiale

| Voce | Valore |
|---|---|
| GPU | NVIDIA RTX A4000, 16 376 MiB |
| Driver | 595.91.07 |
| Vulkan (NVIDIA) | `apiVersion` 1.4.329, `driverVersion` 2 497 102 272 |
| Preset / lane | `linux-video-test` (`CHRONON3D_ENABLE_VULKAN=ON`) |
| Binario | `build/chronon/linux-video-test/tests/chronon3d_text_tile_raster_canary_tests` |
| Sorgente test | `tests/backends/test_text_tile_raster_canary.cpp` |
| Sorgenti shader | `src/backends/vulkan/shaders/text_tile_bin.comp`, `text_tile_raster.comp` |
| SPIR-V | `…/linux-video-test/src/backends/vulkan/generated/text_tile_{bin,raster}_comp_spv.hpp` (generato 2026-09-16 06:29 UTC, non rigenerato da questa run) |
| Commit albero | `2a55799855efc242ece6650f270bddddabafe211` |

## 2. Comando e risultato

```bash
cd Chronon3d/build/chronon/linux-video-test
cmake --build . --target chronon3d_text_tile_raster_canary_tests -j 16
for i in 1 2 3; do ./tests/chronon3d_text_tile_raster_canary_tests; done
```

```
[doctest] test cases:  1 |  1 passed | 0 failed | 0 skipped
[doctest] assertions: 78 | 78 passed | 0 failed |
[doctest] Status: SUCCESS!
```

Il conteggio `0 skipped` è la parte che conta: `make_device()` non ha trovato un
device vulkano assente, quindi la suite **non** è stata saltata in silenzio — ha
eseguito davvero il dispatch compute e la lettura dei pixel.

## 3. Causa reale: binario stantio, non un difetto driver/GPU

Il difetto registrato non è riproducibile dall'albero corrente. La causa è che
le misure del 2026-09-16 **19:08** sono state prese da un binario costruito prima
del fix, senza ricostruirlo.

* `5b59cfd48` — **2026-09-16 16:33:50** — *"fix: restore text tile canary ABI
  contract"* riscrive il canary: il mirror scritto a mano da 64 byte di
  `GpuTextRunDynamic` viene sostituito dall'ABI di produzione
  (`chronon3d::runtime::TextRunDynamic`, 112 byte, stride std430 verificato da
  `static_assert`). Il commento nel sorgente lo dichiara esplicitamente: il
  mirror a 64 byte rendeva invisibile ogni glifo default-initialized e faceva
  leggere il secondo run allo stride sbagliato. **È esattamente il profilo delle
  13 assertion a zero.**
* `build/chronon/linux-video-test/.ninja_log` registra l'ultima build del
  canary (oggetto + eseguibile) a `mtime` **1789542011 s ≈ 2026-09-16 07:00 UTC**,
  cioè **~9,5 h prima** del fix delle 16:33. Tra le 07:00 e la ricostruzione di
  oggi nessuno ha ricompilato quel target: chi ha lanciato
  `./tests/chronon3d_text_tile_raster_canary_tests` alle 19:08 ha eseguito il
  binario pre-fix.
* La ricostruzione di oggi ha ricompilato solo `backends/test_text_tile_raster_canary.cpp`
  e le librerie dipendenti; i due header SPIR-V generati **non** sono stati
  rigenerati (mtime invariato 06:29 UTC). Quindi il verde **non** viene da uno
  shader nuovo: viene dal canary ricostruito contro gli shader già in albero.

Conseguenza: la diagnosi "va diagnosticato su driver/GPU" era **sbagliata**, e
l'esclusione della suite dalla lane era basata su una misura stantia. Il
dispatch compute scrive; il contratto binnig + premultiplied + MTSDF che il
canary dichiara è rispettato su questo host.

## 4. Perché l'esclusione precedente mascherava il verde

`tests/shader_abi_tests.cmake:91` costruisce `chronon3d_text_tile_raster_canary_tests`,
ma `scripts/ci/verify_vulkan_gated_unit_tests.sh` non lo include (esclusione
esplicita). Il target quindi **non** veniva ricostruito da nessun gate: nessuna
road CI lo compilava, e il commento di esclusione citava la misura stantia come
motivo per non includerlo. Un target escluso dalla lane e mai ricostruito non può
auto-correggersi: è il modo in cui una misura stale sopravvive a un fix.

## 5. Effetto sul piano

* **P0-1 (canary text-tile verde)** di `TICKET-TEXT-QUALITY-V2` → **CHIUSO**.
* Il blocco dichiarato sulla riga `Text Quality V2 (FullGraph)` in
  `FOLLOWUP_TICKETS.md` ("BLOCKED dal canary text-tile") **cade**: il programma
  resta bloccato solo da **P0-0b** (verità pre-encode: la via `render -o x.png`
  non compone il testo), che è indipendente e ha la propria evidenza.
* Il canary può tornare a essere un gate: `tools/verify_text_tile_gpu_canary.sh`
  lo costruisce ed esegue, richiedendo 78/78 quando esiste un device Vulkan.

## 6. Riproduzione

### Nota di aggiornamento ABI — 2026-09-25

Il dato da 112 byte sopra è storico e descrive l'ABI al momento della prova del
16 settembre. Rotation e fill-color per-glyph hanno poi esteso il record tile a
128 byte. Il canary attuale usa direttamente
`chronon3d::runtime::TextRunDynamic` (128 byte); `tests/test_shader_layout_contract.py`
confronta entrambi gli shader tile con i campi/size C++ e con l'`ArrayStride`
SPIR-V compilato quando `glslangValidator` è disponibile. Nel closeout 2026-09-25
questo contract test e il canary Vulkan sono passati; la certificazione video
GPU same-SHA e il corpus restano separati e non sono dichiarati completati.

```bash
Chronon3d/tools/verify_text_tile_gpu_canary.sh                     # default: lane linux-video-test
CHRONON3D_TEXT_CANARY_BUILD_DIR=<dir> tools/verify_text_tile_gpu_canary.sh
```

Nota operativa: **ricostruire sempre prima di misurare**. Il fallimento
registrato qui non era un contratto rotto ma un artefatto non rigenerato; la
stessa trappola vale per qualunque suite esclusa da tutte le lane.
