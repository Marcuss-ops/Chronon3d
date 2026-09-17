# ColorContract e FrameHash — evidence (2026-09-17)

## ColorContract

Il repository possiede già `runtime::FrameFormat` come authority per pixel
format, primaries, transfer, matrix, range e alpha. Il lowering dei colori di
stile ora usa la stessa regola in un solo punto:

`include/chronon3d/render_plan/color_utils.hpp::parse_hex_color()` interpreta
`#RRGGBB` come CSS/sRGB e restituisce un `chronon3d::Color` nel dominio linear
canonico. Vulkan, CUDA e software ricevono quindi lo stesso valore; non viene
aggiunta una seconda conversione per backend.

Gate numerico: `#808080` sRGB → linear `≈0.2158605`, con alpha invariato.

## FrameHash

`tools/raw_frame_hash.py` estende la pixel-truth lane raw senza introdurre un
nuovo renderer o un nuovo harness. Legge i frame RGBA8 tight concatenati,
rifiuta file troncati/non multipli della dimensione frame e produce:

```text
CHRONON FRAMEHASH v1
engine_sha=...
plan_digest=...
pixel_format=rgba8
width=...
height=...
frame_bytes=...
frames=...
frame=0 sha256=...
```

PNG resta la proiezione per ispezione umana; il manifest è il gate macchina per
golden text/color/fusion e può essere usato prima della futura lane Slang.

## Verification

```sh
python3 tools/raw_frame_hash.py --self-test
cmake --build build/chronon/linux-asan-native \
  --target chronon3d_core_tests -j10
./build/chronon/linux-asan-native/tests/chronon3d_core_tests \
  -tc='*Color contract*'
```

La capability matrix encoder resta nelle authority esistenti:
`resolve_encoder_config()`, `validate_for_benchmark()` e `chronon doctor`.
`AVBufferRef`-like lifetime, checkasm differential harness e cancellation I/O
restano differiti finché non esiste una causa o un gate che li richieda.

Risultati sul tree ASan `linux-asan-native`:

- core color gate: **1/1 test, 5/5 assertion**;
- text semantic gate: **1/1 test, 9/9 assertion**;
- NVENC resolver: **1/1 test, 6/6 assertion**;
- capability matrix: **34/34 test, 179/179 assertion**;
- GOP parity corpus: **12/12 test, 138/138 assertion**.

Il filtro encoder integration ha completato **26/26 test e 311/311 assertion**;
la chiusura del processo ASan resta sporca per 1.141 byte allocati internamente
da FFmpeg, quindi non viene usata come gate del resolver.
