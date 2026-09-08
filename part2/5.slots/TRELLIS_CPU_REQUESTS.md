# Reconstructing CPU VRAM requests with a trellis

This document describes the `fit_2026.cc --trellis` analysis and the
`.cpureq` files it produces. It is methodology, not part of the VDP behaviour
specification in `VDP_VRAM_TIMING.md`.

The implementation and this document were written with assistance from an AI
coding agent.

## 1. What problem the trellis solves

The logic analyzer records:

* rising `/CSR` and `/CSW` edges, at 80 MHz;
* the VDP's VRAM accesses; and
* refresh cycles used to map analyzer samples onto the 21.477 MHz VDP clock.

The arbiter, however, acts on an **integer VDP cycle** `T`. The analyzer does
not directly record that integer. Its sampling quantisation, the fitted clock
map, input setup time, and the asynchronous analyzer/VDP clocks leave some
edges close enough to a VDP-cycle boundary that either adjacent integer can be
plausible.

This matters because request attribution is sequential. A request can book a
future CPU slot, be discarded while the one-entry buffer is occupied, or be
discarded by the post-grant threshold. Choosing the wrong integer for one edge
can therefore shift the apparent request/grant association for later edges.
Rounding each edge independently or greedily matching it to a nearby access
does not solve the same problem.

The trellis instead asks:

> Is there a globally consistent assignment of every recorded `/CSx` edge to
> an integer request cycle that, when passed through one fixed arbiter model,
> produces exactly the observed CPU VRAM accesses?

## 2. Observation model

For recorded edge `i`:

```
T_i = floor(t2_i + e_i + phi)
```

where:

* `t2_i` is the measured `/CSx` rising-edge time in VDP cycles;
* `phi` is one real pin-to-arbiter delay shared by every edge;
* `e_i` is bounded measurement uncertainty, `|e_i| <= eps`; and
* `T_i` is the hidden integer cycle on which the arbiter sees the request.

`eps` is increased only through a fixed list of values. The program reports
the smallest value for which an exact reconstruction exists. One analyzer
sample is 0.268 VDP cycles, so a tolerance of about one sample is a measurement
uncertainty, not permission to move requests arbitrarily.

The candidate integers change only when `t2_i + phi ± eps` crosses an integer.
`fit_2026` partitions the real `phi` axis at all such boundaries and tests each
resulting half-open interval. This is an exact finite search over the requested
delay range, not a sampled or gradient fit.

## 3. Hidden state and Viterbi-style dynamic programming

For a fixed `phi` interval and `eps`, each edge has a small set of candidate
integer cycles. The dynamic program processes the edges in time order. Its
state contains the history that can affect later decisions:

* how many observed CPU grants have been matched;
* the CPU slot already booked by a pending request, if any;
* a second latched request only in the diagnostic `--qdepth=2` model; and
* the previously granted CPU slot used by the drop threshold.

A transition chooses a candidate `T_i`, first retires any due grant, and then
applies the arbiter rules. Paths that emit a grant different from the next
observed CPU access are rejected. Among legal paths the cost is the number of
edges moved away from their nominal nearest cycle.

This is usefully described as a **trellis** or **Viterbi-style DP**. It has the
same pattern as decoding a hidden Markov model—observations outside, hidden
states inside, and a best complete path—but it is not a probabilistic HMM:
there are no learned transition probabilities. The transitions are the
deterministic hardware model and `eps` is a bounded observation error.

The capture may start while one real request is already pending. That option
is constrained: its arming cycle must precede the first recorded edge and must
actually map to the first observed slot. It is not a free synthetic grant.
`--prepace=N` constrains it further to the measured periodic request train.

## 4. Why this is a valid analysis

The trellis does **not** add new runtime behaviour. It performs global event
attribution under behaviour specified independently. It is valid because:

1. every candidate request remains within the stated measurement uncertainty;
2. one `phi` is shared by all edges, and can be required to be shared across
   captures;
3. every recorded edge must be explained—taken, legally dropped, or granted
   outside the recorded `.txt` window;
4. every observed CPU access must be reproduced in order;
5. buffer occupancy and drop decisions follow the tested arbiter, not an
   edge-specific rule; and
6. the chosen request list is replayed by a separately written forward
   simulator. The DP and verifier do not share their arbiter implementation.

It would become overfitting if arbitrary per-edge delays, per-event arbiter
parameters, unconstrained initial grants, or knowledge of the desired next
access were added as physical rules. Diagnostic options such as per-row
overrides are therefore hypothesis tests; they are not evidence by themselves
and must be replaced by a circuit-derived class or an independently tested
rule before entering the model of record.

An exact path proves that the capture is **compatible** with the tested model
and error bound. It does not prove that no other physical model could produce
the same observations. Failure is stronger: under the stated decoder,
timebase, error bound, initial-state policy, and arbiter, no legal attribution
exists.

## 5. What improved

The old integer-delay score rounded the request train using one integer offset
per mode and compared predicted and observed accesses. At the FINDINGS7 stage
it made 76 access errors and reconstructed 202 of 266 captures perfectly.

The first trellis version reconstructed 240 of 266 captures exactly while
being stricter: it accounted for every `/CSx` edge and allowed no deadline tie.
That is a gain of 38 complete captures, mostly from correctly representing a
request already pending at capture start and edges near integer boundaries.

Subsequent timebase, pulse-decoder, and hardware-derived arbiter improvements
raised the one-shared-`phi` result to **263 of 266** captures:

```
display off  100 / 100
sprites off   83 / 83
sprites on    80 / 83
```

Allowing a separate `phi` for each capture gives 264 of 266. The change from
202 to 263 must not be credited to Viterbi alone. The trellis provided the
correct strict scoring framework; improved observations and physical rules
supplied part of the later gain. Under the shared model, the remaining three
captures differ by one or two grants near the first displayed-line boundary.

The fitted pin delay is approximately 10.75 VDP cycles. A single shared value
fits the corpus in `[10.746, 10.748)` under the final tested model.

## 6. Running it

From `part2/5.slots`:

```sh
g++ -O3 -std=c++20 -o fit_2026 fit_2026.cc
./fit_2026 --trellis --sel= $(ika9958/subslot_rows.py)
```

`subslot_rows.py` supplies the Memory-PLA sub-slot classes used by the final
hardware-derived `NEED` and threshold formulas. Add `--reqfiles` to write one
`.cpureq` sibling for every capture that has an exact reconstruction:

```sh
./fit_2026 --trellis --reqfiles --sel= $(ika9958/subslot_rows.py)
```

This also updates:

* `trellis-2026.txt`: cache of feasible `phi` intervals, keyed by all tested
  model options; and
* `trellis-requests-2026.txt`: verbose edge-by-edge reconstruction for human
  diagnosis.

These are generated analysis artifacts. Review their diff before publishing;
do not hand-edit them.

## 7. The `.cpureq` format

A `.cpureq` file is a compact test fixture. Its body is the reconstructed
integer request cycles, sorted, in RAS numbering and on the same absolute axis
as the sibling `.txt`:

```
absolute_cycle = line_number * 1368 + row
```

Negative cycles are requests before the `.txt` window. They are intentional.
Requests that were dropped or granted outside the window are also present and
must not be removed. The body records requests, not only successful accesses.

For an alternating `rdwrCpu` capture each cycle has an `r` or `w` suffix.
Other files use the header's single `kind`. The tags identify the pin train;
they do not change arbitration.

The header records the capture, display mode, line length, lookahead, row
lookahead overrides, drop margin, buffer depth, overflow policy, request kind,
and body count. It does not contain VRAM addresses: timing cannot reconstruct
them.

Example:

```
capture scr5-dispOff-hmmm-rdCpu-3
mode dispOff
line 1368
need 16
margin 2
buffer 1
policy drop-new
kind r
requests 115
-88
-16
55
```

The integers are model-derived estimates, not raw logic-analyzer facts. Cite
the sibling `.vcd` as the measurement and the `.cpureq` as its reconstruction
under the arbiter parameters in the header.

## 8. Using `.cpureq` files

To test request files against the observed accesses without opening any
`.vcd`:

```sh
./fit_2026 --fromreq
```

This reads each `.cpureq`, runs the current forward arbiter, and compares the
grants inside the capture window with its sibling `.txt`. It also prints
discarded request margins, which should all lie below the applicable
threshold. A failure can mean either a corrupt fixture or that the arbiter has
changed since the fixture was generated; use the header and repository history
to distinguish them.

To check the other direction—whether request cycles remain plausible for the
recorded `/CSx` edges, without running the arbiter or reading `.txt`:

```sh
./fit_2026 --checkreq
```

This checks count/order and verifies that all `T_i - t2_i` values fit one delay
band of width `1 + 2*tol`. Use `--checkreq --tol=X` only when a different
explicit measurement tolerance is intended.

A downstream emulator test should:

1. use the slot lattice and arbiter parameters represented by the fixture;
2. submit **all** body requests in order, including negative, dropped, and
   out-of-window requests;
3. compare successful grants only inside the sibling `.txt` window; and
4. compare read/write tags only if the tested interface distinguishes them.

The files should not be loaded by openMSX during normal emulation. Their useful
role is as deterministic regression input for an arbiter implementation that
would otherwise need the large VCD corpus and its timebase decoder.

## 9. Current files and format limitation

The 256 checked-in `.cpureq` files were generated with the earlier arbiter in
commit `588b40a`. They predate the final Memory-PLA-class threshold formulation
and stalled-memory-grid distance. Against the current source:

```
--checkreq: 256 / 256 plausible against their /CSx captures
--fromreq:  252 / 256 reproduce their .txt exactly
```

The four replay differences are
`scr5-sprOn-line-wrCpu-2`,
`scr5-sprOn-hmmm-wrCpu-3`,
`scr5-sprOn-ymmm-rdCpu-2`, and
`scr5-sprOn-stop-wrCpu-3`. Thus the checked-in files remain valid
reconstructions of their pin edges, but they are not a fully passing fixture
set for the current arbiter.

The current writer can emit `need_row` but cannot serialize `--threshrow` or
the selected wall-clock versus stalled-memory-grid distance. Therefore, do not
regenerate and publish `.cpureq` files with `subslot_rows.py` and then claim
that their headers are self-contained: `--fromreq` could inherit those missing
settings from its command line or process state.

Before publishing a final hardware-model fixture set, extend the format with,
for example:

```
distance memory
margin_row 28 -1
```

for every threshold class row, teach both writer and reader those keywords,
clear all process-global overrides before each replay, regenerate the files,
and require both `--fromreq` and `--checkreq` to pass. Until then, the existing
files remain useful for the model version they explicitly encode, while
`trellis-requests-2026.txt` plus the exact command line is the authoritative
record of a final-model reconstruction.
