# GEL Template route reproduction — 2026-11-11

## Result

The clean Template branch now emits the intended seven-layer structure through
`GelCinematicTitlePack`: one plate, three extruded text backings, then three
opaque GEL faces. Its focused contract test passes (19 checks, 0 failures).

The pixel reproduction gate is **FAIL / unresolved**. The tracked golden
RenderPlan does not regenerate its tracked PNG: the replay has a stronger
green/yellow rim and halo and a different face treatment. The side-by-side
comparison is [`golden-comparison.png`](golden-comparison.png). The generated
Template plan also renders, and frames 0 and 10 of the 11-frame camera-push
sequence remain legible, but that is not yet a match to the target look. The
historic artifacts are preserved unchanged.

## Current golden replay

On NVIDIA GeForce RTX 4060 Ti, driver 610.88, clean CLI
`build/chronon/win-gpu-release-clean/apps/chronon3d_cli/chronon3d_cli.exe`,
with `--backend vulkan --gpu-hot-path-mode require_gpu_native`, rendering the
tracked 1094×143 `final.plan.json` produced:

CLI SHA-256: `ED780FD8D5C6FC6842D42CA34FC9794EDDDBEA3FACEB4306DF25FAB8D25C0506`.

| Measurement | Value |
|---|---|
| Current plan SHA-256 | `6036A953FA80B5CEA5CD333925B7C8A346552E9F4916CBB0D6F948957FAB38D5` |
| Current tracked PNG SHA-256 | `AE4787BD7ECBFB98323D434F988D8A0BADC51510F4B8909E27C72B21E96EEC5E` |
| Replayed RGBA SHA-256 | `3CC9A3BD6FE0730A093089586182DA24B28175D83642382BB32BCB15F2B91784` |
| Replayed PNG SHA-256 | `1F6DA1D1A7DA83D12A7F82C494D5F5117CAD104AEB2E34A2E0D4812819734669` |
| Mean absolute RGB difference vs tracked PNG | `7.7568` levels |
| Pixels whose max-channel difference exceeds 2 | `45,888 / 156,362` |

The evidence README still lists different historical hashes for the plan and
PNG, so those published hashes do not describe the files currently tracked at
this revision. The source SHA of the clean CLI and the dirty-checkout CLI
produced identical RGBA bytes in the earlier baseline check; the discrepancy
is reproducible and is not explained by the dirty renderer edits.

## Template-generated route

`template.plan.json` is emitted by `chronontemplate_emit_gel_cinematic_title`
from the explicit reference-mode preset (7 layers). `frame0.rgba/.png` and the
11-frame camera-push sequence are strict-native Vulkan output; `frame10.png` is
extracted from that sequence. The sequence raw was removed after review to
recover disk space; its SHA-256 and the captured frame PNGs remain below.

| Artifact | SHA-256 |
|---|---|
| `template.plan.json` | `76EA073F937A445C4939AC6FB2A96FE05CBDAD62CB94C9D7F7CCA508F9BD46CC` |
| `frame0.rgba` | `349BF23C369AE5B08E933F129E57A8A44468352EE8BBC03C3D31CB32A2AD2D74` |
| `frame0.png` | `485792078FD5011928F59B3702F60E75AC64815E57977DC0C4EBD6F555C297B5` |
| `frame10.rgba` | `00BF9C76C9ABE417C56824644A862D752DC1BC2B55BD96478CB98E1B783CF2C7` |
| `sequence.rgba` (frames 0–10, removed after review) | `F57C7A7BC629E104EE0515F76093B28B92BE99CE9E16BB99ADAC6D01917CBDC6` |
| `frame10.png` | `91AB5C396E630DCAAC8E1A74BE129E8247093B0306065A84B4BC3A367AD2E34A` |
| `golden-rerender.rgba` | `3CC9A3BD6FE0730A093089586182DA24B28175D83642382BB32BCB15F2B91784` |
| `golden-rerender.png` | `1F6DA1D1A7DA83D12A7F82C494D5F5117CAD104AEB2E34A2E0D4812819734669` |
| `golden-comparison.png` | `80E4BCB33364574AA6A9EEE1BE2410369BE8A5D8D3AAA76481A818289B820486` |

The `golden-rerender.*` files contain the direct replay of the tracked golden;
`*.log` files retain the CLI invocation output. This report records the
remaining visible mismatch and prevents the seven-layer contract PASS from
being mistaken for a visual closeout PASS.
