# TICKET-130: IPC Codec — Heap-Buffer-Overflow in decode_reply

| Campo | Valore |
|---|---|
| **Ticket** | TICKET-130 |
| **Area** | `src/ipc/ipc_codec.cpp` |
| **Severità** | P0 (daemon crash, untrusted input) |
| **Trovato da** | libFuzzer (`ipc_codec_fuzz`) in ~3 secondi |
| **Stato** | ✅ **CLOSED** — fix atterrato in `eabb6713a` |
| **Regression** | `tests/fuzz/regressions/ipc_codec/heap_overflow_decode_reply.bin` → CTest `fuzz_regression_ipc_codec_fuzz_heap_overflow_decode_reply` |

## Root cause

`IpcCodec::decode_reply` (e analogamente `decode_request`) chiamavano
`flatbuffers::GetRoot<IpcReplyEnvelope>()` **senza** un Verifier preventivo.
Su input malformed/truncated, l'accesso a `body_type()` e ai campi successivi
leggeva oltre il buffer, causando heap-buffer-overflow.

```cpp
// PRIMA del fix (vulnerabile):
const auto* env = flatbuffers::GetRoot<IpcReplyEnvelope>(frame.data());
const auto message_id = env->message_id();           // ← OOB su input corrotto
switch (env->body_type()) { ... }                     // ← OOB
```

## Crash input

Il fuzzer ha generato un input di **89 byte** (`tests/fuzz/regressions/ipc_codec/heap_overflow_decode_reply.bin`)
che, passato attraverso il daemon (Unix socket → length framing → FlatBuffers →
IpcCodec → daemon), causava un heap-buffer-overflow rilevato da ASan.

## Fix

Aggiunto `flatbuffers::Verifier` prima di ogni `GetRoot` in entrambe le funzioni.
Il Verifier ricorre nel `union` body (`VerifyIpcReplyBody` / `VerifyIpcRequestBody`),
validando l'intero albero FlatBuffer prima di ogni accesso ai campi.

```cpp
// DOPO il fix (sicuro):
if (frame.empty()) return std::nullopt;
flatbuffers::Verifier verifier(frame.data(), frame.size());
if (!verifier.VerifyBuffer<IpcReplyEnvelope>(nullptr)) {
    return std::nullopt;                              // ← reject early
}
const auto* env = flatbuffers::GetRoot<IpcReplyEnvelope>(frame.data());
// ... sicuro: il Verifier ha già validato l'intero albero
```

**Commit**: `eabb6713a` (atterrato su `main` prima del push dell'infrastruttura fuzz).

## Chain of events

```
fuzzer genera 89 byte di garbage FlatBuffer
         ↓
decode_reply → GetRoot → body_type() → OOB read
         ↓
ASan: heap-buffer-overflow
         ↓
Il crash input viene salvato in tests/fuzz/regressions/
         ↓
Il fix (Verifier) viene applicato in eabb6713a
         ↓
Regression test: exit 0 (clean reject, no OOB)
         ↓
CTest permanente: fuzz_regression_ipc_codec_fuzz_heap_overflow_decode_reply → PASS
```

## Verifica

```bash
# Riproduzione (prima del fix: abort ASan; dopo il fix: exit 0)
./build/fast/tests/fuzz/ipc_codec_fuzz \
  tests/fuzz/regressions/ipc_codec/heap_overflow_decode_reply.bin

# CI regression gate
ctest -L fuzz-regression -R heap_overflow_decode_reply --output-on-failure
# → Passed
```

## Lezioni

1. **FlatBuffers senza Verifier = unsafe deserialization**. Il Verifier deve
   essere chiamato **prima** di `GetRoot`, sempre, su ogni input untrusted.
2. **libFuzzer ha trovato il bug in 3 secondi** — conferma il valore
   dell'infrastruttura fuzz appena costruita.
3. **Ogni crash → regression test permanente** — il pattern funziona: questo
   bug non può più ripresentarsi senza far fallire la CI.