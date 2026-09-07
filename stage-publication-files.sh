#!/bin/sh
# This file was written with assistance from an AI coding agent.
#
# Stage the publication files selected for review. This script does not commit.

set -eu

cd -- "$(dirname -- "$0")"

git add -- .gitignore stage-publication-files.sh

git add -- \
	part2/5.slots/ARBITER_ITER.md \
	part2/5.slots/FINDINGS-ADJUST-S10.md \
	part2/5.slots/FINDINGS.md \
	part2/5.slots/FINDINGS2.md \
	part2/5.slots/FINDINGS3.md \
	part2/5.slots/FINDINGS4.md \
	part2/5.slots/FINDINGS5.md \
	part2/5.slots/FINDINGS6.md \
	part2/5.slots/FINDINGS7.md \
	part2/5.slots/FINDINGS8.md \
	part2/5.slots/IKA9958.md \
	part2/5.slots/VDP_VRAM_TIMING.md

git add -- \
	8.final-analysis/cpu_scratch_2013.cc \
	8.final-analysis/fit_2013.cc \
	part2/5.slots/fit_2026.cc \
	part2/5.slots/fit_cmd_slots.cc

git add -- \
	8.final-analysis/fit_2013_cpu.py \
	8.final-analysis/fit_2013_hmmv_cpu.py \
	part2/1.vcd/lines.py \
	part2/1.vcd/rasmap.py \
	part2/1.vcd/vcdlib.py \
	part2/5.slots/exp4.py \
	part2/5.slots/fit_stop_cpu.py \
	part2/5.slots/retag.py \
	part2/5.slots/ika9958/align.py \
	part2/5.slots/ika9958/class_sweep.py \
	part2/5.slots/ika9958/cone3.py \
	part2/5.slots/ika9958/drop.py \
	part2/5.slots/ika9958/invariant.py \
	part2/5.slots/ika9958/mpla2.py \
	part2/5.slots/ika9958/mpla3.py \
	part2/5.slots/ika9958/mpla4.py \
	part2/5.slots/ika9958/mpla5.py \
	part2/5.slots/ika9958/mpla6.py \
	part2/5.slots/ika9958/netlist2.py \
	part2/5.slots/ika9958/rccsim.py \
	part2/5.slots/ika9958/sim.py \
	part2/5.slots/ika9958/stall.py \
	part2/5.slots/ika9958/subslot.py \
	part2/5.slots/ika9958/subslot_rows.py \
	part2/5.slots/ika9958/sweep.py \
	part2/5.slots/ika9958/trace.py

git add -- part2/5.slots/*.cpureq
git add -- part2/5.slots/trellis-requests-2026.txt

git status --short
