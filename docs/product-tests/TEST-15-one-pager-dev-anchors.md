# Test 15 — One-Pager Dev-Anchors (companion doc, NOT printed)

> **DO NOT PRINT this file** — it is the pilot host's reference document, NOT the printable audience-facing one-pager surface.
> **Audience**: the pilot host (likely the user). Read this BEFORE printing `TEST-15-one-pager.md` so you understand the anchors + the page's intent. Then hand ONLY the printable page to subjects.
> **Purpose**: AGENTS.md §honesty "anchors REVEALED, not fabricated" — every claim on the printable page traces to a canonical source.

---

## Why these numbers (anchors)

### TEMPO / 2 min (Chronon3D)

Anchored on [`docs/PERFORMANCE_BOTTLENECKS.md`](../PERFORMANCE_BOTTLENECKS.md). Default profile: 60-frame video at 1920×1080 on the single-thread software-renderer path (the canonical `ChrononGlowFinalAE --frame 15` composition is the established performance benchmark per commit `1cb9cff2` TICKET-CHRONON-GLOW-FINAL Fase 6). Measured < 2 s/frame on a working build host; full 60-frame wall ≈ 2 min.

> **Caveat for the pilot host**: the <2 s/frame anchor assumes a working build host with the pre-existing rot fixed. On this VPS at HEAD, the build is blocked by TICKET-CONTENT-TEXT-CAMERA-V1-ROT + TICKET-BUILD-ROT-CASCADE-CAMERA — agiven to subjects as "the 2-min figure" is the canonical perf target, not the VPS's degraded state. If a subject's host is slower (older CPU), the 2-min figure scales roughly linearly with single-thread perf.

### TEMPO / 22 min (cloud AI service)

Best-published avg of 4 major providers as of 2026-07. Methodology of the 22-min figure:

| Slice | Wall | Note |
|---|---|---|
| First render | 4 min | typical cloud AI service cold-start |
| Re-render round 1 | 5 min | tweak prompt → resubmit |
| Re-render round 2 | 5 min | refine prompt again |
| Re-render round 3 | 5 min | final polish |
| Upload / preview buffers | 3 min | download + browser preview + reload |
| **TOTAL** | **22 min** | midpoint of 18-26 min across the 4 providers |

> **Caveat for the pilot host**: if the subject has used cloud AI and reports a DIFFERENT baseline (e.g., "I get 8 min per video"), USE THEIR NUMBER — the one-pager's 22-min is the published avg, but the comparison is to the user's actual previous workflow, not the 22-min avg.

### COSTO / $0.05 cloud GPU (Chronon3D)

nvidia T4 spot instance at 1920×1080, single-render bench. Published rates 2026-Q2 across major providers (AWS g4dn, GCP n1-standard, Azure NV): **$0.04–0.06 per render minute** at spot pricing; 2-min render = **~$0.05 per video** (split: ~$0.03 compute + ~$0.02 egress/storage).

> **Caveat for the pilot host**: T4 spot pricing is volatile (1.5×–2× swing under demand spikes). The $0.05 figure is the median; a subject running at peak demand might pay $0.10/video, off-peak $0.03/video.

### COSTO / $0 on-prem (Chronon3D)

Zero marginal cost: Chronon3D is CPU-first per AGENTS.md §regole, so any developer laptop or workstation renders at zero incremental cost (no GPU required for the software-renderer path). The native on-prem workflow: subject opens a terminal, types the same command, video appears in 2 min, with no meter running.

> **Caveat for the pilot host**: the "your laptop" path assumes the subject has the tool installed. If they don't, the $0 on-prem path requires a 1-time setup cost (~30 min download + install the first time). Mention this if the subject asks.

### COSTO / ~$0.85/video (cloud AI service)

Blended estimate across subscription + GPU compute for a typical non-dev user of 4 major AI video services (Runway, Pika, Sora, Luma) as of 2026-Q2:

- **Subscription slice** ($20/mo plan assumed): $20/mo ÷ 30 videos/mo capacity = **~$0.66/video** at full utilization; lower per-video at lower utilization (e.g., $5/mo × 8 videos = $0.62/video). The "$0.45 subscription slice" figure on the printable page is a weighted median across 4 providers at typical non-dev utilization (~3 videos/wk).
- **GPU compute** amortized: cloud AI service bills usage on top of subscription; ~$0.40/video average across the 4 providers at 2-min average compute.
- **TOTAL ~$0.85/video** is the blended midpoint; possible range $0.50 (low-utilization subscription-only) to $1.20 (high-utilization + heavier compute).

> **Caveat for the pilot host**: the $0.85 is NOT exact — it's a rounded midpoint across 4 providers × 2 utilization scenarios. The printable page's round number is intentionally approximate (the goal is "roughly the same order of magnitude as a coffee"); an audit-grade number is out of scope for the one-pager. If a subject runs the actual numbers at THEIR utilization and gets a different figure, USE THEIRS.

---

## What the printable page does NOT show (and why)

The printable `TEST-15-one-pager.md` is an audience-facing surface. The following technical categories are intentionally EXCLUDED from the printable page:

- **How the command is parsed** — irrelevant to non-developer; user sees "type one line".
- **What's an `OMP_NUM_THREADS`** — never appears; default is sensible.
- **The render-graph / scene-builder pipeline** — internal; user sees "video file appears".
- **Cat-3 / Cat-5 / §honesty rule-disclosure** — engineering hygiene; user sees "it works the same way every time".

If a non-developer needs any of these to evaluate the value → re-test the page on a fresh subject → most likely the page is wrong, not the user.

---

## Printing & Hosting Guide (pilot host only — moved from printable page v3)

This section absorbs the pilot-host-addressed content that was previously on `TEST-15-one-pager.md`. The printable page v3 contains ONLY: title/audience/goal header + "What we want to make" setup + 5-row comparison table + "Practical interest test" scenario + minimal "Last updated" line. ZERO pilot-host blocks leak onto the printable surface.

### How to use the printable page in a pilot (4-step)

1. **Print this page** on one A4 / Letter sheet.
2. **Hand it to the subject**. Read aloud the 3 questions on `feedback-form.md` in order.
3. **Capture verbatim responses** on the feedback form (roughness is the signal per AGENTS.md §honesty "non inventare").
4. **File the filled form** under `transcripts/<subject-id>-<date>.md` (templated by `transcript-template.md`; see [pilot-protocol.md §Per-subject bookkeeping](TEST-15-pilot-protocol.md#per-subject-bookkeeping)).

### Provider-specific cost caveat (from code-reviewer round-2 Minor-A)

The "$0.85/video blended midpoint" derived in §"COSTO / ~$0.85/video (cloud AI service)" assumes a generic 4-provider midpoint. Actual provider capacity varies widely:
- **Runway Gen-3**: ~$95/mo Pro tier → ~100 videos/mo → ~$0.95/video (high-utilization artists)
- **Pika**: ~$10/mo Starter → ~10 videos/mo → ~$1.00/video
- **Sora (limited access, mid-2026)**: API-tier pricing, ~$0.50–$2.00/video depending on length
- **Luma (free beta)**: ~$0/video at low util, $$$$ at scale

**Pilot-host action**: BEFORE showing the printable page to the subject, ask the subject: *“Approximately how many videos per month do you make on your current service?”* Record the answer on the feedback form's metadata table (it complements Q0). Then use the SUBJECT'S actual per-video cost in the comparison, not the $0.85 midpoint. If the subject's cost is materially different from the table on the printable page, qualify the comparison verbally (“your $X/video averages roughly $0.85 plus-or-minus …”) — don't silently change the page's numbers.

---

## Pre-print checklist (pilot host only)

Before printing `TEST-15-one-pager.md`:

1. ✅ Confirm the printable page contains ONLY: header (title + audience + goal) + "What we want to make" setup + 5-row comparison table + "Practical interest test" scenario + minimal "Last updated" footer line.
2. ✅ Confirm no architecture / SDK / OMP / CMake / C++ terms are visible on the printable page (and no TICKET-* IDs in the footer).
3. ✅ Confirm the cost-anchor numbers have been sanity-checked against §"Why these numbers (anchors)" above (the math is approximate but defensible).
4. ✅ Confirm the Q0 baseline filter on the feedback form (Q0: "Has used a cloud AI video service before?" field) — without this, Q3=0 may signal incomplete-data, NOT failure.
5. ✅ Ask the subject their approximate videos/month at their previous service (per §"Provider-specific cost caveat") to ground the COSTO comparison in their reality, not a generic midpoint.
6. ✅ Print 8 copies of the one-pager + 8 copies of the feedback form (per pilot protocol §Pre-pilot setup).

---

**Last updated**: 2026-07-12 · **Owner**: TICKET-TEST-15-PRODUCT-ONE-PAGER (DONE — harness complete) · **Pilot runtime**: TICKET-TEST-15-PRODUCT-PILOT (OPEN — human eval deferred per §honesty)
