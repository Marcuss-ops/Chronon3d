# TICKET-CERT-SEQUENCE-WBH-PROTOCOL

## Stato

PARTIAL — protocol and toolchain manifest delivered; clean same-SHA machine
certification remains open.

## Problema

Formalizzare il protocollo WBH (Working-Build-Host) per la certificazione dei test first-principles in modo riproducibile tra macchine diverse.

## Criteri di accettazione

- Documento di protocollo in `docs/`.
- Almeno una certificazione macchina-verificata eseguita con successo.

## Evidenza

- `docs/CERT-SEQUENCE-WBH-PROTOCOL.md` defines tool qualification, gate order,
  exact-SHA rules, evidence fields and external certification boundaries.
- `tools/wbh_toolchain_manifest.env` and
  `tools/check_wbh_toolchain.py` pass 13 tool/configuration checks on the current
  Linux x86_64 host.
- This run's worktree is dirty. It qualifies the local toolchain only and does
  not satisfy the clean source-SHA certification acceptance criterion.

## Recheck — 2026-10-11

`python3 tools/check_wbh_toolchain.py` passed **13/13 checks** on profile
`linux-video-test`, source SHA `e8e06f5b315f4ffe39e6819e926dcfc1156b8408`.
The observed host is Linux x86_64 with CMake 4.2.3, Ninja 1.13.2, GNU C++
15.2.0, Python 3.14.4, and text/video/Vulkan/verification enabled. The output
explicitly reports `worktree=dirty`; this is toolchain qualification only, not
the required clean same-SHA certification. Vulkan enabled in the profile does
not mean a GPU test ran.
