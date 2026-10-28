# TICKET-CERT-SEQUENCE-WBH-PROTOCOL

## Stato

PARTIAL — protocol and toolchain manifest delivered; clean same-SHA machine
certification remains open.

## TODO — cosa manca (agg. 2026-10-27)

- [ ] Certificazione same-SHA su macchina pulita (protocollo e toolchain manifest già consegnati).

### Stato osservato 2026-10-28

- La certificazione clean same-SHA resta aperta: il checkout contiene modifiche
  locali e la build completa non è stata eseguita. Gli oggetti delle unità
  toccate dal renderer sono stati compilati in modo mirato con Ninja `-j12`;
  questo verifica la compilazione, non sostituisce WBH. Il render RTX T05 documentato
  in [`evidenza T05`](../evidence/lights-shadows/T05-native-vulkan-20261028/README.md)
  è una prova GPU mirata, non WBH.
- Il remote `main` osservato è `be6b4dc3a799fe3b38fe6e54e7bddcae5a60cd5e`;
  i workflow su quel commit sono ancora in corso e non includono le modifiche
  locali. Nessun run certifica quindi il tree renderer corrente.

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
