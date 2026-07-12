# Test 15 — Pilot Protocol

> **Owner**: TICKET-TEST-15-PRODUCT-PILOT (OPEN — human-execution-required per AGENTS.md §honesty)
> **Deliverable**: ≥5 valid transcripts (`transcripts/<subject-id>-<date>.md` per `transcript-template.md`) + 1 aggregate (`transcripts/aggregate.md`).
> **What this doc is**: the SOP for the pilot host (likely you, the user). What to do, in what order, with what backup.

---

## Duration & cohort size

- **Minimum cohort**: **5** subjects (gives a median + IQR for Q1 / Q3).
- **Ideal cohort**: 8–10 (allows dropouts / inconsistencies).
- **Per-subject time**: 8–12 min total (≈4 min reading + questions, ≈6 min admin overhead).
- **Total pilot duration**: **2 hours** (set all 8–10 slots in one block; back-to-back avoids context loss).

---

## Subject selection

Pick a balanced mix across the 4 roles that the one-pager targets:

| Role | Target count | Why |
|---|---|---|
| **Founder** (early-stage, knows their domain, doesn't code) | 2 | Decision-maker persona |
| **Product manager / marketer** (writes briefs daily, runs campaigns) | 2 | Day-to-day user persona |
| **Designer / content lead** (knows After Effects or similar; skeptical) | 2 | Sophisticated-precedent persona |
| **Operator / channel owner** (publishes the videos, low technical fluency) | 2–4 | Power-user persona |

**Inclusion criteria**:
- Has produced / commissiong ≥ 1 video in the past 6 months.
- Has NOT seen the Chronon3D one-pager before.
- Speaks the language of the one-pager fluently.

**Exclusion criteria**:
- Already on the Chronon3D dev team (knowledge bias).
- Has a direct financial interest in the pilot's outcome.

---

## Pre-pilot setup (T-1 day)

1. **Print 8 copies** of `one-pager.md` (one A4 sheet each).
2. **Print 8 copies** of `feedback-form.md`.
3. **Reserve a quiet room** for 2 hours. No video calls / laptops other than the demo screen.
4. **Pre-render a Chronon3D demo** (working build host): `chronon3d_cli render ProductLaunch --props promo.json --output demo/launch.mp4` (or use the canonical `ChrononGlowFinalAE` if `ProductLaunch` is not yet registered on the host). Save as `demo/launch.mp4` + 1 thumbnail PNG `demo/launch_thumb.png`.
5. **Print the aggregate template** (`transcript-template.md` × 8 copies = 1 per subject, save the 8th as a clean aggregate skeleton).

---

## Per-subject flow (~10 min each)

### Minute 0–4: Show the one-pager
- Place the one-pager face-up on the table. Say: *"Take 60 seconds to read this — I'll answer questions after."*
- Do **NOT** explain anything. The one-pager is the test surface.
- Watch for stalls: if the subject reads for >90 sec on one row, that row is the problem.

### Minute 4–8: Ask the 3 questions
- **Q1**: read aloud verbatim from `feedback-form.md`. Wait for answer; circle YES / NO / SIMILAR / NOT SURE; write the **verbatim rationale** in the box.
- **Q2**: same. Note "this week / eventually / depends" carefully — that tells you whether the value lands.
- **Q3**: same. The numeric minute estimate is the most useful signal.

### Minute 8–10: Bonus + sign-off
- NPS 0–10.
- Verbatim quote (ask: *"If you had to tell a colleague in one sentence what you thought, what would it be?"*)
- Optional unsolicited observations.
- **Confirmation**: *"Thanks. I'll file this transcript in the project archive; the content of your responses won't be attributed to you by name in any public write-up unless you sign a release form."*

---

## Per-subject bookkeeping

Immediately after each subject leaves:

1. **Rename** `feedback-form.md` → `transcripts/<SUBJ-NN>-<YYYY-MM-DD>.md`.
2. **Fill the subject metadata table** at the top (subject ID is the filename; other fields from observation).
3. **Copy the verbatim responses** into the refresh `transcripts/aggregate.md` row for that subject.
4. **Sanity check**: if the form has handwritten scribbles only you can read, ASK the subject before they leave to clarify. Fuzzier-than-natural data is OK; unverifiable data is NOT.

---

## Aggregate (`transcripts/aggregate.md`)

After all 5–10 subjects:

| Subject ID | Date | Q1 (YES=+1, NO=-1, SIMILAR=0, NOT SURE=0) | Q2 (this_wk=+2, next_wk=+1, eventually=0, NO=-1, depends=0) | Q3 minutes saved | NPS | Verbatim quote (short) |
|---|---|---|---|---|---|---|
| SUBJ-01 |   |   |   |   |   |   |
| SUBJ-02 |   |   |   |   |   |   |
| ... |   |   |   |   |   |   |
| **Median** | — | ___ | ___ | ___ min | ___ | — |

---

## PASS criterion (CAT-15)

- **Q1**: ≥ **3 of 5 participants vote YES** (median Q1 = +1; with YES=+1, NO=-1, SIMILAR=0, NOT_SURE=0, the integer-median floor ≥ 0.5 mathematically requires ≥ 3 YES for a 5-subject cohort)
- **Q2**: median Q2 ≥ +1 (median subject intends to use it next week or sooner; "YES - this week" = +2, "YES - next week" = +1, "eventually" = 0, "NO" = -1, "DEPENDS" = 0)
- **Q3**: median Q3 ≥ 10 minutes saved per video (the headline delta vs the table on the one-pager)
- **Q3 = 0**: HARD FAIL **only if the subject had previous AI-service experience** (i.e. their answer to the meta question *"Has used a cloud AI video service before?"* was `yes`); this is the **baseline filter**. If a subject reports 0 min saved but had NO prior AI service to compare against, this is **incomplete-data signal**, NOT failure — exclude from the Q3 aggregate or report separately. (This filter prevents the test from FAILing for the wrong reason on subjects who legitimately cannot benchmark.)

If **any** Q1/Q2/Q3 criterion fails → the one-pager goes back to draft → repeat the next cohort with the panel that confused subjects iterated.

---

## Anti-tampering notes

- **NEVER** paraphrase the subject's verbatim text into "marketing-speak" before filing. The roughness is the signal.
- **NEVER** offer the subject a "what does this mean?" clarification in the middle of the 3-question flow. After they answer all 3, then chat.
- **NEVER** show the subject the comparison table in any other format (PowerPoint, video, etc.). The printout is the test surface; if it works on paper, it works.

---

## What comes next (TEST 15 closure)

After the pilot completes:

1. **If PASS** → write a one-paragraph summary to `docs/CHANGELOG.md` (Test 15 closes) + cross-link the aggregate median values + verbatim quotes into the existing `TICKET-TEST-15-PRODUCT-PILOT` row of `FOLLOWUP_TICKETS.md` (open → done).
2. **If FAIL** → update the one-pager per the **specific panel that confused subjects** (almost always the COMANDO row when the median Q3 is < 5 min, OR the COSTO row when Q3 median is > 60 min).
3. **Always**: file the aggregate + transcripts under `transcripts/`. Even if the one-pager fails, the transcripts are signal.

---

**Last updated**: 2026-07-12 · **Owner**: TICKET-TEST-15-PRODUCT-PILOT
