# TICKET-NVENC-DUPLICATE-REPLICATE — la run statica NVENC non prende il path di replica

- **Stato:** CLOSED — 2026-11-16
- **Chiusura:** rimossa la scorciatoia `if (!gpu_nvenc_)` + optimistic-Replicate per pts 0 (IDR di costruzione). Il run alimenta seed intra forzati finché l'anchor pubblica (adopt in drain_packets), poi replica. Verifica su RTX A4000: `P1 duplicate runs keep exact pixel parity` 306/306 PASS, `replicated==true`, 24/24 hash identici.
- **Ambito:** Chronon3d, export video nativo NVENC, contratto duplicati
- **Origine:** suite completa 2026-11-16, `chronon3d_native_decoder_teardown_tests`

## Sintomo

Il caso «P1 duplicate runs keep exact pixel parity (resubmit path)»
(`tests/video/test_native_av_encoder_teardown_stress.cpp:523`) fallisce sul
sotto-caso NVENC con `real_frames=1`:

```
test_native_av_encoder_teardown_stress.cpp:595: CHECK( static_case.replicated ) is NOT correct!
  values: CHECK( false )
test_native_av_encoder_teardown_stress.cpp:609: CHECK( hash == dup.front() ) is NOT correct!
  values: CHECK( 4e8d4624eaa95682d03965db67cc8477 == 47d58bc2ef947a4b9b5548ea6ba2cb45 )
  values: CHECK( e81b967f21a280193a739e5fed7c0808 == 47d58bc2... )
  values: CHECK( c750c6a5ba3cf3cbec7cf704ced92ec4 == 47d58bc2... )
  values: CHECK( f94740a75d3a8c3d78915b190d1ffefc == 47d58bc2... )
```

Il commento del test dichiara il contratto: con `real_frames=1` il primo frame
della sessione è per costruzione un key packet, quindi la run deve prendere il
path O(1) di replica e le 24 righe devono essere identiche al root. Su questo
host NVENC la replica non viene presa e i duplicati divergono.

## Cosa non è rotto

- I due casi `libx264` (resubmit con 2 frame reali e reveal-then-hold)
  passano: la parità reference/duplicated è corretta.
- Il caso NVENC con `hold_start=8, real_frames=9` passa: il path di resubmit
  hardware produce gli stessi pixel del render completo.
- Non è un problema di decoder o di teardown: il binario si chiude pulito e il
  fallimento è confinato al flag di replica.

## Piano di chiusura

1. **Strumentare la decisione.** In `NativeAvEncoder::write_duplicate_frame`
   (`apps/chronon3d_cli/utils/video/native_av_encoder_write.cpp:351-475`) e
   `duplicate_run_replicated()` (`native_av_encoder.hpp:225`) loggare: tipo di
   sessione, PTS del primo pacchetto, flag key (`AV_PKT_FLAG_KEY`), valore di
   `duplicate_replicated_through_pts_`.
2. **Verificare l'anchor.** Stabilire se su NVENC il primo pacchetto non è key,
   oppure se arriva dopo la decisione di run-mode (frame threading/ritardo
   dell'encoder). Il test forza `encode_threads=1`, quindi il sospetto è il
   ritardo della sessione hardware, non la concorrenza.
3. **Correggere il percorso hardware.** Se l'anchor non è ancora uscito,
   forzare il flush del primo frame prima della prima `write_duplicate_frame`
   (o richiedere un IDR sul frame 0) e verificare che
   `duplicate_replicated_through_pts_` venga aggiornato.
4. **Fallback onesto.** Se NVENC su un dato driver non può garantire un key
   packet al primo frame, dichiarare la policy fail-closed (replica O(1) solo
   con anchor key) e allineare il test alla policy reale, documentando il
   perché. Non allentare la parità pixel.
5. **Doppia verifica.** Rieseguire su host NVIDIA di riferimento e con
   `libx264`; i casi resubmit e la parità reference devono restare invariati.

## Criteri di accettazione

- [ ] `static_case.replicated == true` su NVENC con anchor key, oppure policy
      fail-closed documentata e test coerente.
- [ ] Le 24 hash dei duplicati sono identiche al root.
- [ ] Nessuna regressione sui casi `libx264` e sul caso NVENC resubmit.
- [ ] La correzione è verificata su un host con NVENC reale, non solo in
      software.

## Riferimenti

- `Chronon3d/tests/video/test_native_av_encoder_teardown_stress.cpp:173-230`
  (`duplicate_parity_pair`), `:523-621`
- `Chronon3d/apps/chronon3d_cli/utils/video/native_av_encoder_write.cpp`
- `Chronon3d/apps/chronon3d_cli/utils/video/native_av_encoder.hpp`
- `Chronon3d/.tmp/ctest-full-20261116b.log`
