# VDP command slot timing — conclusions

This document was written with assistance from an AI coding agent.

Frozen 2026-08-22. Working summary is now `FINDINGS3.md`.
Exploration notes: `FINDINGS.md` (frozen).

Data: `scr5-{dispOff,sprOff,sprOn}-*-noCpu-*.txt` in this directory
(282 original files + 201 `*nx??-?d.txt` extra-NX captures; §3.3).
Model: after access at `t`, wait fixed `W`, then take first available slot ≥ `t+W`.
Line period = 1368. Measurement times = slot table + 1 (see §2 for two EOL exceptions).

---

## 1. Trace / model (brief)

- Row = cycle in line; column = successive display line; `time = 1368*col + t`.
- Accesses: `R.s` / `R.d` / `W.d` + address. Variants: plain / `b` (64px) / `c` (4px) /
  `nxW` (pixel width `W` from the filename). `nx` files often tag `R..` / `W..`;
  writes become `W.d`, and LMMM reads are labeled `R.s`/`R.d` from the `R,R,W` cadence.
- Line break (address heuristic): dest (or src) page-cross `//0x80`; `|Δ|≤1` is
  mid-line even at `0x7f→0x80`. For `b`/`c` also fixed strides 97/127.
- `S` (command start) not fitted — traces are mid-command only.

---

## 2. Hardware slot tables

Three operating modes → three slot sets (counts from histograms):

| Mode | Slots / line | Measurement vs canonical table |
|------|--------------|--------------------------------|
| dispOff (screen off) | 154 | +1 everywhere; **1324/1334 → +2** (obs. **1326/1336**) |
| sprOff (bitmap, sprites off) | 88 | +1 everywhere; **1322/1332 → +2** (obs. **1324/1334**) |
| sprOn (bitmap, sprites on) | 31 | **+1 only** (incl. 1330→1331); no extra shifts |

Canonical lists live in openMSX `VDPAccessSlots.cc`; the two EOL +2 corrections above are required for the traces / histograms. All fits below use these corrected tables.

---

## 3. Combined wait parameters

Plateaus are from matching transitions across modes (each mode with its own slot table).
Traces constrain the **pre-access wait** (mid-line `P`/`Pr`/… and break `P+L`/`Pr+L`/…);
**L** is inferred as break−mid. Plateaus include odd and even integers; below, **preferred
representatives are chosen even** where the plateaus and cross-command heuristics allow
(possible ÷2 internal command clock as a tie-break — not required by the data).

### 3.1 Plateaus

| Command | Access pattern | Pixel waits (plateau) | **L** (plateau) | Status |
|---------|----------------|----------------------|-----------------|--------|
| **HMMV** | `W` | P∈[45,48] | **[55,59]** | Clean |
| **YMMM** | `R.s`↔`W.d` | Pw∈[21,24], Pr∈[37,38] | **[65,67]** | Clean† |
| **HMMM** | `R.s`↔`W.d` | Pw∈[21,24], Pr∈[61,64] | **[64,67]**‡ | Partial — see §4 |
| **LMMV** | `R.d`↔`W.d` | Pw∈[21,24], Pr∈[71,72] | **[57,61]** | Clean§ |
| **LMMM** | `R.s`→`R.d`→`W.d` | two mode sets — §3.2 | see §3.2 | Clean **per mode** |

Combined three-mode plateaus are **unchanged** after the `nx` set (§3.3). A few
*per-mode* bands got slightly narrower (still contain the preferred evens).

### 3.2 Preferred representatives (all even)

Heuristics used when picking inside plateaus:

1. **Even** values only (for now).
2. **HMMV.L = LMMV.L** (both destination-only fills) → only even in [57,59] is **58**.
3. **YMMM.L = HMMM.L = LMMM.L** in all three modes (copy-family line-step) → **66**.
   Equivalently **YMMM.L ≤ HMMM.L**, which already forces HMMM.L off 64.
4. **YMMM.Pw = HMMM.Pw** (byte copy after src read) → **24**.
5. **LMMV.Pw = LMMM.Prd** (`R.d`→`W.d` RMW) → **24**.
6. **HMMM.Pr = LMMM.Pwd** (`W.d`→`R.s`) → **62** when HMMM.L=66 and Pr+L=128.
   LMMM sprOn uses Pwd=**64** only so Pwd+L can be 130 with the same L=66
   (Pwd=62 is still a perfect *mid-line* fit on sprOn; see §4.2–4.3).

| Command | Preferred (even) | Implied break wait |
|---------|------------------|--------------------|
| **HMMV** | P=**46**, L=**58** | P+L=104 |
| **LMMV** | Pw=**24**, Pr=**72**, L=**58** | Pr+L=130 |
| **YMMM** | Pw=**24**, Pr=**38**, L=**66** | Pr+L=104 |
| **HMMM** | Pw=**24**, Pr=**62**, L=**66** | Pr+L=128 |
| **LMMM** dispOff/sprOff | Prs=**32**, Prd=**24**, Pwd=**62**, L=**66** | Pwd+L=128 |
| **LMMM** sprOn | Prs=**34**, Prd=**24**, Pwd=**64**, L=**66** | Pwd+L=130 |

HMMM is **one** preferred tuple for all three modes. Mid-line Pw/Pr do not
need a sprOn split: sprOn Pr=62 is 420/420 (same as Pr=64). The sprOn problem
is only **line-break** wait (§4.2), same open issue as LMMM Pwd+L. Raising
sprOn HMMM.Pr to 64 would lift Pr+L to 130 and help those breaks, but that is
a parametrization of the open issue, not a measured mid-line Pr change (and
Pr+L=130 is still 127/132, not 132/132).

LMMM **does** need two sets, because **Prs** plateaus do not overlap
([29,32] vs [33,52]). Inside the sprOn set, **Pwd=64, L=66** matches
**Pwd=62, L=68** on noCpu pairs (2822/2828); **Pwd=62, L=66** (sum 128) is
worse on breaks (15/79). Mid-line Pwd=62 is already perfect on sprOn
(862/862). YMMM already uses L=66 in all modes.

**L at a glance** (preferred): fills **58**; YMMM / HMMM / LMMM **66** (all modes).

† A few `*b` mid-line “fails” are page-cross with `Δ==1`; they fit `Pr+L` if labeled as breaks.  
‡ Raw HMMM L from Pr+L=128 is [64,67]; heuristic (3) keeps **66**. sprOn breaks still need Pr+L≥129 (§4.2) — not absorbed into a second HMMM.Pr.  
§ LMMV EOL wraps want Pr+L≥133 (`8b` and many `nx`); ignored in favour of L=58 / Pr+L=130 (§4.4).

**L** is mode-independent on the preferred set. Pixel waits are mode-independent
where the model fits (HMMV, HMMM mid-line, YMMM, LMMV). LMMM.Prs (and the
sprOn LMMM *break* pick Pwd=64) still need a sprOn vs off split. Slot layout
is always mode-dependent.

### 3.3 Extra-NX captures (`*nx??-?d.txt`)

201 files. Goal was new command-line widths (not 4/64/256) to leave the old
slot orbits: more line-breaks, and writes just before the refresh hole.
Screen 5, noCpu. Coverage (files × NX):

| Mode | Command | New NX |
|------|---------|--------|
| dispOff | HMMM | 10, 12, 16, 18, 30 |
| dispOff | HMMV | 2, 24, 26 |
| dispOff | YMMM | 2, 6, 12 |
| dispOff | LMMV | 2, 3, 6, 7 |
| dispOff | LMMM | 3, 14, 16 |
| sprOff | HMMM | 12, 16, 18 |
| sprOff | LMMV | 6, 8 |
| sprOff | LMMM | 2, 3 |
| sprOn | HMMM | 2, 16 |
| sprOn | HMMV | 2 |
| sprOn | LMMM | 2, 3 |

**Preferred even tuples still match.** No contradiction that would force a
different representative. Exact mid-line counts stay at the old plateaus;
break counts scale up as intended (short NX).

What *did* change:

| Item | Before `nx` | After `nx` | Notes |
|------|-------------|------------|-------|
| LMMM Prs dispOff-alone | [26,32] | **[29,32]** | 9 traces need ≥29; now matches the combined off-mode plateau. Preferred **32** still at the right edge. |
| HMMM Pw dispOff-alone | [18,24] | [20,24] | Combined three-mode still **[21,24]**. |
| LMMM Prd dispOff-alone | [18,24] | [20,24] | Combined still **[21,24]**. |
| YMMM Pr+L | 482/482 at [103,104] | **1017/1017** | Many more breaks; **L not tighter** ([65,67] still). |
| HMMV sprOn NX=2 | — | 485/485 at P+L=104 | One byte/line ⇒ every transition is a break; no extra mid-line P. |
| HMMM/LMMM sprOn breaks | 132 / 79 | **359 / 222** | Same ≤128 vs ≥129 split (§4.2); not resolved. |

Slot-equivalence still limits the leftover integers (e.g. P∈[45,48] is one
class on these three maps). Extra NX cannot split that.

Three leftover miss classes are the open issues below, not new waits:
refresh-gap Δ=60 (§4.1), sprOn break 128 vs 129 (§4.2), LMMV EOL breaks (§4.4).
sprOff LMMM NX=2 also has 3 `R.s→R.d` with feasible wait ≥33 (same direction as
sprOn Prs); too few to split sprOff.

---


## 4. Open issues

### 4.1 Refresh-gap mid-line (HMMM, some LMMM)

After a write whose `t+60` lands at or after the large hole, hardware takes
`next_slot(t, 60)` — **Δ = 60** whenever that slot exists. That is **not**
“always the first post-hole slot”:

| Mode | Write `t` | HW next (mid-line Pr/Pwd) | `t+60` |
|------|-----------|---------------------------|--------|
| dispOff | 105 | 165 | 165 (first after hole) |
| dispOff | 113 | 173 | 173 (skips 165) |
| dispOff | 121 | 181 | 181 (skips 165 and 173) |
| sprOff | 103 | 163 | 163 |
| sprOff | 111 | 171 | 171 |
| sprOff | 1207 | 1267 | 1267 |

The `nx` set was built to produce these writes (HMMM NX=16/30/12/…, LMMM NX=3,
LMMV NX=2/6). They showed up in volume: e.g. HMMM dispOff `nx` has 63 mid-line
Pr with Δ=60 (19+21+23 on 105/113/121); sprOff `nx` has 102 (42+60 on 103/1207).

`Pr/Pwd ∈ [61,64]` predicts the *next* slot after `t+60` when `t+60` is itself
a slot, so every Δ=60 mid-line case is a miss. **Excluding Δ=60**, HMMM Pr and
LMMM Pwd are **perfect** on [61,64] in both off-modes (old and `nx`). Keep
preferred **62**; do not move Pr to 60 to swallow the gap (that loses bulk
mid-line on dispOff: 61–64 is 1000/1000 of non-gap old Pr, 60 is 945/1000).

A three-mode *sum* can peak at Pr=60 only because sprOff gap volume is large;
that is the same tension, not a new plateau. No single Pr/Pwd fits both the
gap and the bulk mid-line set. Unresolved.

### 4.2 Line-break wait: sprOn vs other modes (HMMM, LMMM)

| | Pr+L / Pwd+L = 128 | = 129 |
|--|-------------------|-------|
| dispOff + sprOff | near-perfect | poor |
| sprOn | near-zero | perfect |

Same ≤128 vs ≥129 split with the extra `nx` breaks (HMMM sprOn 359 pairs:
128 → 53/359, ≥129 → 346/359, max at [133,134] 359/359; LMMM sprOn 222 pairs:
128 → 26/222, ≥129 → 209/222). Unresolved — do **not** treat this as a second
HMMM preferred tuple. (LMMM already has a separate sprOn set because of Prs;
there Pwd=64 with L=66 is only how that set meets Pwd+L≥129 without changing L.)

### 4.3 LMMM — separate sprOn pixel waits (L stays 66)

sprOn LMMM is exact on its own per-kind plateaus (old **3262/3262**; `nx`
adds 352+357+210+143 pairs, still 100% on those plateaus). It does not
intersect the dispOff/sprOff max plateau on Prs / Pwd+L. **L=66** is inside
both L plateaus, so the sprOn split is on **Prs** (and the break wait Pwd+L),
not L. `nx` also tightens dispOff-alone Prs from [26,32] to **[29,32]**.

| Param | dispOff / sprOff plateau | sprOn plateau | Preferred even (sprOn) |
|-------|--------------------------|---------------|-------------------------|
| Prs | [29,32] | [33,52] | **34** |
| Prd | [21,24] | [9,26] | **24** |
| Pwd | [61,64] | [59,64] | **64** (so Pwd+L=130 with L=66; mid-line 62 also fits) |
| **L** | [64,67] | [65,89] | **66** (same as YMMM / HMMM / LMMM off) |
| Pwd+L | 128 | [129,148] | **130** |

Document **two** LMMM sets (§3.2) because of **Prs**, not because mid-line Pwd
differs. HMMM has **no** analogous mid-line split. openMSX’s sprites-on
Prs 32→48 matches the *direction* of the Prs shift (48∈ sprOn plateau). A
full sprOn match also needs Pwd+L≥129 (here Pwd=64 with L=66), not only a
larger Prs.

Whether real HW has mode-dependent LMMM waits, or the model is incomplete,
remains open.

### 4.4 LMMV end-of-line breaks / labeling

- **LMMV** dest wrap near the line end wants **Pr+L ≥ 133**, not 130.
  Old: one dispOff `8b` `W@1221→R@1361`. `nx` short widths (NX=2,3,6,7) repeat
  the same family many times (`1221→1361`, `1213→1353`, `1269→41`, `1317→89`,
  Δ≈140, feasible [133,140]). dispOff `nx` is 351/392 at Pr+L=130 vs 370/392
  at [133,136]. Still ignored for the preferred pick: L=58 (shared with HMMV)
  ⇒ Pr+L=130. Using 133 would mean L=61 with Pr=72, which leaves the even /
  HMMV.L heuristic.
- **YMMM/LMMV b64:** page-cross with `|Δ|==1` should count as line break.
- Two dispOff HMMV `nx2` P+L misses: `25→173` (break across the refresh hole,
  skipped 165) and `485→597` (feasible ≥105, so P+L=104 takes 589).

---

## 5. Method (one paragraph)

For each scored transition `(t0→t1)`, collect waits `w` with `next_slot(t0+w)=t1`. Per transition class, the plateau is the `w` set maximizing exact matches. Combined plateaus maximize the sum of matches over dispOff + sprOff + sprOn (each with its corrected slot table). Original and `nx` files are pooled unless a table splits them. First and last capture columns are trimmed. LMMM `R..` in `nx` files is labeled from the `R,R,W` sequence.
