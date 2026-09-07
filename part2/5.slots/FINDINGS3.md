# VDP command slot timing — conclusions

This document was written with assistance from an AI coding agent.

Working summary (2026-08-22). Earlier drafts: `FINDINGS2.md`, `FINDINGS.md` (frozen).

Data: `scr5-{dispOff,sprOff,sprOn}-*-noCpu-*.txt` (including `*nx??-?d.txt`).
Model: after an access at `t`, wait a fixed `W`, then take the first slot ≥ `t+W`.
Line period = 1368. Measurement times = slot table + 1 (see §2 for two EOL exceptions).
Command start `S` is not fitted; traces are mid-command only.

---

## 1. Slot tables

Three operating modes, three slot maps (openMSX `VDPAccessSlots.cc`, then the
measurement offset):

| Mode | Slots / line | Measurement vs canonical |
|------|--------------|--------------------------|
| dispOff (screen off) | 154 | +1 everywhere; **1324/1334 → +2** (obs. **1326/1336**) |
| sprOff (bitmap, sprites off) | 88 | +1 everywhere; **1322/1332 → +2** (obs. **1324/1334**) |
| sprOn (bitmap, sprites on) | 31 | **+1 only** (incl. 1330→1331); no extra shifts |

Fits below use these corrected tables. Sprite count/size does not add command
slots. Screen 5 and 8 share the same bitmap maps.

---

## 2. Wait parameters

Traces constrain the **pre-access wait** (mid-line `P` / `Pr` / … and line-break
`P+L` / `Pr+L` / …). **L** is break − mid. Each integer in a plateau produces
the same next slot on the measured transitions.

The leftover width is mostly **slot equivalence** on these three maps (e.g. every
P in [45,48] is indistinguishable). Extra NX traces shrank some *per-mode* bands
but left the **three-mode intersection** unchanged, and did not change the
preferred evens.

### 2.1 Plateaus (three-mode intersection)

| Command | Pattern | Pixel waits | **L** |
|---------|---------|-------------|-------|
| **HMMV** | `W` | P∈[45,48] | [55,59] |
| **YMMM** | `R.s`↔`W.d` | Pw∈[21,24], Pr∈[37,38] | [65,67] |
| **HMMM** | `R.s`↔`W.d` | Pw∈[21,24], Pr∈[61,64] | [64,67] |
| **LMMV** | `R.d`↔`W.d` | Pw∈[21,24], Pr∈[71,72] | [57,61] |
| **LMMM** dispOff/sprOff | `R.s`→`R.d`→`W.d` | Prs∈[29,32], Prd∈[21,24], Pwd∈[61,64] | [64,67] |
| **LMMM** sprOn | same | Prs∈[33,52], Prd∈[9,26], Pwd∈[59,64] | [65,89] |

HMMM mid-line Pw/Pr is one set for all three modes. LMMM **Prs** has no overlap
between off-modes and sprOn, so LMMM needs two sets. HMMM/LMMM line-break waits
still disagree across modes (§4.2); that is not treated as a second HMMM.Pr.

### 2.2 Heuristics (picking inside a plateau)

Plateaus include odd and even integers. Preferred representatives are **even**,
plus cross-command ties (a possible ÷2 command clock — not required by the data):

1. **Even** values only (for now).
2. **HMMV.L = LMMV.L** (destination-only fills) → only even in [57,59] is **58**.
3. **YMMM.L = HMMM.L = LMMM.L** in all three modes (copy-family line-step) → **66**.
   (Also **YMMM.L ≤ HMMM.L**, which already drops HMMM.L=64.)
4. **YMMM.Pw = HMMM.Pw** (byte copy after src read) → **24**.
5. **LMMV.Pw = LMMM.Prd** (`R.d`→`W.d` RMW) → **24**.
6. **HMMM.Pr = LMMM.Pwd** (`W.d`→`R.s`) → **62** when the break wait is 128.
   LMMM sprOn uses Pwd=**64** only so Pwd+L=130 with the same L=66
   (mid-line Pwd=62 is still a perfect sprOn fit).

**L at a glance:** fills **58**; YMMM / HMMM / LMMM **66** (all modes).

### 2.3 Preferred even representatives

| Command | Preferred | Break wait |
|---------|-----------|------------|
| **HMMV** | P=**46**, L=**58** | 104 |
| **LMMV** | Pw=**24**, Pr=**72**, L=**58** | 130 |
| **YMMM** | Pw=**24**, Pr=**38**, L=**66** | 104 |
| **HMMM** | Pw=**24**, Pr=**62**, L=**66** | 128 |
| **LMMM** dispOff/sprOff | Prs=**32**, Prd=**24**, Pwd=**62**, L=**66** | 128 |
| **LMMM** sprOn | Prs=**34**, Prd=**24**, Pwd=**64**, L=**66** | 130 |

L is mode-independent on this set. Pixel waits are mode-independent where the
model fits (HMMV, HMMM mid-line, YMMM, LMMV). LMMM.Prs (and the sprOn LMMM
*break* pick Pwd=64) still need a sprOn vs off split. Slot layout is always
mode-dependent.

---

## 3. Open issues

### 3.1 Refresh gap (HMMM Pr / LMMM Pwd)

When `t+60` is a slot at or after the large hole, hardware takes that slot
(Δ=60). It is not “always the first post-hole slot”:

| Mode | Write `t` | Next access | `t+60` |
|------|-----------|-------------|--------|
| dispOff | 105 | 165 | 165 (first after hole) |
| dispOff | 113 | 173 | 173 (skips 165) |
| dispOff | 121 | 181 | 181 (skips 165 and 173) |
| sprOff | 103 | 163 | 163 |
| sprOff | 111 | 171 | 171 |
| sprOff | 1207 | 1267 | 1267 |

Preferred Pr/Pwd=**62** predicts the *next* slot after `t+60` when `t+60` is
itself a slot. Drop the Δ=60 cases and [61,64] is exact. Do not move Pr to 60:
that misses bulk mid-line on dispOff. No single value fits both.

### 3.2 Line-break wait: sprOn vs off-modes (HMMM, LMMM)

| | Pr+L / Pwd+L = 128 | = 129 |
|--|-------------------|-------|
| dispOff + sprOff | near-perfect | poor |
| sprOn | near-zero | perfect |

Keep one HMMM tuple (Pr=62, L=66). Do not invent a sprOn HMMM.Pr: mid-line Pr=62
is already exact on sprOn. LMMM already has a sprOn set because of Prs; Pwd=64
there is only how that set reaches Pwd+L≥129 without changing L.

### 3.3 LMMM Prs is mode-dependent

Off-modes [29,32] vs sprOn [33,52] — no overlap at the maximum. Prd and mid-line
Pwd *do* overlap. Whether HW really has a larger sprites-on Prs, or the model is
incomplete, is open. openMSX’s 32→48 move is in the right direction (48∈ sprOn
plateau) but a full match also needs Pwd+L≥129.

### 3.4 LMMV end-of-line breaks

Dest wrap near the line end often wants Pr+L≥133 (`W@1221→R@1361` and the same
pattern at short NX). Preferred **130** (L=58, shared with HMMV) misses those.
Pr+L=133 would be L=61 with Pr=72 — drops the even / HMMV.L heuristic. Parked.

---

## 4. Method

For each transition `(t0→t1)`, the feasible waits are those `w` with
`next_slot(t0+w)=t1`. The plateau is the `w` set maximizing exact matches.
Combined plateaus maximize the sum over the three modes (each with its slot
table). First and last capture columns are trimmed. Line-break vs mid-line is
labeled from dest/src address (`//0x80`; `|Δ|≤1` is mid-line). LMMM `R..` tags
are assigned `R.s`/`R.d` from the `R,R,W` cadence.
