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
