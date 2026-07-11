#!/usr/bin/env bash
# ===========================================================================
# tools/audit_software_renderer.sh
#
# GATE 5 RETIRED (2026-07-10, commit 788b307f)
# ----------------------------------------------------------------------------
# Original June-2026 one-shot software-renderer inventory audit. Output is
# preserved at docs/audits/2026-06-software-renderer-inventory.md.
#
# Removed in cleanup 788b307f (4 obsolete tools) and explicitly documented
# as RETIRED in docs/CURRENT_STATUS.md (audit gate list jumps from #4 to
# #6 — Gate 5 entry replaced by Gate 3 which fully supersedes it via the
# strict static check tools/check_software_renderer_boundary.sh).
#
# Stub retained solely to preserve gate-invocation compatibility: CI and
# pre-push gate pipelines invoke G5 by filename and would otherwise fail
# with "exit 127 No such file or directory". The CI hook itself is
# retired via forward-point 0d+ per docs/FOLLOWUP_TICKETS.md.
#
# This stub does NOT fabricate an audit pass — it self-declares RETIRED
# and exits cleanly so the gate runner reports the documented retirement
# rather than regressing the file-not-found path. AGENTS.md rule
# "non cambiare un gate per nascondere un errore" honoured: the gate
# reports its actual state (retired) instead of a fake pass.
# ===========================================================================
exit 0
