// 2026 NMS 8280 CPU slot model (stop traces and HMMV+CPU).
// This file was written with assistance from an AI coding agent.
//
//   g++ -O3 -std=c++20 -o fit_2026 fit_2026.cc
//   ./fit_2026 [slots-dir]                 # all rdCpu/wrCpu (stop + commands)
//   ./fit_2026 --cmd [stop|hmmv|...|all]
//   ./fit_2026 --hmmv [slots-dir]
//   ./fit_2026 --wiggle [slots-dir]
//   ./fit_2026 --nocpu [slots-dir]         # command engine, no CPU occupancy
//   ./fit_2026 --hyp-search [name] [dir]   # δ × NEED search, pending-CPU blocks cmd
//   ./fit_2026 --idle-rdd [slots-dir]      # treat R.. as idle; pending packed +6
//   ./fit_2026 --scratch [slots-dir]       # txt-only: occupancy + oracle engine
//   ./fit_2026 --mismatch [slots-dir]      # packed leftovers vs CPU T-window
//   ./fit_2026 --origin [slots-dir]        # .txt vs .vcd scan-line origin
//   ./fit_2026 --rw [--sel=SUB] [dir]      # independent /CSR and /CSW δ
//   ./fit_2026 --rw --force=D [--sel=SUB]  # mismatch rows at a fixed δ
//   ./fit_2026 --pad3[=E] / --need=N       # sprites-on padding / lookahead
//   ./fit_2026 --packedneed=N               # packed continuation deadline
//   ./fit_2026 --rawthreshdist              # diagnostic: do not remove RCC stalls
//
// VDP arbiter: FINDINGS7. Request at T, D16 with engine-distance,
// drop-new while occupied (keep scheduled slot), 2-cycle holdoff after RAS,
// CPU never uses packed +6. Command: FINDINGS4 waits, sprites-on addend +1,
// padding +4 in every mode (sprites-on: 1330 costs 2, 1337/1348 one each),
// packed-start +1 unconditional. Mixed: skip CPU RAS and dummy R...
// Dummy R.. : best integer approximation is packed NEED=19 after the request
// missed the run-start slot at C-6. CI WAITING suggests NEED=18, but the
// command/address ownership path and exact boundary are absent from IKA9958.
//
// CPU model: /CSR or /CSW edges (both, for the interleaved rdwrCpu captures)
// from the matching .vcd, converted onto the
// 3.time VDP clock (refresh interpolation), then T = floor(t2) + δ.
// Edges closer than CSX_MIN_GAP (20) VDP cycles are dropped (analyzer ringing
// at the ~15-cycle pulse width). Search δ and falling vs rising.
//
// Scoring: if first_slot(T-1) or first_slot(T+1) differs from first_slot(T),
// either slot is a legal D16-tie outcome (async sample). That is not an
// oracle: the sim still emits first_slot(T).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <set>
#include <climits>

namespace fs = std::filesystem;

constexpr int LINE = 1368;
int NEED = 16; // --need=N, to trade lookahead against δ (FINDINGS7 §8.2)
int PACKED_NEED = 19; // --packedneed=18 tests the CI continuation hypothesis
constexpr int BUSY = 2;
constexpr int CSX_MIN_GAP = 20; // VDP cycles; drop ringing at ~15–17, keep first-I/O ~23+
constexpr int CSX_MIN_SAMPLES = 8; // drop 1-sample spikes; real I/O is ~56 samples
bool CS_DUMP = false;              // --csdump: show how /CSx edges survive the filters
// libsigrok sampled 16 channels at 80 MHz, so one analyzer sample is:
constexpr double VDP_PER_SAMPLE = 21.477270 / 80.0;
// VCD timestamps are in the $timescale of 100 ps, not in samples:
constexpr double VDP_PER_UNIT = 21.477270e6 * 1e-10;
bool ANCHOR_FIX = true;               // --noanchorfix: time the refresh index by counting
constexpr int ANCHOR_MAX_STEP = 24;   // at most 3 lines of missed refreshes
// VCD timestamps are in $timescale units, so a pulse width in samples has to be
// scaled before it can be compared with one. Without this CSX_MIN_SAMPLES is
// read as 0.8 ns and the filter never fires, so analyzer spikes survive as
// requests the CPU never made.
constexpr int UNITS_PER_SAMPLE = 125; // 12.5 ns at 100 ps
bool PULSE_FIX = true;                // --nopulsefix: compare against 0.8 ns, i.e. never fire
int PRE_PACE = 0;                     // --prepace=K: K pre-capture requests at the loop pace
bool WRITE_REQ = false;               // --reqfiles: write the .cpureq sibling files
bool FAIL_DUMP = false;               // --faildump: attribute each miss to a discarded request
std::string ONLY;                     // --only=SUB: restrict --faildiag to matching captures
// Engine-grid cycles by which a request must clear the slot granted to the
// request before it, per mode (dispOff, sprOff, sprOn). The whole drop rule is
// that one inequality. Row overrides below translate the gate-derived
// sub-slot classes; signed_engine_dist() removes intervening RCC stalls.
int THRESH_MODE[3] = {BUSY, BUSY, -1}; // --thresh=N or --thresh=a,b,c
// Diagnostic only (--threshrow=ROW:N): give one row of the lattice its own
// threshold, to ask whether a capture that cannot be reconstructed wants
// something the rest of the corpus contradicts, or only something it has never
// had the chance to observe. Not written to the .cpureq files.
std::map<int, int> THRESH_ROW;
int ACC_THRESH = BUSY;                 // the entry for the capture in hand
bool THRESH_ENGINE_DIST = true;         // threshold is on the stalled phiL grid
int ACC_MODE_INDEX = 0;

// The threshold that applies to a request whose predecessor was granted sprev.
static int mod_line(int t);
static int thresh_at(int sprev)
{
	if (THRESH_ROW.empty()) return ACC_THRESH;
	auto it = THRESH_ROW.find(mod_line(sprev));
	return it == THRESH_ROW.end() ? ACC_THRESH : it->second;
}
constexpr int HMMV_P = 46;
constexpr int HMMV_NL = 104;
constexpr int LMMV_PW = 24;
constexpr int LMMV_PR = 72;
constexpr int LMMV_NL = 130;
constexpr int YMMM_PW = 24;
constexpr int YMMM_PR = 36;
constexpr int YMMM_NL = 104;
constexpr int HMMM_PW = 24;
constexpr int HMMM_PR = 60;
constexpr int HMMM_NL = 128;
constexpr int LMMM_PRS = 32;
constexpr int LMMM_PRD = 24;
constexpr int LMMM_PWD = 60;
constexpr int LMMM_NL = 128;
constexpr int LINE_PW = 24;
constexpr int LINE_PR = 84;
constexpr int LINE_NL = 120;
constexpr int VRAM_PITCH = 128; // screen 5 bytes per display line
constexpr int DELTA_LO = 0;
constexpr int DELTA_HI = 40; // inclusive
constexpr int REFRESH_ROW[] = {285, 413, 541, 669, 797, 925, 1053, 1181};

enum class Mode { DispOff, SprOff, SprOn };

enum class Cmd { Hmmv, Lmmv, Ymmm, Hmmm, Lmmm, Line, Unknown };

enum class Variant { Wide, B64, C4 };

constexpr int SLOTS_DISPOFF[] = {
	0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120,
	164, 172, 180, 188, 196, 204, 212, 220, 228, 236, 244, 252, 260, 268,
	276, 292, 300, 308, 316, 324, 332, 340, 348, 356, 364, 372, 380, 388,
	396, 404, 420, 428, 436, 444, 452, 460, 468, 476, 484, 492, 500, 508,
	516, 524, 532, 548, 556, 564, 572, 580, 588, 596, 604, 612, 620, 628,
	636, 644, 652, 660, 676, 684, 692, 700, 708, 716, 724, 732, 740, 748,
	756, 764, 772, 780, 788, 804, 812, 820, 828, 836, 844, 852, 860, 868,
	876, 884, 892, 900, 908, 916, 932, 940, 948, 956, 964, 972, 980, 988,
	996, 1004, 1012, 1020, 1028, 1036, 1044, 1060, 1068, 1076, 1084, 1092,
	1100, 1108, 1116, 1124, 1132, 1140, 1148, 1156, 1164, 1172, 1188, 1196,
	1204, 1212, 1220, 1228, 1268, 1276, 1284, 1292, 1300, 1308, 1316, 1324,
	1334, 1344, 1352, 1360,
};
constexpr int SLOTS_SPROFF[] = {
	6, 14, 22, 30, 38, 46, 54, 62, 70, 78, 86, 94, 102, 110, 118, 162,
	170, 182, 188, 214, 220, 246, 252, 278, 310, 316, 342, 348, 374, 380,
	406, 438, 444, 470, 476, 502, 508, 534, 566, 572, 598, 604, 630, 636,
	662, 694, 700, 726, 732, 758, 764, 790, 822, 828, 854, 860, 886, 892,
	918, 950, 956, 982, 988, 1014, 1020, 1046, 1078, 1084, 1110, 1116,
	1142, 1148, 1174, 1206, 1212, 1266, 1274, 1282, 1290, 1298, 1306, 1314,
	1322, 1332, 1342, 1350, 1358, 1366,
};
constexpr int SLOTS_SPRON[] = {
	28, 92, 162, 170, 188, 220, 252, 316, 348, 380, 444, 476, 508, 572,
	604, 636, 700, 732, 764, 828, 860, 892, 956, 988, 1020, 1084, 1116,
	1148, 1212, 1264, 1330,
};

struct Pad {
	int c;
	int extra;
};

struct SlotTable {
	const int* data;
	int n;
	Pad pad[4];
	int npad;
};

SlotTable CMD_TABLE[] = {
	{SLOTS_DISPOFF, int(std::size(SLOTS_DISPOFF)), {{1334, 2}, {1344, 2}}, 2},
	{SLOTS_SPROFF,  int(std::size(SLOTS_SPROFF)),  {{1332, 2}, {1342, 2}}, 2},
	{SLOTS_SPRON,   int(std::size(SLOTS_SPRON)),   {{1330, 2}, {1337, 1}, {1348, 1}}, 3},
};

// --pad3 restores the old sprites-on padding (+1/+1/+1 = 3, addend 2). The
// natural sprites-on gap at 1315→1330 is 13, not the 14 of the 1365-cycle
// line, so the padded cycle completing at 1330 costs 2 and the total is +4
// like the other two modes; the per-step addend is then 1 (FINDINGS7 §3).
int SPR_ADDEND = 1;

// --padsil: replace the fitted padding with the derived clock stall.
bool PAD_SILICON = false;
int PAD_SHIFT = 0;
int PAD_PAIR = 0;

static int spr_addend(Mode m) { return m == Mode::SprOn ? SPR_ADDEND : 0; }

static int mode_index(Mode m) { return int(m); }

static Mode mode_of(std::string_view name)
{
	if (name.find("dispOff") != std::string_view::npos) return Mode::DispOff;
	if (name.find("sprOff") != std::string_view::npos) return Mode::SprOff;
	if (name.find("sprOn") != std::string_view::npos) return Mode::SprOn;
	throw std::runtime_error(std::string(name) + ": unknown mode");
}

static const char* mode_name(Mode m)
{
	switch (m) {
	case Mode::DispOff: return "dispOff";
	case Mode::SprOff:  return "sprOff";
	case Mode::SprOn:   return "sprOn";
	}
	return "?";
}

static Cmd cmd_of(std::string_view name)
{
	if (name.find("-hmmv-") != std::string_view::npos) return Cmd::Hmmv;
	if (name.find("-lmmv-") != std::string_view::npos) return Cmd::Lmmv;
	if (name.find("-ymmm-") != std::string_view::npos) return Cmd::Ymmm;
	if (name.find("-hmmm-") != std::string_view::npos) return Cmd::Hmmm;
	if (name.find("-lmmm-") != std::string_view::npos) return Cmd::Lmmm;
	if (name.find("-line-") != std::string_view::npos) return Cmd::Line;
	return Cmd::Unknown;
}

static const char* cmd_name(Cmd c)
{
	switch (c) {
	case Cmd::Hmmv: return "hmmv";
	case Cmd::Lmmv: return "lmmv";
	case Cmd::Ymmm: return "ymmm";
	case Cmd::Hmmm: return "hmmm";
	case Cmd::Lmmm: return "lmmm";
	case Cmd::Line: return "line";
	case Cmd::Unknown: return "?";
	}
	return "?";
}

static Variant parse_variant(std::string_view name)
{
	auto pos = name.find("noCpu-");
	if (pos == std::string_view::npos) return Variant::Wide;
	char suf = 0;
	for (size_t i = pos + 6; i < name.size(); ++i) {
		if (name[i] == 'b' || name[i] == 'c') {
			suf = name[i];
			break;
		}
		if (name[i] == '.') break;
	}
	if (suf == 'b') return Variant::B64;
	if (suf == 'c') return Variant::C4;
	return Variant::Wide;
}

static bool is_line_break(int a0, int a1, Variant var, bool nybble)
{
	int d = a1 - a0;
	bool page = (a0 / VRAM_PITCH) != (a1 / VRAM_PITCH);
	switch (var) {
	case Variant::Wide:
		return page;
	case Variant::B64:
		if (d == 97) return true;
		return nybble ? (page && std::abs(d) != 1) : page;
	case Variant::C4:
		if (d == 127) return true;
		return nybble ? (page && std::abs(d) != 1) : page;
	}
	return false;
}

static bool is_nybble_cmd(Cmd cmd)
{
	return cmd == Cmd::Lmmv || cmd == Cmd::Lmmm || cmd == Cmd::Line;
}

static bool stretch_cas(Mode m, int row)
{
	if (m == Mode::DispOff) return row == 1326 || row == 1336;
	if (m == Mode::SprOff) return row == 1324 || row == 1334;
	return false;
}

static int cas_to_ras(Mode m, int t)
{
	int row = t % LINE;
	if (row < 0) row += LINE;
	return t - 1 - (stretch_cas(m, row) ? 1 : 0);
}

static int mod_line(int t)
{
	int r = t % LINE;
	return r < 0 ? r + LINE : r;
}

static int floor_div(int a, int b)
{
	int q = a / b;
	int r = a % b;
	if (r != 0 && ((a < 0) != (b < 0))) --q;
	return q;
}

static int pad_sub(int t, int s, const SlotTable& tab)
{
	if (s <= t) return 0;
	int n = 0;
	for (int i = 0; i < tab.npad; ++i) {
		int c = tab.pad[i].c;
		int kmin = floor_div(t - c, LINE) + 1;
		int kmax = floor_div(s - c, LINE);
		if (kmax >= kmin) n += (kmax - kmin + 1) * tab.pad[i].extra;
	}
	return n;
}

static int engine_dist(int t, int s, const SlotTable& tab)
{
	return s - t - pad_sub(t, s, tab);
}

static int signed_engine_dist(int from, int to, const SlotTable& tab)
{
	return to >= from ? engine_dist(from, to, tab) : -engine_dist(to, from, tab);
}

static std::vector<int> packed_slots_of(const SlotTable& tab)
{
	std::vector<int> out;
	int prev = -999;
	for (int i = 0; i < tab.n; ++i) {
		int s = tab.data[i];
		if (prev >= 0 && s - prev == 6) out.push_back(s);
		prev = s;
	}
	return out;
}

static bool is_packed_ras(int ras)
{
	static const auto packed = [] {
		auto v = packed_slots_of(CMD_TABLE[1]);
		return std::unordered_set<int>(v.begin(), v.end());
	}();
	return packed.count(mod_line(ras));
}

// LMMM dest-read keeps 32 so P5 can distinguish overflow vs idle.
static bool is_p5_delta(int d) { return d == LMMM_PRS; }
// Packed-start +1 turns FINDINGS4 newline 128 into 129.
static bool is_nl_delta(int d) { return d == HMMM_NL || d == HMMM_NL + 1; }

static int cpu_snext(const std::vector<int>& obs, int last)
{
	auto it = std::upper_bound(obs.begin(), obs.end(), last);
	return (it == obs.end()) ? -1 : *it;
}

static bool skip_packed_for_cpu(Mode mode, int last, int cand, int delta, int Snext)
{
	if (mode != Mode::SprOff) return false;
	if (!is_packed_ras(last) || !is_packed_ras(cand)) return false;
	if (is_p5_delta(delta) && Snext > cand) return true;
	if (is_nl_delta(delta) && Snext > last && Snext <= cand) return true;
	return false;
}

// CPU RAS, plus the best deterministic approximation of packed +6 cycles
// suppressed by a request that missed the run-start grant. CI WAITING gives a
// NEED=18 candidate; reconstructed integer T scores marginally better at 19.
static std::unordered_set<int> occupy_for_command(
	const std::vector<std::pair<int, int>>& ts, Mode mode, int need = NEED)
{
	(void)need;
	std::unordered_set<int> occ;
	auto packed = packed_slots_of(CMD_TABLE[mode_index(mode)]);
	for (auto [T, S] : ts) {
		occ.insert(S);
		if (packed.empty()) continue;
		int base = S - mod_line(S);
		for (int wrap = -1; wrap <= 1; ++wrap) {
			for (int r : packed) {
				int C = base + wrap * LINE + r;
				int lead = S - C;
				int d = T - C;
				if ((lead == 26 || lead == 54) &&
				    d > -(NEED + 6) && d <= -PACKED_NEED)
					occ.insert(C);
			}
		}
	}
	return occ;
}

// Command blocked on every command slot C while a CPU request is pending
// (T <= C < S), even if that request has not yet met D16 for C.
static std::unordered_set<int> occupy_pending_window(
	const std::vector<std::pair<int, int>>& ts, Mode mode)
{
	std::unordered_set<int> occ;
	const SlotTable& tab = CMD_TABLE[mode_index(mode)];
	for (auto [T, S] : ts) {
		occ.insert(S);
		int base0 = T - mod_line(T);
		int base1 = S - mod_line(S);
		for (int base = base0; base <= base1; base += LINE) {
			for (int i = 0; i < tab.n; ++i) {
				int C = base + tab.data[i];
				if (T <= C && C < S) occ.insert(C);
			}
		}
	}
	return occ;
}

static std::vector<int> cpu_slots_of(const SlotTable& tab)
{
	std::vector<int> out;
	out.reserve(tab.n);
	int prev = -999;
	for (int i = 0; i < tab.n; ++i) {
		int s = tab.data[i];
		if (prev < 0 || s - prev != 6) out.push_back(s);
		prev = s;
	}
	return out;
}

// --needrow=r,r,..:N gives those slot rows a lookahead of their own, to test
// whether the deadline is a property of the arbiter or of the slot.
std::map<int, int> NEED_ROW;

static std::vector<int> make_wait(const std::vector<int>& slots, const SlotTable& tab, int need)
{
	std::vector<int> ext;
	ext.reserve(slots.size() * 3);
	for (int k = 0; k < 3; ++k) {
		for (int s : slots) ext.push_back(s + k * LINE);
	}
	std::vector<int> wait(LINE, 0);
	for (int t = 0; t < LINE; ++t) {
		bool found = false;
		for (int s : ext) {
			if (s < t) continue;
			auto it = NEED_ROW.find(mod_line(s));
			if (engine_dist(t, s, tab) >= (it == NEED_ROW.end() ? need : it->second)) {
				wait[t] = s - t;
				found = true;
				break;
			}
		}
		if (!found) throw std::runtime_error("no slot in wait LUT");
	}
	return wait;
}

static int first_slot(const std::vector<int>& wait, int t)
{
	return t + wait[mod_line(t)];
}

static int next_cmd_slot(
	int last, int delta, const SlotTable& tab,
	const std::unordered_set<int>& occupied)
{
	int base = last - mod_line(last);
	for (int wrap = 0; wrap < 8; ++wrap) {
		for (int i = 0; i < tab.n; ++i) {
			int s = base + wrap * LINE + tab.data[i];
			if (s <= last) continue;
			if (engine_dist(last, s, tab) < delta) {
				continue;
			}
			if (occupied.count(s)) continue;
			return s;
		}
	}
	throw std::runtime_error("no command slot");
}

struct EngAcc {
	int ras = 0;
	int addr = 0;
	char kind = 0; // 's' R.s, 'd' R.d, 'w' W.d
};

struct HmmvPred {
	std::vector<int> pred;
	int skips = 0; // candidate slots skipped because CPU occupied
	int unknown = 0;
};

static int prev_kind_addr(const std::vector<EngAcc>& e, int k, char kind)
{
	for (int j = k - 1; j >= 0; --j) {
		if (e[j].kind == kind) return e[j].addr;
	}
	return -1;
}

static bool read_line_break(const std::vector<EngAcc>& e, int k, char kind, Variant var, Cmd cmd)
{
	int prev = prev_kind_addr(e, k, kind);
	if (prev < 0) return false;
	return is_line_break(prev, e[k].addr & 0x1FFFF, var, is_nybble_cmd(cmd));
}

struct StepWait {
	int delta = -1;
	int alt_nl = -1; // set when wrap cannot be classified from addresses
};

static StepWait step_wait(Cmd cmd, Mode mode, const std::vector<EngAcc>& e, int k, int last_ras,
			  Variant var)
{
	StepWait out;
	int spr = spr_addend(mode);
	char a = e[k - 1].kind, b = e[k].kind;
	bool nyb = is_nybble_cmd(cmd);
	auto finish = [&]() -> StepWait {
		// Packed-start +1 (FINDINGS7 §6). Unconditional: the nx4
		// experiment (§10.3) shows it also covers LMMM dest-read
		// Δ=32, which used to be excluded here.
		if (mode == Mode::SprOff && is_packed_ras(last_ras) && out.delta >= 0) {
			out.delta += 1;
			if (out.alt_nl >= 0) out.alt_nl += 1;
		}
		return out;
	};
	switch (cmd) {
	case Cmd::Hmmv:
		if (a == 'w' && b == 'w') {
			int a0 = e[k - 1].addr & 0x1FFFF, a1 = e[k].addr & 0x1FFFF;
			out.delta = (is_line_break(a0, a1, var, false) ? HMMV_NL : HMMV_P) + spr;
			return finish();
		}
		break;
	case Cmd::Lmmv:
		if (a == 'd' && b == 'w') {
			out.delta = LMMV_PW + spr;
			return finish();
		}
		if (a == 'w' && b == 'd') {
			int a0 = e[k - 1].addr & 0x1FFFF, a1 = e[k].addr & 0x1FFFF;
			out.delta = (is_line_break(a0, a1, var, true) ? LMMV_NL : LMMV_PR) + spr;
			return finish();
		}
		break;
	case Cmd::Ymmm:
		if (a == 's' && b == 'w') {
			out.delta = YMMM_PW + spr;
			return finish();
		}
		if (a == 'w' && b == 's') {
			int prev = prev_kind_addr(e, k, 's');
			int mid = YMMM_PR + spr;
			int nl = YMMM_NL + spr;
			if (prev < 0) {
				out.delta = mid;
				out.alt_nl = nl;
			} else {
				out.delta = is_line_break(prev, e[k].addr & 0x1FFFF, var, false) ? nl : mid;
			}
			return finish();
		}
		break;
	case Cmd::Hmmm:
		if (a == 's' && b == 'w') {
			out.delta = HMMM_PW + spr;
			return finish();
		}
		if (a == 'w' && b == 's') {
			int prev = prev_kind_addr(e, k, 's');
			int mid = HMMM_PR + spr;
			int nl = HMMM_NL + spr;
			if (prev < 0) {
				out.delta = mid;
				out.alt_nl = nl;
			} else {
				bool br = is_line_break(prev, e[k].addr & 0x1FFFF, var, false);
				out.delta = br ? nl : mid;
			}
			return finish();
		}
		break;
	case Cmd::Lmmm:
		if (a == 's' && b == 'd') {
			out.delta = LMMM_PRS + spr;
			return finish();
		}
		if (a == 'd' && b == 'w') {
			out.delta = LMMM_PRD + spr;
			return finish();
		}
		if (a == 'w' && b == 's') {
			int prev = prev_kind_addr(e, k, 's');
			int mid = LMMM_PWD + spr;
			int nl = LMMM_NL + spr;
			if (prev < 0) {
				out.delta = mid;
				out.alt_nl = nl;
			} else {
				bool br = is_line_break(prev, e[k].addr & 0x1FFFF, var, nyb);
				out.delta = br ? nl : mid;
			}
			return finish();
		}
		break;
	case Cmd::Line:
		if ((a == 's' || a == 'd') && b == 'w') {
			out.delta = LINE_PW + spr;
			return finish();
		}
		if (a == 'w' && (b == 's' || b == 'd')) {
			int a0 = e[k - 1].addr & 0x1FFFF, a1 = e[k].addr & 0x1FFFF;
			out.delta = (is_line_break(a0, a1, var, true) ? LINE_NL : LINE_PR) + spr;
			return finish();
		}
		break;
	case Cmd::Unknown:
		break;
	}
	return finish();
}

static int pick_delta(const StepWait& w, int last, const SlotTable& tab,
		      const std::unordered_set<int>& occupied, int obs_next)
{
	if (w.delta < 0) return -1;
	if (w.alt_nl < 0 || obs_next < 0) return w.delta;
	int sm = next_cmd_slot(last, w.delta, tab, occupied);
	int sn = next_cmd_slot(last, w.alt_nl, tab, occupied);
	if (sn == obs_next && sm != obs_next) return w.alt_nl;
	return w.delta;
}

static int step_delta(Cmd cmd, Mode mode, const std::vector<EngAcc>& e, int k, int last_ras,
		     Variant var)
{
	return step_wait(cmd, mode, e, k, last_ras, var).delta;
}

static HmmvPred predict_engine(
	const std::vector<EngAcc>& eng, Mode mode, Cmd cmd,
	const std::unordered_set<int>& occupied, Variant var = Variant::Wide,
	bool from_observed = false, const std::vector<int>* cpu_obs = nullptr)
{
	HmmvPred out;
	if (eng.empty()) return out;
	const SlotTable& tab = CMD_TABLE[mode_index(mode)];
	out.pred.reserve(eng.size());
	out.pred.push_back(eng.front().ras);
	int last = eng.front().ras;
	for (size_t k = 1; k < eng.size(); ++k) {
		auto w = step_wait(cmd, mode, eng, int(k), last, var);
		int delta = pick_delta(w, last, tab, occupied, eng[k].ras);
		if (delta < 0) {
			++out.unknown;
			delta = 24 + ((mode == Mode::SprOn) ? 1 : 0);
		}
		std::unordered_set<int> occ = occupied;
		int free = next_cmd_slot(last, delta, tab, {});
		int cand = next_cmd_slot(last, delta, tab, occ);
		if (cpu_obs) {
			int Snext = cpu_snext(*cpu_obs, last);
			if (skip_packed_for_cpu(mode, last, cand, delta, Snext)) occ.insert(cand);
		}
		int s = next_cmd_slot(last, delta, tab, occ);
		if (s != free) ++out.skips;
		out.pred.push_back(s);
		last = from_observed ? eng[k].ras : s;
	}
	return out;
}

struct Serv {
	int S = 0;  // scheduled slot (from T, NEED=16)
	int T = 0;  // arming post
	int Sm = 0; // first_slot(T-1)
	int Sp = 0; // first_slot(T+1)
};

struct CpuSim {
	std::vector<Serv> pred;
	int overwrites = 0;
	int holdoff_dt0 = 0; // dropped posts with T == last RAS
	int holdoff_dt1 = 0; // dropped posts with T == last RAS + 1
};

// holdoff_lo: ignore a post with holdoff_lo <= (T - last_s) < BUSY.
// 0 = 2013 rule [S, S+2); 1 = only S+1; BUSY = off.
static CpuSim vdp_cpu_posts(
	const std::vector<int>& posts, const std::vector<int>& wait, int tmin, int tmax,
	int holdoff_lo = 0)
{
	CpuSim out;
	bool occupied = false;
	std::optional<int> sched;
	std::optional<int> last_s;
	std::optional<int> arm_T;
	size_t i = 0;
	const size_t n = posts.size();
	while (i < n || occupied) {
		std::optional<int> t_req;
		if (i < n) t_req = posts[i];
		if (!occupied && last_s && t_req) {
			int dt = *t_req - *last_s;
			if (dt >= holdoff_lo && dt < BUSY) {
				++out.overwrites;
				if (dt == 0) ++out.holdoff_dt0;
				else if (dt == 1) ++out.holdoff_dt1;
				++i;
				continue;
			}
		}
		if (occupied && sched && (!t_req || *sched <= *t_req)) {
			if (tmin <= *sched && *sched <= tmax) {
				int T = arm_T ? *arm_T : *sched;
				out.pred.push_back({
					*sched, T,
					first_slot(wait, T - 1),
					first_slot(wait, T + 1),
				});
			}
			last_s = sched;
			occupied = false;
			sched.reset();
			arm_T.reset();
			continue;
		}
		if (!t_req) break;
		if (!occupied) {
			occupied = true;
			sched = first_slot(wait, *t_req);
			arm_T = t_req;
		} else {
			++out.overwrites;
		}
		++i;
	}
	return out;
}

struct Score {
	int hit = 0;
	int extra = 0;
	int miss = 0;
	int ties = 0; // subset of hit: obs was first_slot(T±1), not T
};

struct Align {
	enum Kind { Hit, Tie, Extra, Miss } kind;
	int t = 0;      // obs time for hit/tie/miss, pred time for extra
	int t_other = 0;
};

static Score score_times(const std::vector<int>& pred, const std::vector<int>& obs)
{
	Score s;
	size_t i = 0, j = 0;
	while (i < pred.size() && j < obs.size()) {
		if (pred[i] == obs[j]) {
			++s.hit;
			++i;
			++j;
		} else if (pred[i] < obs[j]) {
			++i;
		} else {
			++j;
		}
	}
	s.extra = int(pred.size()) - s.hit;
	s.miss = int(obs.size()) - s.hit;
	return s;
}

// Exact slot match (no D16 tie).
static Score score_exact(const std::vector<Serv>& pred, const std::vector<int>& obs)
{
	std::vector<int> t;
	t.reserve(pred.size());
	for (auto& p : pred) t.push_back(p.S);
	return score_times(t, obs);
}

// D16-tie match: if first_slot(T-1) != first_slot(T) (or T+1), either slot is legal.
static std::vector<Align> align_d16(const std::vector<Serv>& pred, const std::vector<int>& obs)
{
	std::vector<Align> ev;
	size_t i = 0, j = 0;
	while (i < pred.size() && j < obs.size()) {
		const Serv& p = pred[i];
		int o = obs[j];
		if (p.S == o) {
			ev.push_back({Align::Hit, o, p.S});
			++i;
			++j;
		} else if (o < p.S && p.Sm != p.S && o == p.Sm) {
			ev.push_back({Align::Tie, o, p.S});
			++i;
			++j;
		} else if (p.S < o && p.Sp != p.S && o == p.Sp) {
			ev.push_back({Align::Tie, o, p.S});
			++i;
			++j;
		} else if (p.S < o) {
			ev.push_back({Align::Extra, p.S, o});
			++i;
		} else {
			ev.push_back({Align::Miss, o, p.S});
			++j;
		}
	}
	while (i < pred.size()) {
		ev.push_back({Align::Extra, pred[i].S, 0});
		++i;
	}
	while (j < obs.size()) {
		ev.push_back({Align::Miss, obs[j], 0});
		++j;
	}
	return ev;
}

static Score score_d16(
	const std::vector<Align>& ev, int tmin, int tmax, int start_g, int end_g)
{
	Score s;
	int lo = tmin + start_g;
	int hi = tmax - end_g;
	for (auto& e : ev) {
		bool edge = e.t < lo || e.t > hi;
		if (e.kind == Align::Hit) {
			if (!edge) ++s.hit;
		} else if (e.kind == Align::Tie) {
			if (!edge) {
				++s.hit;
				++s.ties;
			}
		} else if (e.kind == Align::Extra) {
			if (!edge) ++s.extra;
		} else {
			if (!edge) ++s.miss;
		}
	}
	return s;
}

static int counted_n(const std::vector<int>& obs, int tmin, int tmax, int start_g, int end_g)
{
	int lo = tmin + start_g, hi = tmax - end_g, n = 0;
	for (int t : obs) if (t >= lo && t <= hi) ++n;
	return n;
}

enum class WigPolicy {
	Nominal,       // T as measured
	AlwaysEarly,   // first_slot(T-1): treat dist 15 as enough
	AlwaysLate,    // first_slot(T+1)
	BoundOracle,   // ±1 only when that changes the slot; pick the observed one
	BoundOracle2,  // same with ±2
	Oracle,        // ±1 on every post (schedule + holdoff); pick to match next obs
};

struct WigStat {
	std::vector<int> pred;
	int overwrites = 0;
	int n_post = 0;
	int n_bound = 0;      // first_slot(T-1) != first_slot(T) or vs T+1
	int used_m1 = 0;
	int used_0 = 0;
	int used_p1 = 0;
	int holdoff_flip = 0; // holdoff decision changed by ±1
};

static WigStat vdp_cpu_wiggle(
	const std::vector<int>& posts, const std::vector<int>& wait,
	int tmin, int tmax, const std::vector<int>& obs, WigPolicy pol)
{
	WigStat st;
	bool occupied = false;
	std::optional<int> sched;
	std::optional<int> last_s;
	size_t i = 0;
	size_t j = 0; // next observation to match
	const size_t n = posts.size();

	auto slot_of = [&](int t) { return first_slot(wait, t); };

	while (i < n || occupied) {
		std::optional<int> t_nom;
		if (i < n) t_nom = posts[i];

		int t_use = t_nom ? *t_nom : 0;
		int dt_used = 0;
		if (t_nom && pol != WigPolicy::Nominal) {
			int T = *t_nom;
			int span = (pol == WigPolicy::BoundOracle2) ? 2 : 1;
			int s0 = slot_of(T);
			int sm = slot_of(T - span);
			int sp = slot_of(T + span);
			bool bound = (sm != s0) || (s0 != sp);
			if (span == 1) {
				int sm1 = slot_of(T - 1);
				int sp1 = slot_of(T + 1);
				bound = (sm1 != s0) || (s0 != sp1);
			}
			if (bound) ++st.n_bound;

			auto dt_for_target = [&](int target) {
				for (int prefer : {0, -1, 1, -2, 2}) {
					if (std::abs(prefer) > span) continue;
					if (slot_of(T + prefer) == target) return prefer;
				}
				return 0;
			};

			if (pol == WigPolicy::AlwaysEarly) {
				dt_used = -1;
			} else if (pol == WigPolicy::AlwaysLate) {
				dt_used = 1;
			} else if ((pol == WigPolicy::BoundOracle || pol == WigPolicy::BoundOracle2 ||
				    pol == WigPolicy::Oracle) &&
				   j < obs.size()) {
				bool use = (pol == WigPolicy::Oracle) || bound;
				if (use) dt_used = dt_for_target(obs[j]);
			}
			t_use = T + dt_used;
		}

		if (!occupied && last_s && t_nom && t_use < *last_s + BUSY) {
			if (pol == WigPolicy::Oracle && t_nom) {
				int T = *t_nom;
				if (T < *last_s + BUSY && T + 1 >= *last_s + BUSY) {
					t_use = T + 1;
					dt_used = 1;
					++st.holdoff_flip;
				} else if (T - 1 < *last_s + BUSY && T >= *last_s + BUSY) {
					// nominal would not holdoff; keep as chosen
				}
			}
			if (t_use < *last_s + BUSY) {
				++st.overwrites;
				if (t_nom) {
					++st.n_post;
					if (dt_used < 0) ++st.used_m1;
					else if (dt_used > 0) ++st.used_p1;
					else ++st.used_0;
				}
				++i;
				continue;
			}
		}
		if (occupied && sched && (!t_nom || *sched <= t_use)) {
			if (tmin <= *sched && *sched <= tmax) {
				st.pred.push_back(*sched);
				if (j < obs.size() && obs[j] == *sched) ++j;
				else if (j < obs.size() && obs[j] < *sched) {
					while (j < obs.size() && obs[j] < *sched) ++j;
					if (j < obs.size() && obs[j] == *sched) ++j;
				}
			}
			last_s = sched;
			occupied = false;
			sched.reset();
			continue;
		}
		if (!t_nom) break;
		++st.n_post;
		if (dt_used < 0) ++st.used_m1;
		else if (dt_used > 0) ++st.used_p1;
		else ++st.used_0;
		if (!occupied) {
			occupied = true;
			sched = slot_of(t_use);
		} else {
			++st.overwrites;
		}
		++i;
	}
	return st;
}

// --- VCD decode (vcd2 timing: bus from previous timestamp on RAS/CAS) ---

struct Acc {
	int t = 0;
	int addr = 0;
	bool rd = true;
	// The two flags vcd2.cc prints as the 2nd and 3rd character of the type.
	// 'b' = not the first CAS after this RAS, i.e. a page-mode burst member.
	// 'v' = VDS low, i.e. the display is fetching. Without them, a bitmap
	// fetch whose address happens to end in 0x3f is indistinguishable from a
	// refresh, which mis-anchors the whole capture (FINDINGS7 §11.6).
	bool first = true;
	bool vds = true;
};

struct VcdCap {
	std::vector<Acc> acc;
	std::vector<int> csr_f, csr_r, csw_f, csw_r;
};

static std::vector<std::string> split_ws(const std::string& line)
{
	std::vector<std::string> out;
	std::istringstream in(line);
	std::string w;
	while (in >> w) out.push_back(w);
	return out;
}

static VcdCap decode_vcd(const fs::path& path)
{
	std::ifstream in(path);
	if (!in) throw std::runtime_error("cannot open " + path.string());

	// ids from the known 2026 header; also read $var in case they differ
	char idA[8] = {'!', '"', '#', '$', '%', '&', '\'', '('};
	char idRAS = ')', idCAS0 = '*', idCAS1 = '+', idRW = ',';
	char idVDS = '-';
	char idCSR = '0', idCSW = '/';

	std::string line;
	bool in_data = false;
	while (std::getline(in, line)) {
		if (!in_data) {
			if (line.rfind("$var", 0) == 0) {
				auto tok = split_ws(line);
				if (tok.size() >= 5 && tok[4].size() >= 1 && tok[3].size() == 1) {
					char id = tok[3][0];
					const std::string& name = tok[4];
					if (name.size() == 2 && name[0] == 'A' && name[1] >= '0' && name[1] <= '7') {
						idA[name[1] - '0'] = id;
					} else if (name == "RAS") idRAS = id;
					else if (name == "CAS0") idCAS0 = id;
					else if (name == "CAS1") idCAS1 = id;
					else if (name == "R/W") idRW = id;
					else if (name == "VDS") idVDS = id;
					else if (name == "CSR") idCSR = id;
					else if (name == "CSW") idCSW = id;
				}
			}
			if (line.rfind("$enddefinitions", 0) == 0) {
				in_data = true;
			}
			continue;
		}
		break;
	}

	int new_bits[8] = {};
	int bits[8] = {};
	int new_rw = 1, rw = 1;
	int new_vds = 1, vds = 1;
	bool high_used = false;
	int csr = -1, csw = -1;
	int row = 0;
	int prev = -1;
	int time = 0;
	VcdCap cap;

	auto A = [&]() {
		int v = 0;
		for (int i = 0; i < 8; ++i) if (bits[i]) v |= 1 << i;
		return v;
	};

	auto enter_time = [&]() {
		for (int i = 0; i < 8; ++i) bits[i] = new_bits[i];
		rw = new_rw;
		vds = new_vds;
	};

	auto handle_line = [&](const std::string& ln) {
		if (ln.empty() || ln == "$end") return;
		auto tokens = split_ws(ln);
		if (tokens.empty()) return;
		size_t i0 = 0;
		if (!tokens[0].empty() && tokens[0][0] == '#') {
			time = std::stoi(tokens[0].substr(1));
			i0 = 1;
		}
		if (time != prev) {
			enter_time();
			prev = time;
		}
		for (size_t i = i0; i < tokens.size(); ++i) {
			const std::string& tok = tokens[i];
			if (tok.size() < 2) continue;
			int v = (tok[0] == '1') ? 1 : 0;
			char sid = tok[1];
			bool is_a = false;
			for (int b = 0; b < 8; ++b) {
				if (sid == idA[b]) {
					new_bits[b] = v;
					is_a = true;
					break;
				}
			}
			if (is_a) continue;
			if (sid == idRW) {
				new_rw = v;
			} else if (sid == idVDS) {
				new_vds = v;
			} else if (sid == idRAS) {
				if (v == 0) {
					row = A() << 8;
					high_used = false;
				}
			} else if (sid == idCAS0) {
				if (v == 0) {
					cap.acc.push_back({time, row + A(), rw == 1,
							   !high_used, vds == 1});
					high_used = true;
				}
			} else if (sid == idCAS1) {
				if (v == 0) {
					cap.acc.push_back({time, row + A() + 0x10000,
							   rw == 1, !high_used, vds == 1});
					high_used = true;
				}
			} else if (sid == idCSR) {
				if (csr == 1 && v == 0) cap.csr_f.push_back(time);
				if (csr == 0 && v == 1) cap.csr_r.push_back(time);
				csr = v;
			} else if (sid == idCSW) {
				if (csw == 1 && v == 0) cap.csw_f.push_back(time);
				if (csw == 0 && v == 1) cap.csw_r.push_back(time);
				csw = v;
			}
		}
	};

	if (in_data && !line.empty()) handle_line(line);
	while (std::getline(in, line)) handle_line(line);
	return cap;
}

// --- refresh timebase (same as part2/2.rw/process.cc) ---

static std::vector<int> candidate_filter1(const std::vector<Acc>& acc)
{
	std::vector<int> c;
	for (int i = 0; i < int(acc.size()); ++i) {
		// process.cc requires type == "R..", i.e. read, first CAS after
		// RAS, and VDS inactive. Match it exactly, or the two sides can
		// pick different scan-line origins.
		const Acc& a = acc[i];
		if (a.rd && a.first && a.vds && (a.addr & 0x3f) == 0x3f) c.push_back(i);
	}
	return c;
}

static std::vector<int> candidate_filter2b(
	const std::vector<Acc>& acc, const std::vector<int>& cand, size_t start)
{
	int prev = acc[cand[start]].addr;
	std::vector<int> out = {cand[int(start)]};
	for (size_t i = start + 1; i < cand.size(); ++i) {
		int idx = cand[i];
		int nxt = acc[idx].addr;
		if ((nxt & 0xf0000) != ((prev & 0xf0000) ^ 0x10000)) continue;
		if ((nxt & 0x3f) == (prev & 0x3f)) {
			if ((((prev >> 8) + 1) & 0xff) != ((nxt >> 8) & 0xff)) continue;
		} else {
			if ((((prev >> 8) + 1) & 0xf0) != ((nxt >> 8) & 0xf0)) continue;
		}
		out.push_back(idx);
		prev = nxt;
	}
	return out;
}

static std::vector<int> candidate_filter2(
	const std::vector<Acc>& acc, const std::vector<int>& cand)
{
	std::vector<int> best;
	for (size_t s = 0; s < cand.size(); ++s) {
		auto c = candidate_filter2b(acc, cand, s);
		if (c.size() > best.size()) best = std::move(c);
	}
	return best;
}

static std::vector<int> find_refresh_starts(
	const std::vector<Acc>& acc, const std::vector<int>& refresh)
{
	std::vector<int> starts;
	if (refresh.size() < 8) return starts;
	for (size_t i = 1; i + 7 < refresh.size(); ++i) {
		int prev_gap = acc[refresh[i]].t - acc[refresh[i - 1]].t;
		int small[7];
		for (int j = 0; j < 7; ++j) {
			small[j] = acc[refresh[i + j + 1]].t - acc[refresh[i + j]].t;
		}
		int sorted[7];
		std::copy(small, small + 7, sorted);
		std::sort(sorted, sorted + 7);
		int med = sorted[3];
		int mn = sorted[0], mx = sorted[6];
		bool similar = (mx - mn) <= std::max(2000, 3 * med / 5);
		bool large = prev_gap >= std::max(3 * med, mx + 10000);
		if (similar && large) starts.push_back(refresh[i]);
	}
	return starts;
}

static int refresh_vdp(int r)
{
	int m = r / 8;
	int o = r % 8;
	if (o < 0) {
		o += 8;
		m -= 1;
	}
	return LINE * m + REFRESH_ROW[o];
}

struct Anchor {
	int t_vcd;
	double t_vdp;
};

static std::vector<Anchor> make_anchors(
	const std::vector<Acc>& acc, const std::vector<int>& refresh,
	const std::vector<int>& starts)
{
	if (starts.empty()) throw std::runtime_error("no refresh starts");
	int first = starts[0];
	auto it = std::find(refresh.begin(), refresh.end(), first);
	if (it == refresh.end()) throw std::runtime_error("refresh start not in list");
	int pos = int(it - refresh.begin());
	int r0 = (pos == 0) ? 0 : 8;

	std::vector<int> r_of(refresh.size());
	if (!ANCHOR_FIX) {
		int r = r0;
		for (int j = pos - 1; j >= 0; --j) {
			--r;
			r_of[j] = r;
		}
		r = r0;
		for (int j = pos; j < int(refresh.size()); ++j) {
			r_of[j] = r;
			++r;
		}
	} else {
		// Stepping the refresh index by one per detected refresh assumes
		// the detector never misses one and never invents one. It does
		// both, and then every later anchor carries the wrong VDP time.
		// The crystal frequency is known, so let the elapsed wall time
		// say how many refresh periods really went by.
		auto step_from_wall = [&](int r, int dt) {
			double want = dt * VDP_PER_UNIT;
			int best = 1;
			double berr = 1e18;
			for (int k = 0; k <= ANCHOR_MAX_STEP; ++k) {
				double err = std::abs(refresh_vdp(r + k) - refresh_vdp(r) - want);
				if (err < berr) { berr = err; best = k; }
			}
			return best;
		};
		r_of[pos] = r0;
		for (int j = pos + 1; j < int(refresh.size()); ++j) {
			int dt = acc[refresh[j]].t - acc[refresh[j - 1]].t;
			r_of[j] = r_of[j - 1] + step_from_wall(r_of[j - 1], dt);
		}
		for (int j = pos - 1; j >= 0; --j) {
			int dt = acc[refresh[j + 1]].t - acc[refresh[j]].t;
			// Search backwards from the later anchor.
			double want = dt * VDP_PER_UNIT;
			int best = 1;
			double berr = 1e18;
			for (int k = 0; k <= ANCHOR_MAX_STEP; ++k) {
				double err = std::abs(refresh_vdp(r_of[j + 1]) -
						      refresh_vdp(r_of[j + 1] - k) - want);
				if (err < berr) { berr = err; best = k; }
			}
			r_of[j] = r_of[j + 1] - best;
		}
	}

	std::vector<Anchor> a;
	a.reserve(refresh.size());
	for (size_t i = 0; i < refresh.size(); ++i) {
		// A step of 0 means the detector fired twice for one refresh;
		// keeping both would put two VDP times on one wall time.
		if (i && r_of[i] == r_of[i - 1]) continue;
		a.push_back({acc[refresh[i]].t, double(refresh_vdp(r_of[i]))});
	}
	return a;
}

static double interpolate(int t, const std::vector<Anchor>& a)
{
	if (a.size() < 2) throw std::runtime_error("need 2 refresh anchors");
	if (t <= a.front().t_vcd) {
		double factor = (a[1].t_vdp - a[0].t_vdp) / double(a[1].t_vcd - a[0].t_vcd);
		return a[0].t_vdp - (a[0].t_vcd - t) * factor;
	}
	if (t >= a.back().t_vcd) {
		const auto& a0 = a[a.size() - 2];
		const auto& a1 = a.back();
		double factor = (a1.t_vdp - a0.t_vdp) / double(a1.t_vcd - a0.t_vcd);
		return (t - a0.t_vcd) * factor + a0.t_vdp;
	}
	size_t lo = 0, hi = a.size() - 1;
	while (lo + 1 < hi) {
		size_t mid = (lo + hi) / 2;
		if (a[mid].t_vcd <= t) lo = mid;
		else hi = mid;
	}
	double factor = (a[hi].t_vdp - a[lo].t_vdp) / double(a[hi].t_vcd - a[lo].t_vcd);
	return (t - a[lo].t_vcd) * factor + a[lo].t_vdp;
}

// The anchors are refresh bursts, roughly one per scan line, and each is
// located only to the nearest analyzer sample. Interpolating between adjacent
// pairs therefore hands every edge between them the quantisation error of both
// brackets, and that is the dominant per-edge error (§11.5).
//
// It need not be. Both clocks are crystals: over the 400 us of a capture
// neither drifts measurably, so the true map from analyzer time to VDP cycles
// is a single straight line, and fitting one through all the anchors at once
// averages their quantisation down by sqrt(N). Returning the fit as two
// anchors on the line leaves `interpolate` to evaluate it, since between two
// anchors it is exactly that line.
//
// The risk is the mirror of the gain: one anchor with the wrong refresh index
// corrupts a global fit everywhere, where pairwise interpolation would confine
// the damage. So the worst residual is returned for the caller to check.
bool LSQ_TIME = true; // --pairtime: interpolate between adjacent anchors

// A few anchors carry the wrong refresh count outright -- tens of cycles off,
// not a fraction of a sample -- so the fit has to be robust or those few would
// tilt the line for the whole capture. Anchors are dropped and the line refitted
// until only ones consistent with their own quantisation remain.
int LSQ_DROPPED = 0;
int LSQ_ANCHORS = 0;

static std::vector<Anchor> lsq_anchors(const std::vector<Anchor>& a, double* worst)
{
	if (worst) *worst = 0;
	if (!LSQ_TIME || a.size() < 3) return a;
	// t_vcd spans ~4e6, so centre before accumulating.
	double x0 = a.front().t_vcd;
	std::vector<char> use(a.size(), 1);
	double slope = 0, inter = 0;
	for (int pass = 0; pass < 4; ++pass) {
		double sx = 0, sy = 0, sxx = 0, sxy = 0, n = 0;
		for (size_t i = 0; i < a.size(); ++i) {
			if (!use[i]) continue;
			double x = a[i].t_vcd - x0;
			sx += x;
			sy += a[i].t_vdp;
			sxx += x * x;
			sxy += x * a[i].t_vdp;
			++n;
		}
		if (n < 3) return a;
		double den = n * sxx - sx * sx;
		if (den <= 0) return a;
		slope = (n * sxy - sx * sy) / den;
		inter = (sy - slope * sx) / n;
		// An anchor is quantised to a sample, so half a cycle of residual is
		// as much as a sound one can show. Anything past a whole cycle is a
		// miscounted refresh, and dropping it costs nothing: there are ~100
		// anchors and the line needs two.
		int dropped = 0;
		for (size_t i = 0; i < a.size(); ++i) {
			if (!use[i]) continue;
			double r = a[i].t_vdp - (inter + slope * (a[i].t_vcd - x0));
			if (std::abs(r) > 1.0) {
				use[i] = 0;
				++dropped;
			}
		}
		LSQ_DROPPED += dropped;
		if (!dropped) break;
	}
	LSQ_ANCHORS += int(a.size());
	if (worst)
		for (size_t i = 0; i < a.size(); ++i)
			if (use[i])
				*worst = std::max(
					*worst,
					std::abs(a[i].t_vdp
						 - (inter + slope * (a[i].t_vcd - x0))));
	int lo = a.front().t_vcd, hi = a.back().t_vcd;
	return {{lo, inter + slope * (lo - x0)}, {hi, inter + slope * (hi - x0)}};
}

struct SlotCpu {
	int cas = 0;
	int addr = 0;
	bool rd = true;
	char kind = 0; // engine: 's','d','w'
};

struct ParsedTxt {
	std::vector<SlotCpu> cpu;
	std::vector<SlotCpu> eng;
	std::vector<SlotCpu> dummy; // R..
};

static ParsedTxt parse_txt(const fs::path& path)
{
	std::ifstream in(path);
	if (!in) throw std::runtime_error("cannot open " + path.string());
	ParsedTxt out;
	std::string line;
	while (std::getline(in, line)) {
		auto colon = line.find(':');
		if (colon == std::string::npos) continue;
		int row = int(std::strtol(line.c_str(), nullptr, 10));
		std::string_view rest(line.c_str() + colon + 1, line.size() - (colon + 1));
		int col = 0;
		while (rest.size() >= 13 ||
		       (rest.size() > 2 && rest.substr(2).find_first_not_of(' ') != std::string_view::npos)) {
			std::string_view cell = rest.size() >= 13 ? rest.substr(2, 11) : rest.substr(2);
			auto nonempty = cell.find_first_not_of(' ');
			if (nonempty != std::string_view::npos && cell.size() >= 3) {
				char rw = cell[0];
				char type = cell[2];
				int addr = 0;
				auto hx = cell.find("0x");
				if (hx != std::string_view::npos) {
					addr = int(std::strtol(cell.data() + hx, nullptr, 16));
				}
				SlotCpu item{LINE * col + row, addr, rw == 'R'};
				// 'W.r' is a CPU write into 0x14000-0x16000, which
				// annotate_type() reserves for CPU reads. Only the
				// rdwrCpu / wrCpu132 captures use that region for writes.
				if ((rw == 'R' && type == 'r') ||
				    (rw == 'W' && (type == 'w' || type == 'r'))) {
					out.cpu.push_back(item);
				} else if (rw == 'R' && type == '.') {
					out.dummy.push_back(item);
				} else if (rw == 'R' && type == 's') {
					item.kind = 's';
					out.eng.push_back(item);
				} else if (rw == 'R' && type == 'd') {
					item.kind = 'd';
					out.eng.push_back(item);
				} else if (rw == 'W' && (type == 'd' || type == 'e' || type == '.')) {
					item.kind = 'w';
					out.eng.push_back(item);
				}
			}
			if (rest.size() < 13) break;
			rest.remove_prefix(13);
			++col;
		}
	}
	std::sort(out.cpu.begin(), out.cpu.end(),
		[](const SlotCpu& a, const SlotCpu& b) { return a.cas < b.cas; });
	std::sort(out.eng.begin(), out.eng.end(),
		[](const SlotCpu& a, const SlotCpu& b) { return a.cas < b.cas; });
	std::sort(out.dummy.begin(), out.dummy.end(),
		[](const SlotCpu& a, const SlotCpu& b) { return a.cas < b.cas; });
	return out;
}

static bool is_cpu_file(std::string_view name, std::string_view filter)
{
	if (name.size() < 4 || name.substr(name.size() - 4) != ".txt") return false;
	if (name.find("noCpu") != std::string_view::npos) return false;
	if (name.find("wrong") != std::string_view::npos) return false;
	if (name.find("rdCpu") == std::string_view::npos &&
	    name.find("wrCpu") == std::string_view::npos) return false;
	bool stop = name.find("-stop-") != std::string_view::npos;
	if (filter.empty() || filter == "all") return true;
	if (filter == "stop") return stop;
	if (stop) return false;
	Cmd c = cmd_of(name);
	if (c == Cmd::Unknown) return false;
	return cmd_name(c) == filter;
}

static bool is_nocpu_file(std::string_view name, std::string_view filter)
{
	if (name.size() < 4 || name.substr(name.size() - 4) != ".txt") return false;
	if (name.find("noCpu") == std::string_view::npos) return false;
	if (name.find("wrong") != std::string_view::npos) return false;
	Cmd c = cmd_of(name);
	if (c == Cmd::Unknown) return false;
	if (filter.empty() || filter == "all") return true;
	return cmd_name(c) == filter;
}

struct Capture {
	std::string name;
	Mode mode = Mode::DispOff;
	std::vector<int> obs;
	std::vector<int> dummy;
	std::vector<EngAcc> eng;
	int tmin = 0;
	int tmax = 0;
	std::vector<double> t2[2]; // 0=fall, 1=rise
	std::vector<double> t2r[2]; // /CSR only, for --rw
	std::vector<double> t2w[2]; // /CSW only, for --rw
	std::vector<char> obs_rd;   // parallel to obs: 1 = VRAM read
	// The raw VCD timestamps behind t2[1], kept parallel to it, so that a
	// suspect edge can be judged in wall time before the refresh
	// interpolation has had a chance to move it.
	std::vector<int> raw_r;
	std::vector<int> raw_f[2]; // 0 = /CSR falls, 1 = /CSW falls, raw
	double anchor_worst = 0;   // worst anchor residual against the fitted line
};

// Every /CSx pulse has the same true width W = 55 + f samples, so the sampled
// width is a one-bit measurement of where inside its own sample the rising
// edge really fell. With the edge detected at sample R the true rise sits at
// R - 1 + v, and the width says which side of f the phase v is on: 56 means
// v <= f, 55 means v > f. The midpoint of the surviving interval is then a
// better estimate of the edge than R itself.
//
// Only the difference between the two cases carries information -- half a
// sample, whichever f is -- so the offsets are centred to leave the mean of
// t2 where it was. That matters because the integer model of 11.2 adds a
// whole-cycle delta and cannot absorb a common sub-sample shift, while the
// trellis absorbs it into phi either way. Weighted by f and 1 - f the raw
// midpoints average to exactly -1/2 sample for any f, so centring is just
// adding 1/2. A width we do not recognise carries no phase information and
// keeps the old convention.
constexpr double CSX_WIDTH_FRAC = 0.70; // fitted in FINDINGS7 §11.5
bool WIDTH_FIX = true;                  // --nowidthfix

static double rise_subsample(const std::vector<int>& falls, int rise)
{
	if (!WIDTH_FIX) return 0.0;
	auto it = std::upper_bound(falls.begin(), falls.end(), rise);
	if (it == falls.begin()) return 0.0;
	int w = (rise - *(it - 1)) / UNITS_PER_SAMPLE;
	if (w == 56) return (CSX_WIDTH_FRAC - 1.0) / 2;
	if (w == 55) return CSX_WIDTH_FRAC / 2;
	return 0.0;
}

// A pair of transitions closer together than min_w is a glitch. /CSx is low
// for 56 samples per access and high for the remainder of a loop iteration
// that is never shorter than 250, so nothing real lasts one or two samples.
// Dropping both transitions of such a pair restores the level that surrounded
// it, which re-joins the pulse a spike had split in two -- and that is the
// point of doing it in both polarities: a spike high in the middle of an
// access leaves two short lows whose rising edges are both wrong, and keeping
// either of them puts the request some 12 cycles off the loop pace.
// Cancelled pairs whose cancelled interval was *high*, i.e. a spike inside an
// access, as opposed to the long-known ringing on a trailing edge. Only the
// high ones are counted, because those are the rare ones (--pacescan).
int GLITCH_HIGH = 0;

static void drop_glitches(std::vector<int>& falls, std::vector<int>& rises, int min_w)
{
	struct Ev { int t; bool rise; };
	std::vector<Ev> evs;
	evs.reserve(falls.size() + rises.size());
	for (int t : falls) evs.push_back({t, false});
	for (int t : rises) evs.push_back({t, true});
	std::sort(evs.begin(), evs.end(),
		  [](const Ev& a, const Ev& b) { return a.t < b.t; });
	std::vector<Ev> out;
	for (const Ev& e : evs) {
		// Cancelling a pair makes its predecessor the neighbour of
		// whatever comes next, which is what the level does too, so a
		// run of spikes collapses in this one pass.
		if (!out.empty() && out.back().rise != e.rise
		    && e.t - out.back().t < min_w) {
			if (out.back().rise) ++GLITCH_HIGH;
			out.pop_back();
			continue;
		}
		out.push_back(e);
	}
	falls.clear();
	rises.clear();
	for (const Ev& e : out) (e.rise ? rises : falls).push_back(e.t);
}

static fs::path resolve_vcd(const fs::path& txt, const fs::path& vcd)
{
	if (fs::exists(vcd)) return vcd;
	auto stem = txt.stem().string();
	auto dir = vcd.parent_path();
	if (stem == "scr5-dispOff-hmmv-rdCpu222-7e")
		return dir / "scr5-sprOff-hmmv-rdCpu222-3e.vcd";
	if (stem == "scr5-dispOff-hmmv-rdCpu222-8e")
		return dir / "scr5-sprOff-hmmv-rdCpu222-4e.vcd";
	return vcd;
}

// A companion vector is filtered alongside, so a caller can keep the raw
// timestamps in step with the cycle positions derived from them.
static void debounce_t2(std::vector<double>& t2, double min_gap,
			std::vector<int>* raw = nullptr)
{
	if (t2.empty()) return;
	std::vector<double> out;
	std::vector<int> rout;
	out.reserve(t2.size());
	double prev = t2.front() - min_gap - 1;
	for (size_t i = 0; i < t2.size(); ++i) {
		if (t2[i] - prev >= min_gap) {
			out.push_back(t2[i]);
			if (raw && i < raw->size()) rout.push_back((*raw)[i]);
		}
		prev = t2[i];
	}
	t2 = std::move(out);
	if (raw) *raw = std::move(rout);
}

static Capture load_engine_txt(const fs::path& txt)
{
	Capture c;
	c.name = txt.filename().string();
	c.mode = mode_of(c.name);
	auto parsed = parse_txt(txt);
	c.eng.reserve(parsed.eng.size());
	for (auto& s : parsed.eng)
		c.eng.push_back({cas_to_ras(c.mode, s.cas), s.addr, s.kind});
	return c;
}

static Capture load_txt_only(const fs::path& txt)
{
	Capture c;
	c.name = txt.filename().string();
	c.mode = mode_of(c.name);
	auto parsed = parse_txt(txt);
	c.obs.reserve(parsed.cpu.size());
	for (auto& s : parsed.cpu) c.obs.push_back(cas_to_ras(c.mode, s.cas));
	c.dummy.reserve(parsed.dummy.size());
	for (auto& s : parsed.dummy) c.dummy.push_back(cas_to_ras(c.mode, s.cas));
	c.eng.reserve(parsed.eng.size());
	for (auto& s : parsed.eng)
		c.eng.push_back({cas_to_ras(c.mode, s.cas), s.addr, s.kind});
	return c;
}

static int classify_row(int ras, const std::unordered_set<int>& legal,
			const std::unordered_set<int>& packed,
			const std::unordered_set<int>& cmd)
{
	int row = mod_line(ras);
	if (legal.count(row)) return 0;
	if (packed.count(row)) return 1;
	if (cmd.count(row)) return 2;
	return 3;
}

// From-scratch 2026 pass: slot occupancy and FINDINGS5 engine (packed-start +1,
// P5 + newline) skipping observed CPU RAS (and optionally observed R..).
// observed CPU RAS (and optionally observed R..). No /CSR, no δ.
static int run_scratch_2026(const fs::path& slots_dir)
{
	std::vector<int> cpu_slot[3], packed_slot[3];
	std::unordered_set<int> legal_set[3], packed_set[3], cmd_set[3];
	for (int m = 0; m < 3; ++m) {
		cpu_slot[m] = cpu_slots_of(CMD_TABLE[m]);
		packed_slot[m] = packed_slots_of(CMD_TABLE[m]);
		legal_set[m] = {cpu_slot[m].begin(), cpu_slot[m].end()};
		packed_set[m] = {packed_slot[m].begin(), packed_slot[m].end()};
		for (int i = 0; i < CMD_TABLE[m].n; ++i)
			cmd_set[m].insert(CMD_TABLE[m].data[i]);
	}

	std::vector<fs::path> files;
	for (const auto& ent : fs::directory_iterator(slots_dir)) {
		if (!ent.is_regular_file()) continue;
		auto name = ent.path().filename().string();
		if (is_cpu_file(name, "all")) files.push_back(ent.path());
	}
	std::sort(files.begin(), files.end());

	struct Acc {
		int n = 0, legal = 0, packed = 0, off = 0;
	};
	Acc cpu_m[3], dum_m[3], eng_m[3];
	int n_collide = 0, n_stop = 0, n_mix = 0;

	struct Mix {
		Capture cap;
		Cmd cmd = Cmd::Unknown;
	};
	std::vector<Mix> mixed;

	std::cout << "=== 2026 A. occupancy (RAS) ===\n";
	for (const auto& txt : files) {
		Capture cap = load_txt_only(txt);
		int mi = mode_index(cap.mode);
		bool stop = cap.name.find("-stop-") != std::string::npos;
		if (stop) ++n_stop;
		else ++n_mix;
		Acc cc{}, dd{}, ee{};
		for (int ras : cap.obs) {
			++cc.n;
			int k = classify_row(ras, legal_set[mi], packed_set[mi], cmd_set[mi]);
			if (k == 0) ++cc.legal;
			else if (k == 1) ++cc.packed;
			else ++cc.off;
		}
		for (int ras : cap.dummy) {
			++dd.n;
			int k = classify_row(ras, legal_set[mi], packed_set[mi], cmd_set[mi]);
			if (k == 0) ++dd.legal;
			else if (k == 1) ++dd.packed;
			else ++dd.off;
		}
		for (auto& e : cap.eng) {
			++ee.n;
			int k = classify_row(e.ras, legal_set[mi], packed_set[mi], cmd_set[mi]);
			if (k == 0) ++ee.legal;
			else if (k == 1) ++ee.packed;
			else ++ee.off;
		}
		cpu_m[mi].n += cc.n; cpu_m[mi].legal += cc.legal;
		cpu_m[mi].packed += cc.packed; cpu_m[mi].off += cc.off;
		dum_m[mi].n += dd.n; dum_m[mi].legal += dd.legal;
		dum_m[mi].packed += dd.packed; dum_m[mi].off += dd.off;
		eng_m[mi].n += ee.n; eng_m[mi].legal += ee.legal;
		eng_m[mi].packed += ee.packed; eng_m[mi].off += ee.off;
		std::unordered_set<int> occ(cap.obs.begin(), cap.obs.end());
		int col = 0;
		for (auto& e : cap.eng) if (occ.count(e.ras)) ++col;
		n_collide += col;
		if (!cap.eng.empty()) mixed.push_back({std::move(cap), cmd_of(txt.filename().string())});
		if (cc.packed || cc.off || dd.legal || dd.off || ee.off || col) {
			std::cout << "  odd " << txt.filename().string()
				  << " cpu " << cc.n << " L" << cc.legal << " P" << cc.packed << " X" << cc.off
				  << "  R.. " << dd.n << " L" << dd.legal << " P" << dd.packed << " X" << dd.off
				  << "  eng " << ee.n << " L" << ee.legal << " P" << ee.packed << " X" << ee.off
				  << "  col " << col << "\n";
		}
	}
	std::cout << "files stop " << n_stop << " mixed " << n_mix << " collide(CPU∩eng RAS) " << n_collide << "\n";
	auto dump_acc = [](const char* lab, Acc a[3]) {
		std::cout << lab << " legal/packed/off:\n";
		for (int m = 0; m < 3; ++m) {
			if (!a[m].n) continue;
			std::cout << "  " << mode_name(Mode(m)) << "  "
				  << a[m].legal << '/' << a[m].packed << '/' << a[m].off
				  << " of " << a[m].n << "\n";
		}
	};
	dump_acc("CPU", cpu_m);
	dump_acc("R..", dum_m);
	dump_acc("eng", eng_m);

	std::cout << "\n=== 2026 B. engine, skip occupied (from observed last) ===\n";
	std::cout << "A = skip observed CPU RAS only\n";
	std::cout << "P = A + P5 + newline (promoted mixed, no dummy tag)\n";
	std::cout << "B = skip CPU RAS + observed R..\n";
	std::cout << "C = B + P5 + newline (dummy oracle)\n";

	int cmd_n[6][3] = {}, cmd_ok_a[6][3] = {}, cmd_ok_b[6][3] = {};
	int cmd_ok_p[6][3] = {}, cmd_ok_c[6][3] = {};
	int cmd_h_a[6][3] = {}, cmd_n_a[6][3] = {}, cmd_h_b[6][3] = {};
	int cmd_h_p[6][3] = {}, cmd_h_c[6][3] = {};
	int nA = 0, nB = 0, nP = 0, nC = 0, nF = 0;
	int hitA = 0, hitB = 0, hitP = 0, hitC = 0, totE = 0;

	struct Leftover {
		std::string name;
		char which;
		int k = 0, last = 0, delta = 0, pred = 0, obs = 0;
		bool cpu_pred = false, rdd_pred = false, cpu_obs = false, packed_pred = false;
	};
	std::vector<Leftover> left;

	for (auto& mx : mixed) {
		auto& cap = mx.cap;
		Cmd cmd = mx.cmd;
		if (cmd == Cmd::Unknown || cap.eng.size() < 2) continue;
		++nF;
		int mi = mode_index(cap.mode);
		int ci = int(cmd);
		Variant var = parse_variant(cap.name);
		std::unordered_set<int> occ_cpu(cap.obs.begin(), cap.obs.end());
		auto occ_rdd = occ_cpu;
		for (int d : cap.dummy) occ_rdd.insert(d);
		std::vector<int> obs_e;
		for (auto& e : cap.eng) obs_e.push_back(e.ras);
		auto pa = predict_engine(cap.eng, cap.mode, cmd, occ_cpu, var, true);
		auto pp = predict_engine(cap.eng, cap.mode, cmd, occ_cpu, var, true, &cap.obs);
		auto pb = predict_engine(cap.eng, cap.mode, cmd, occ_rdd, var, true);
		auto pc = predict_engine(cap.eng, cap.mode, cmd, occ_rdd, var, true, &cap.obs);
		auto sa = score_times(pa.pred, obs_e);
		auto sp = score_times(pp.pred, obs_e);
		auto sb = score_times(pb.pred, obs_e);
		auto sc = score_times(pc.pred, obs_e);
		bool okA = sa.extra == 0 && sa.miss == 0;
		bool okP = sp.extra == 0 && sp.miss == 0;
		bool okB = sb.extra == 0 && sb.miss == 0;
		bool okC = sc.extra == 0 && sc.miss == 0;
		nA += okA;
		nP += okP;
		nB += okB;
		nC += okC;
		++cmd_n[ci][mi];
		if (okA) ++cmd_ok_a[ci][mi];
		if (okP) ++cmd_ok_p[ci][mi];
		if (okB) ++cmd_ok_b[ci][mi];
		if (okC) ++cmd_ok_c[ci][mi];
		cmd_h_a[ci][mi] += sa.hit;
		cmd_h_p[ci][mi] += sp.hit;
		cmd_h_b[ci][mi] += sb.hit;
		cmd_h_c[ci][mi] += sc.hit;
		cmd_n_a[ci][mi] += int(obs_e.size());
		hitA += sa.hit;
		hitP += sp.hit;
		hitB += sb.hit;
		hitC += sc.hit;
		totE += int(obs_e.size());

		auto first_miss = [&](const std::vector<int>& pred, const std::unordered_set<int>& occ,
				      char which, bool p5) {
			int last = cap.eng.front().ras;
			for (size_t k = 1; k < cap.eng.size(); ++k) {
				auto w = step_wait(cmd, cap.mode, cap.eng, int(k), last, var);
				int delta = pick_delta(w, last, CMD_TABLE[mi], occ, cap.eng[k].ras);
				if (delta < 0) delta = 24 + ((cap.mode == Mode::SprOn) ? 1 : 0);
				std::unordered_set<int> occ2 = occ;
				int cand = next_cmd_slot(last, delta, CMD_TABLE[mi], occ2);
				if (p5) {
					int Snext = cpu_snext(cap.obs, last);
					if (skip_packed_for_cpu(cap.mode, last, cand, delta, Snext))
						occ2.insert(cand);
				}
				int pred_s = next_cmd_slot(last, delta, CMD_TABLE[mi], occ2);
				int obs = cap.eng[k].ras;
				if (pred_s != obs) {
					Leftover L;
					L.name = cap.name;
					L.which = which;
					L.k = int(k);
					L.last = last;
					L.delta = delta;
					L.pred = pred_s;
					L.obs = obs;
					L.cpu_pred = occ_cpu.count(pred_s);
					L.rdd_pred = occ_rdd.count(pred_s) && !occ_cpu.count(pred_s);
					L.cpu_obs = occ_cpu.count(obs);
					L.packed_pred = packed_set[mi].count(mod_line(pred_s));
					left.push_back(L);
					return;
				}
				last = obs;
			}
			(void)pred;
		};
		if (!okA) first_miss(pa.pred, occ_cpu, 'A', false);
		if (!okP) first_miss(pp.pred, occ_cpu, 'P', true);
		if (!okB) first_miss(pb.pred, occ_rdd, 'B', false);
		if (!okC) first_miss(pc.pred, occ_rdd, 'C', true);
	}

	std::cout << "files  A " << nA << '/' << nF << "  P " << nP << '/' << nF
		  << "  B " << nB << '/' << nF << "  C " << nC << '/' << nF << "\n";
	std::cout << "steps  A " << hitA << '/' << totE << "  P " << hitP << '/' << totE
		  << "  B " << hitB << '/' << totE << "  C " << hitC << '/' << totE << "\n";
	std::cout << "command × mode  files(A/P/B/C perfect)  steps A hit/n\n";
	for (int ci = 0; ci < 6; ++ci) {
		for (int mi = 0; mi < 3; ++mi) {
			if (!cmd_n[ci][mi]) continue;
			std::cout << "  " << std::left << std::setw(6) << cmd_name(Cmd(ci))
				  << std::setw(8) << mode_name(Mode(mi))
				  << std::right << cmd_ok_a[ci][mi] << '/' << cmd_ok_p[ci][mi]
				  << '/' << cmd_ok_b[ci][mi] << '/' << cmd_ok_c[ci][mi]
				  << '/' << cmd_n[ci][mi]
				  << "  " << cmd_h_a[ci][mi] << '/' << cmd_n_a[ci][mi]
				  << "\n";
		}
	}

	std::cout << "\nfirst leftover (A = CPU RAS; P = +P5+nl; B = +R..; C = R..+P5+nl):\n";
	int shown = 0;
	int nleft[4] = {}, nA_rdd = 0, nA_idle = 0, nA_other = 0;
	auto wi = [](char w) {
		return w == 'A' ? 0 : w == 'P' ? 1 : w == 'B' ? 2 : 3;
	};
	for (auto& L : left) {
		++nleft[wi(L.which)];
		if (L.which == 'A') {
			if (L.rdd_pred) ++nA_rdd;
			else if (L.packed_pred) ++nA_idle;
			else ++nA_other;
		}
	}
	std::cout << "  leftovers A " << nleft[0] << " (R..@pred " << nA_rdd
		  << ", idle-packed " << nA_idle << ", other " << nA_other
		  << ")  P " << nleft[1] << "  B " << nleft[2] << "  C " << nleft[3] << "\n";
	for (auto& L : left) {
		if (L.which == 'A' && L.rdd_pred) continue;
		if (shown >= 30) {
			std::cout << "  ... more\n";
			break;
		}
		std::cout << "  " << L.which << " " << L.name
			  << " k=" << L.k
			  << " last=" << L.last << " row " << mod_line(L.last)
			  << " Δ=" << L.delta
			  << " pred=" << L.pred << " row " << mod_line(L.pred)
			  << " obs=" << L.obs << " row " << mod_line(L.obs)
			  << (L.cpu_pred ? " cpu@pred" : "")
			  << (L.rdd_pred ? " R..@pred" : "")
			  << (L.packed_pred && !L.cpu_pred && !L.rdd_pred ? " idle-packed@pred" : "")
			  << (L.cpu_obs ? " cpu@obs" : "")
			  << "\n";
		++shown;
	}
	return 0;
}

static Capture load_capture(const fs::path& txt, const fs::path& vcd)
{
	Capture c;
	c.name = txt.filename().string();
	c.mode = mode_of(c.name);
	auto parsed = parse_txt(txt);

	c.obs.reserve(parsed.cpu.size());
	for (auto& s : parsed.cpu) {
		c.obs.push_back(cas_to_ras(c.mode, s.cas));
		c.obs_rd.push_back(s.rd ? 1 : 0);
	}
	c.dummy.reserve(parsed.dummy.size());
	for (auto& s : parsed.dummy) c.dummy.push_back(cas_to_ras(c.mode, s.cas));
	c.eng.reserve(parsed.eng.size());
	for (auto& s : parsed.eng)
		c.eng.push_back({cas_to_ras(c.mode, s.cas), s.addr, s.kind});

	if (c.obs.empty()) {
		if (!c.eng.empty()) {
			c.tmin = c.eng.front().ras;
			c.tmax = c.eng.back().ras;
		}
		return c;
	}
	fs::path vcd_path = resolve_vcd(txt, vcd);
	if (!fs::exists(vcd_path)) throw std::runtime_error(c.name + ": missing VCD");

	auto cap = decode_vcd(vcd_path);
	if (cap.acc.size() <= 2) throw std::runtime_error(c.name + ": too few VCD accesses");
	cap.acc.erase(cap.acc.begin(), cap.acc.begin() + 2);

	auto cand = candidate_filter1(cap.acc);
	auto refresh = candidate_filter2(cap.acc, cand);
	auto starts = find_refresh_starts(cap.acc, refresh);
	auto anchors = make_anchors(cap.acc, refresh, starts);
	double anchor_worst = 0;
	anchors = lsq_anchors(anchors, &anchor_worst);
	c.anchor_worst = anchor_worst;

	c.tmin = c.obs.front();
	c.tmax = c.obs.back();

	int min_w = PULSE_FIX ? CSX_MIN_SAMPLES * UNITS_PER_SAMPLE : CSX_MIN_SAMPLES;
	drop_glitches(cap.csr_f, cap.csr_r, min_w);
	drop_glitches(cap.csw_f, cap.csw_r, min_w);

	// "rdwrCpu" interleaves IN and OUT, so its request train is /CSR *and*
	// /CSW. "rdwrCpu" also contains "wrCpu", so test for it first.
	bool both = c.name.find("rdwr") != std::string::npos;
	bool write = !both && c.name.find("wrCpu") != std::string::npos;
	const std::vector<int>* edges[2] = {
		write ? &cap.csw_f : &cap.csr_f,
		write ? &cap.csw_r : &cap.csr_r,
	};
	const std::vector<int>* csw[2] = {&cap.csw_f, &cap.csw_r};
	// The width that measures a rising edge is the width of its own pulse, so
	// the falls have to come from the same train as the rise.
	const std::vector<int>& own_f = write ? cap.csw_f : cap.csr_f;
	auto t2_of = [&](int t, int ei, const std::vector<int>& falls) {
		return interpolate(t, anchors) +
		       (ei == 1 ? rise_subsample(falls, t) * VDP_PER_SAMPLE : 0.0);
	};
	for (int ei = 0; ei < 2; ++ei) {
		c.t2[ei].reserve(edges[ei]->size());
		// Carry the timestamp along with the cycle position it produced,
		// so that raw_r stays parallel to t2[1] through merge and debounce.
		std::vector<std::pair<double, int>> pairs;
		for (int t : *edges[ei]) pairs.emplace_back(t2_of(t, ei, own_f), t);
		if (both)
			for (int t : *csw[ei])
				pairs.emplace_back(t2_of(t, ei, cap.csw_f), t);
		std::sort(pairs.begin(), pairs.end());
		std::vector<int> rr;
		for (auto& [v, t] : pairs) {
			c.t2[ei].push_back(v);
			rr.push_back(t);
		}
		std::vector<double> raw = c.t2[ei];
		debounce_t2(c.t2[ei], CSX_MIN_GAP, &rr);
		if (ei == 1) c.raw_r = std::move(rr);
		if (CS_DUMP && ei == 1) {
			std::cout << c.name << ": anchors " << anchors.size()
				  << ", raw /CSx edges " << raw.size() << ", after debounce("
				  << CSX_MIN_GAP << ") " << c.t2[ei].size() << "\n";
			int bad = 0;
			for (size_t k = 1; k < anchors.size(); ++k)
				if (anchors[k].t_vdp <= anchors[k - 1].t_vdp ||
				    anchors[k].t_vcd <= anchors[k - 1].t_vcd) ++bad;
			std::cout << "  non-monotonic anchor steps: " << bad << "\n";
			// The slope has to be the clock ratio everywhere. A step that
			// is off means a refresh got the wrong VDP time, which warps
			// t2 locally and can push real edges under the debounce gap.
			for (size_t k = 1; k < anchors.size(); ++k) {
				double dv = anchors[k].t_vdp - anchors[k - 1].t_vdp;
				double ds = anchors[k].t_vcd - anchors[k - 1].t_vcd;
				double want = ds * VDP_PER_UNIT;
				if (std::abs(dv - want) < 0.03 * want) continue;
				std::cout << "  anchor step " << k << ": " << ds * 1e-4
					  << " us of wall time carries " << dv
					  << " VDP cycles, should be " << want << "\n";
			}
			for (size_t k = 1; k < raw.size(); ++k) {
				double d = raw[k] - raw[k - 1];
				if (d >= CSX_MIN_GAP) continue;
				std::cout << "  dropped edge " << k << ": t2 " << raw[k]
					  << ", previous " << raw[k - 1] << ", gap " << d << "\n";
			}
		}
	}
	c.raw_f[0] = cap.csr_f;
	c.raw_f[1] = cap.csw_f;
	const std::vector<int>* csr[2] = {&cap.csr_f, &cap.csr_r};
	for (int ei = 0; ei < 2; ++ei) {
		for (int t : *csr[ei]) c.t2r[ei].push_back(t2_of(t, ei, cap.csr_f));
		debounce_t2(c.t2r[ei], CSX_MIN_GAP);
		for (int t : *csw[ei]) c.t2w[ei].push_back(t2_of(t, ei, cap.csw_f));
		debounce_t2(c.t2w[ei], CSX_MIN_GAP);
	}
	return c;
}

static std::vector<int> make_posts(
	const std::vector<double>& t2s, int delta, const std::vector<int>& wait,
	int tmin, int tmax)
{
	std::vector<int> posts;
	posts.reserve(t2s.size());
	for (double t2 : t2s) {
		int T = int(std::floor(t2)) + delta;
		int S0 = first_slot(wait, T);
		if (S0 + BUSY < tmin) continue;
		if (T > tmax + NEED) continue;
		posts.push_back(T);
	}
	std::sort(posts.begin(), posts.end());
	return posts;
}

static std::vector<int> make_posts_eps(
	const std::vector<double>& t2s, double eps, const std::vector<int>& wait,
	int tmin, int tmax)
{
	std::vector<int> posts;
	posts.reserve(t2s.size());
	for (double t2 : t2s) {
		int T = int(std::floor(t2 + eps));
		int S0 = first_slot(wait, T);
		if (S0 + BUSY < tmin) continue;
		if (T > tmax + NEED) continue;
		posts.push_back(T);
	}
	std::sort(posts.begin(), posts.end());
	return posts;
}

struct FileFit {
	std::string name;
	Mode mode = Mode::DispOff;
	int n = 0;
	int hit = 0;
	int extra = 0;
	int miss = 0;
	int ties = 0;
	int exact_hit = 0;
	int exact_extra = 0;
	int exact_miss = 0;
	int ow = 0;
	int delta = 0;
	int edge_i = 0;
	const char* edge = "?";
};

static FileFit fit_file(
	const Capture& cap,
	const std::vector<int> wait[3],
	int glob_hit[2][DELTA_HI - DELTA_LO + 1],
	int glob_extra[2][DELTA_HI - DELTA_LO + 1],
	int glob_miss[2][DELTA_HI - DELTA_LO + 1],
	int glob_n[2][DELTA_HI - DELTA_LO + 1])
{
	const char* edge_name[2] = {"fall", "rise"};
	const auto& w = wait[mode_index(cap.mode)];
	FileFit best;
	best.name = cap.name;
	best.mode = cap.mode;
	best.n = int(cap.obs.size());
	bool have = false;

	for (int ei = 1; ei >= 0; --ei) {
		for (int d = DELTA_LO; d <= DELTA_HI; ++d) {
			auto posts = make_posts(cap.t2[ei], d, w, cap.tmin, cap.tmax);
			auto sim = vdp_cpu_posts(posts, w, cap.tmin, cap.tmax);
			auto ev = align_d16(sim.pred, cap.obs);
			auto sc = score_d16(ev, cap.tmin, cap.tmax, 0, 0);
			auto ex = score_exact(sim.pred, cap.obs);
			int gi = d - DELTA_LO;
			glob_hit[ei][gi] += sc.hit;
			glob_extra[ei][gi] += sc.extra;
			glob_miss[ei][gi] += sc.miss;
			glob_n[ei][gi] += best.n;

			int err = sc.extra + sc.miss;
			int nerr = have ? best.extra + best.miss : 1 << 30;
			if (!have || sc.hit > best.hit ||
			    (sc.hit == best.hit && err < nerr) ||
			    (sc.hit == best.hit && err == nerr && sim.overwrites < best.ow) ||
			    (sc.hit == best.hit && err == nerr && sim.overwrites == best.ow && d < best.delta)) {
				best.hit = sc.hit;
				best.extra = sc.extra;
				best.miss = sc.miss;
				best.ties = sc.ties;
				best.exact_hit = ex.hit;
				best.exact_extra = ex.extra;
				best.exact_miss = ex.miss;
				best.ow = sim.overwrites;
				best.delta = d;
				best.edge_i = ei;
				best.edge = edge_name[ei];
				have = true;
			}
		}
	}
	return best;
}

static void dump_diag(
	const Capture& cap, const FileFit& fit, const std::vector<int> wait[3])
{
	const auto& w = wait[mode_index(cap.mode)];
	auto posts = make_posts(cap.t2[fit.edge_i], fit.delta, w, cap.tmin, cap.tmax);
	auto sim = vdp_cpu_posts(posts, w, cap.tmin, cap.tmax);
	auto ev = align_d16(sim.pred, cap.obs);

	std::cout << "\n=== DIAG " << cap.name
		  << "  δ=" << fit.delta << " " << fit.edge
		  << "  posts=" << posts.size()
		  << "  CSx=" << cap.t2[fit.edge_i].size()
		  << "  obs=" << cap.obs.size()
		  << "  pred=" << sim.pred.size()
		  << "  ow=" << sim.overwrites
		  << "  holdoff T==S/T==S+1 " << sim.holdoff_dt0
		  << "/" << sim.holdoff_dt1 << " ===\n";

	const char* en[2] = {"fall", "rise"};
	for (int ei = 0; ei < 2; ++ei) {
		std::vector<int> Te;
		for (double t2 : cap.t2[ei]) Te.push_back(int(std::floor(t2)));
		int n72 = 0, n252 = 0, n_other = 0;
		std::cout << "CSx " << en[ei] << " n=" << Te.size() << " other-gaps:\n";
		for (size_t i = 1; i < Te.size(); ++i) {
			int g = Te[i] - Te[i - 1];
			if (g >= 68 && g <= 75) ++n72;
			else if (g >= 240 && g <= 260) ++n252;
			else {
				++n_other;
				std::cout << "    " << i - 1 << "->" << i << "  Δ=" << g
					  << "  t2 " << Te[i - 1] << " -> " << Te[i]
					  << "  row " << (Te[i - 1] % LINE) << " -> " << (Te[i] % LINE)
					  << "\n";
			}
		}
		std::cout << "  ~72: " << n72 << "  ~252: " << n252 << "  other: " << n_other << "\n";
	}

	std::cout << "D16 by edge (best δ):\n";
	for (int ei = 0; ei < 2; ++ei) {
		int bh = -1, be = 0, bm = 0, bd = 0, bow = 0;
		for (int d = DELTA_LO; d <= DELTA_HI; ++d) {
			auto p = make_posts(cap.t2[ei], d, w, cap.tmin, cap.tmax);
			auto sm = vdp_cpu_posts(p, w, cap.tmin, cap.tmax);
			auto ev2 = align_d16(sm.pred, cap.obs);
			auto sc = score_d16(ev2, cap.tmin, cap.tmax, 0, 0);
			if (sc.hit > bh || (sc.hit == bh && sc.extra + sc.miss < be + bm)) {
				bh = sc.hit; be = sc.extra; bm = sc.miss; bd = d; bow = sm.overwrites;
			}
		}
		std::cout << "  " << en[ei] << "  " << bh << '/' << cap.obs.size()
			  << " extra " << be << " miss " << bm << "  δ=" << bd
			  << " ow=" << bow << "  nCSx=" << cap.t2[ei].size() << "\n";
	}

	std::cout << "\nLeftovers with arming T and D16 neighbors:\n";
	// map pred index via walking align
	size_t ip = 0, jo = 0;
	for (auto& e : ev) {
		if (e.kind == Align::Hit || e.kind == Align::Tie) {
			if (e.kind == Align::Tie) {
				const Serv& p = sim.pred[ip];
				std::cout << "  TIE obs " << e.t << " pred " << p.S
					  << " T=" << p.T << " Sm=" << p.Sm << " Sp=" << p.Sp << "\n";
			}
			++ip;
			++jo;
			continue;
		}
		if (e.kind == Align::Extra) {
			const Serv& p = sim.pred[ip];
			std::cout << "  EXTRA pred " << p.S << " row " << (p.S % LINE)
				  << "  T=" << p.T << " dist=" << (p.S - p.T)
				  << "  Sm=" << p.Sm << " Sp=" << p.Sp
				  << "  obs~ " << (jo < cap.obs.size() ? cap.obs[jo] : -1) << "\n";
			++ip;
		} else {
			int o = e.t;
			int Tnear = -1;
			for (int T : posts) if (T <= o) Tnear = T;
			int S0 = (Tnear >= 0) ? first_slot(w, Tnear) : -1;
			std::cout << "  MISS  obs " << o << " row " << (o % LINE)
				  << "  lastT<=obs " << Tnear
				  << "  first_slot(T)=" << S0
				  << "  Sm=" << (Tnear >= 0 ? first_slot(w, Tnear - 1) : -1)
				  << "  pred~ " << (ip < sim.pred.size() ? sim.pred[ip].S : -1)
				  << "\n";
			++jo;
		}
	}

	int t0 = -1;
	for (auto& e : ev) {
		if (e.kind == Align::Extra || e.kind == Align::Miss) {
			t0 = e.t;
			break;
		}
	}
	if (t0 < 0) return;
	std::cout << "\nTimeline around first leftover RAS " << t0 << " ±200:\n";
	std::cout << "  posts:\n";
	for (int T : posts) {
		if (T < t0 - 250 || T > t0 + 250) continue;
		std::cout << "    T=" << T << " row " << (T % LINE)
			  << "  -> S " << first_slot(w, T) << " row " << (first_slot(w, T) % LINE)
			  << "  Sm " << first_slot(w, T - 1) << "\n";
	}
	std::cout << "  obs:\n";
	for (int o : cap.obs) {
		if (o < t0 - 250 || o > t0 + 400) continue;
		std::cout << "    " << o << " row " << (o % LINE) << "\n";
	}
	std::cout << "  pred:\n";
	for (auto& p : sim.pred) {
		if (p.S < t0 - 250 || p.S > t0 + 400) continue;
		std::cout << "    " << p.S << " row " << (p.S % LINE)
			  << " T=" << p.T << "\n";
	}
}

static void dump_hmmv_diag(
	const Capture& cap, const FileFit& fit, const std::vector<int> wait[3],
	const std::unordered_set<int>& occ_obs, const std::unordered_set<int>& occ_pred)
{
	std::vector<int> obs_e;
	obs_e.reserve(cap.eng.size());
	for (auto& e : cap.eng) obs_e.push_back(e.ras);
	Cmd cmd = cmd_of(cap.name);
	Variant var = parse_variant(cap.name);
	auto p0 = predict_engine(cap.eng, cap.mode, cmd, {}, var);
	auto po = predict_engine(cap.eng, cap.mode, cmd, occ_obs, var);
	auto pp = predict_engine(cap.eng, cap.mode, cmd, occ_pred, var);
	auto s0 = score_times(p0.pred, obs_e);
	auto so = score_times(po.pred, obs_e);
	auto sp = score_times(pp.pred, obs_e);

	std::cout << "\n=== DIAG " << cap.name
		  << "  CPU δ=" << fit.delta << " " << fit.edge
		  << "  cpu " << fit.hit << '/' << fit.n
		  << " extra " << fit.extra << " miss " << fit.miss
		  << "  eng n=" << obs_e.size()
		  << "  skip-obs " << po.skips
		  << "  skip-pred " << pp.skips << " ===\n";
	std::cout << "  eng none     " << s0.hit << '/' << obs_e.size()
		  << " extra " << s0.extra << " miss " << s0.miss
		  << "  unk " << p0.unknown << "\n";
	std::cout << "  eng occ-RAS  " << so.hit << '/' << obs_e.size()
		  << " extra " << so.extra << " miss " << so.miss << "\n";
	std::cout << "  eng occ-pend " << sp.hit << '/' << obs_e.size()
		  << " extra " << sp.extra << " miss " << sp.miss << "\n";

	const SlotTable& tab = CMD_TABLE[mode_index(cap.mode)];
	auto show_first = [&](const char* label, const std::vector<int>& pred) {
		size_t i = 0, j = 0;
		int shown = 0;
		while ((i < pred.size() || j < obs_e.size()) && shown < 8) {
			if (i < pred.size() && j < obs_e.size() && pred[i] == obs_e[j]) {
				++i; ++j; continue;
			}
			if (shown == 0) std::cout << "  first leftovers (" << label << "):\n";
			if (i < pred.size() && (j == obs_e.size() || pred[i] < obs_e[j])) {
				std::cout << "    extra RAS " << pred[i]
					  << " row " << (pred[i] % LINE) << "\n";
				++i;
			} else {
				std::cout << "    miss  RAS " << obs_e[j]
					  << " row " << (obs_e[j] % LINE) << "\n";
				++j;
			}
			++shown;
		}
		if (shown == 0) return;
		// step that first diverged
		int k = 1;
		for (; k < int(std::min(pred.size(), obs_e.size())); ++k) {
			if (pred[k] != obs_e[k]) break;
		}
		if (k >= int(cap.eng.size()) || k < 1) return;
		int last = pred[k - 1];
		int delta = step_delta(cmd, cap.mode, cap.eng, k, last, parse_variant(cap.name));
		int cand = (delta >= 0) ? next_cmd_slot(last, delta, tab, {}) : -1;
		std::cout << "    step " << (k - 1) << "->" << k
			  << " last=" << last << " row " << (last % LINE)
			  << "  Δ=" << delta
			  << "  cand=" << cand
			  << (cand >= 0 ? (std::string(" row ") + std::to_string(cand % LINE)) : "")
			  << (occ_obs.count(cand) ? "  CPU-obs" : "")
			  << (occ_pred.count(cand) ? "  CPU-pred" : "")
			  << "  obs=" << obs_e[k] << " pred=" << pred[k] << "\n";
	};
	show_first("occ-obs", po.pred);
	show_first("occ-pred", pp.pred);
	(void)wait;
}

struct HypRow {
	int need = 0;
	int ei = 0;
	int d = 0;
	int cpu_h = 0, cpu_e = 0, cpu_m = 0;
	int eng_h = 0, eng_e = 0, eng_m = 0;
	int skips = 0;
};

static std::vector<std::pair<int, int>> ts_from_align(
	const CpuSim& sim, const std::vector<Align>& ev)
{
	std::vector<std::pair<int, int>> ts;
	size_t ip = 0;
	for (auto& e : ev) {
		if (e.kind == Align::Hit || e.kind == Align::Tie) {
			ts.push_back({sim.pred[ip].T, e.t});
			++ip;
		} else if (e.kind == Align::Extra) {
			ts.push_back({sim.pred[ip].T, sim.pred[ip].S});
			++ip;
		}
	}
	return ts;
}

// /CSR→arbiter delay (δ) × CPU D16 (NEED). Command skipped while CPU pending (T<=C<S).
static int run_hyp_search(const fs::path& slots_dir, const fs::path& vcd_dir,
			  const std::string& filter)
{
	std::vector<fs::path> files;
	for (const auto& ent : fs::directory_iterator(slots_dir)) {
		if (!ent.is_regular_file()) continue;
		auto name = ent.path().filename().string();
		if (!is_cpu_file(name, "all")) continue;
		if (name.find("-stop-") != std::string::npos) continue;
		if (!filter.empty() && name.find(filter) == std::string::npos) continue;
		files.push_back(ent.path());
	}
	std::sort(files.begin(), files.end());
	if (files.empty()) {
		std::cerr << "no files matching --hyp-search " << filter << " in " << slots_dir << "\n";
		return 1;
	}

	constexpr int NEED_LO = 1, NEED_HI = 32;
	const char* en[2] = {"fall", "rise"};
	auto print_row = [&](const HypRow& r, int ncpu, int neng) {
		std::cout << "  NEED=" << r.need
			  << "  δ=" << r.d << " " << en[r.ei]
			  << "  cpu " << r.cpu_h << '/' << ncpu
			  << " extra " << r.cpu_e << " miss " << r.cpu_m
			  << "  eng " << r.eng_h << '/' << neng
			  << " extra " << r.eng_e << " miss " << r.eng_m
			  << "  skip " << r.skips << "\n";
	};

	for (const auto& txt : files) {
		fs::path vcd = vcd_dir / (txt.stem().string() + ".vcd");
		Capture cap;
		try {
			cap = load_capture(txt, vcd);
		} catch (const std::exception& ex) {
			std::cout << txt.filename().string() << "  SKIP " << ex.what() << "\n";
			continue;
		}
		if (cap.eng.empty() || cap.obs.empty()) {
			std::cout << cap.name << "  SKIP no eng/cpu\n";
			continue;
		}
		Cmd cmd = cmd_of(cap.name);
		Variant var = parse_variant(cap.name);
		std::vector<int> obs_e;
		obs_e.reserve(cap.eng.size());
		for (auto& e : cap.eng) obs_e.push_back(e.ras);
		const SlotTable& tab = CMD_TABLE[mode_index(cap.mode)];
		auto cs = cpu_slots_of(tab);
		const int ncpu = int(cap.obs.size());
		const int neng = int(obs_e.size());

		std::cout << "\n=== HYP-SEARCH " << cap.name
			  << "  eng n=" << neng << "  cpu n=" << ncpu << " ===\n";
		std::cout << "occ: every command slot C with T<=C<S (CPU pending blocks command)\n";
		std::cout << "δ = delay from /CSR|/CSW edge to arbiter (T=floor(t2)+δ)\n";

		HypRow best{};
		bool have = false;
		std::vector<HypRow> perfect;
		std::vector<HypRow> n16_cpu_ok;
		HypRow best16[2]{};
		bool have16[2] = {false, false};

		for (int need = NEED_LO; need <= NEED_HI; ++need) {
			std::vector<int> w;
			try {
				w = make_wait(cs, tab, need);
			} catch (const std::exception&) {
				std::cout << "  NEED=" << need << "  skip (no wait LUT)\n";
				continue;
			}
			for (int ei = 0; ei < 2; ++ei) {
				if (cap.t2[ei].empty()) continue;
				for (int d = DELTA_LO; d <= DELTA_HI; ++d) {
					auto posts = make_posts(cap.t2[ei], d, w, cap.tmin, cap.tmax);
					auto sim = vdp_cpu_posts(posts, w, cap.tmin, cap.tmax);
					auto ev = align_d16(sim.pred, cap.obs);
					auto sc = score_d16(ev, cap.tmin, cap.tmax, 0, 0);
					auto ts = ts_from_align(sim, ev);
					auto occ = occupy_pending_window(ts, cap.mode);
					auto pe = predict_engine(cap.eng, cap.mode, cmd, occ, var);
					auto se = score_times(pe.pred, obs_e);
					HypRow r{need, ei, d, sc.hit, sc.extra, sc.miss,
						 se.hit, se.extra, se.miss, pe.skips};
					if (se.extra == 0 && se.miss == 0)
						perfect.push_back(r);
					int err = se.extra + se.miss;
					int berr = have ? best.eng_e + best.eng_m : 1 << 30;
					if (!have || se.hit > best.eng_h ||
					    (se.hit == best.eng_h && err < berr) ||
					    (se.hit == best.eng_h && err == berr && sc.hit > best.cpu_h)) {
						best = r;
						have = true;
					}
					if (need == 16) {
						int e16 = have16[ei] ? best16[ei].eng_e + best16[ei].eng_m : 1 << 30;
						if (!have16[ei] || se.hit > best16[ei].eng_h ||
						    (se.hit == best16[ei].eng_h && err < e16) ||
						    (se.hit == best16[ei].eng_h && err == e16 && sc.hit > best16[ei].cpu_h)) {
							best16[ei] = r;
							have16[ei] = true;
						}
						if (sc.extra == 0 && sc.miss == 0)
							n16_cpu_ok.push_back(r);
					}
				}
			}
		}

		std::cout << "best engine (NEED 1..32, δ 0..40, fall/rise):\n";
		if (have) print_row(best, ncpu, neng);
		if (!perfect.empty()) {
			std::cout << "engine 100% at " << perfect.size() << " grid points (first 20):\n";
			for (size_t i = 0; i < perfect.size() && i < 20; ++i)
				print_row(perfect[i], ncpu, neng);
		} else {
			std::cout << "no engine 100% on this grid\n";
		}
		std::cout << "NEED=16 best per /CSR edge:\n";
		for (int ei = 0; ei < 2; ++ei) {
			if (have16[ei]) print_row(best16[ei], ncpu, neng);
		}
		std::cout << "NEED=16 and CPU D16-perfect:\n";
		if (n16_cpu_ok.empty()) std::cout << "  (none)\n";
		else {
			HypRow b = n16_cpu_ok.front();
			for (auto& r : n16_cpu_ok) {
				print_row(r, ncpu, neng);
				if (r.eng_h > b.eng_h) b = r;
			}
			std::cout << "  best among these: eng " << b.eng_h << '/' << neng << "\n";
		}
	}
	return 0;
}

static void add_packed_pending(
	std::unordered_set<int>& occ,
	const std::vector<std::pair<int, int>>& ts, Mode mode, int need_lo)
{
	auto packed = packed_slots_of(CMD_TABLE[mode_index(mode)]);
	if (packed.empty()) return;
	for (auto [T, S] : ts) {
		int base = S - mod_line(S);
		for (int wrap = -1; wrap <= 1; ++wrap) {
			for (int r : packed) {
				int C = base + wrap * LINE + r;
				if (T <= C - need_lo && C < S) occ.insert(C);
			}
		}
	}
}

static void print_first_step(
	const char* label, const std::vector<int>& pred, const std::vector<int>& obs,
	const Capture& cap, const std::unordered_set<int>& occ)
{
	int k = 1;
	int n = int(std::min(pred.size(), obs.size()));
	for (; k < n; ++k) if (pred[k] != obs[k]) break;
	if (k >= int(cap.eng.size()) || k < 1) {
		std::cout << "    " << label << "  (no leftover)\n";
		return;
	}
	Cmd cmd = cmd_of(cap.name);
	int last = pred[k - 1];
	int delta = step_delta(cmd, cap.mode, cap.eng, k, last, parse_variant(cap.name));
	const SlotTable& tab = CMD_TABLE[mode_index(cap.mode)];
	int cand = (delta >= 0) ? next_cmd_slot(last, delta, tab, occ) : -1;
	int free = (delta >= 0) ? next_cmd_slot(last, delta, tab, {}) : -1;
	std::cout << "    " << label
		  << "  last=" << last << " row " << (last % LINE)
		  << "  Δ=" << delta
		  << "  free=" << free << " row " << (free % LINE)
		  << "  pred=" << pred[k] << " row " << (pred[k] % LINE)
		  << "  obs=" << obs[k] << " row " << (obs[k] % LINE)
		  << (occ.count(free) ? "  free-blocked" : "")
		  << (occ.count(obs[k]) ? "  obs-blocked?" : "")
		  << "\n";
	(void)cand;
}

// Treat observed R.. as idle. Block packed +6 for the command only from CPU
// pending (T,S), not from the dummy tag.
static int run_idle_rdd(const fs::path& slots_dir, const fs::path& vcd_dir)
{
	const char* names[] = {
		"scr5-sprOff-ymmm-rdCpu-1.txt",
		"scr5-sprOff-ymmm-rdCpu-2.txt",
		"scr5-sprOff-hmmv-wrCpu-1.txt",
		"scr5-sprOff-hmmv-rdCpu-3.txt",
		"scr5-sprOff-lmmm-rdCpu-1.txt",
		"scr5-sprOff-lmmm-wrCpu-2.txt",
		"scr5-sprOff-hmmm-rdCpu-1.txt",
	};
	std::vector<int> wcpu[3];
	for (int m = 0; m < 3; ++m) {
		auto cs = cpu_slots_of(CMD_TABLE[m]);
		wcpu[m] = make_wait(cs, CMD_TABLE[m], NEED);
	}

	std::cout << "R.. treated as IDLE. Packed +6 blocked only if a CPU request is pending.\n";
	std::cout << "  A cpu        observed CPU RAS only\n";
	std::cout << "  B cpu+R..    A plus observed dummy (old occ-R..)\n";
	std::cout << "  C T<=C-16    A plus packed +6 with CPU pending 16 cycles (dummy tag unused)\n";
	std::cout << "  D T<=C       A plus packed +6 with CPU pending at all (idle window too)\n";

	for (const char* name : names) {
		fs::path txt = slots_dir / name;
		if (!fs::exists(txt)) {
			std::cout << name << "  SKIP missing\n";
			continue;
		}
		Capture cap;
		try {
			cap = load_capture(txt, vcd_dir / (txt.stem().string() + ".vcd"));
		} catch (const std::exception& ex) {
			std::cout << name << "  SKIP " << ex.what() << "\n";
			continue;
		}
		Cmd cmd = cmd_of(cap.name);
		Variant var = parse_variant(cap.name);
		std::vector<int> obs_e;
		for (auto& e : cap.eng) obs_e.push_back(e.ras);
		std::unordered_set<int> occ_cpu(cap.obs.begin(), cap.obs.end());
		std::unordered_set<int> occ_rdd = occ_cpu;
		for (int d : cap.dummy) occ_rdd.insert(d);

		const auto& w = wcpu[mode_index(cap.mode)];
		FileFit dummy_fit;
		dummy_fit.name = cap.name;
		dummy_fit.mode = cap.mode;
		dummy_fit.n = int(cap.obs.size());
		int glob_h[2][DELTA_HI - DELTA_LO + 1] = {}, glob_e[2][DELTA_HI - DELTA_LO + 1] = {};
		int glob_m[2][DELTA_HI - DELTA_LO + 1] = {}, glob_n[2][DELTA_HI - DELTA_LO + 1] = {};
		FileFit fit = fit_file(cap, wcpu, glob_h, glob_e, glob_m, glob_n);
		auto posts = make_posts(cap.t2[fit.edge_i], fit.delta, w, cap.tmin, cap.tmax);
		auto sim = vdp_cpu_posts(posts, w, cap.tmin, cap.tmax);
		auto ev = align_d16(sim.pred, cap.obs);
		auto ts = ts_from_align(sim, ev);

		auto occ_d16 = occ_cpu;
		add_packed_pending(occ_d16, ts, cap.mode, NEED);
		auto occ_pend = occ_cpu;
		add_packed_pending(occ_pend, ts, cap.mode, 0);

		auto pa = predict_engine(cap.eng, cap.mode, cmd, occ_cpu, var);
		auto pb = predict_engine(cap.eng, cap.mode, cmd, occ_rdd, var);
		auto pc = predict_engine(cap.eng, cap.mode, cmd, occ_d16, var);
		auto pd = predict_engine(cap.eng, cap.mode, cmd, occ_pend, var);
		auto sa = score_times(pa.pred, obs_e);
		auto sb = score_times(pb.pred, obs_e);
		auto sc = score_times(pc.pred, obs_e);
		auto sd = score_times(pd.pred, obs_e);
		int ne = int(obs_e.size());
		auto cell = [](const Score& s, int n) {
			std::ostringstream o;
			o << s.hit << '/' << n;
			if (s.extra == 0 && s.miss == 0) o << "OK";
			return o.str();
		};
		std::cout << "\n" << cap.name
			  << "  dummy " << cap.dummy.size()
			  << "  CPU δ=" << fit.delta << " " << fit.edge
			  << "  cpu " << fit.hit << '/' << fit.n << "\n";
		std::cout << "  A cpu      " << cell(sa, ne) << " extra " << sa.extra << " miss " << sa.miss << " skip " << pa.skips << "\n";
		std::cout << "  B cpu+R..  " << cell(sb, ne) << " extra " << sb.extra << " miss " << sb.miss << " skip " << pb.skips << "\n";
		std::cout << "  C T<=C-16  " << cell(sc, ne) << " extra " << sc.extra << " miss " << sc.miss << " skip " << pc.skips
			  << "  packed+" << (occ_d16.size() - occ_cpu.size()) << "\n";
		std::cout << "  D T<=C     " << cell(sd, ne) << " extra " << sd.extra << " miss " << sd.miss << " skip " << pd.skips
			  << "  packed+" << (occ_pend.size() - occ_cpu.size()) << "\n";
		print_first_step("A", pa.pred, obs_e, cap, occ_cpu);
		print_first_step("B", pb.pred, obs_e, cap, occ_rdd);
		print_first_step("C", pc.pred, obs_e, cap, occ_d16);
		print_first_step("D", pd.pred, obs_e, cap, occ_pend);

		// How many observed R.. are in C vs D
		int n_d16 = 0, n_pend = 0, n_miss_d16 = 0;
		for (int d : cap.dummy) {
			if (occ_d16.count(d)) ++n_d16;
			else ++n_miss_d16;
			if (occ_pend.count(d)) ++n_pend;
		}
		std::cout << "  observed R.. covered by C " << n_d16 << '/' << cap.dummy.size()
			  << "  by D " << n_pend << '/' << cap.dummy.size()
			  << "  R.. not predicted by C " << n_miss_d16 << "\n";
	}
	return 0;
}

static std::pair<int, int> cpu_t_window(const std::vector<int>& wait, int S)
{
	int tmin = 1, tmax = 0;
	for (int T = S - LINE; T <= S; ++T) {
		if (first_slot(wait, T) == S) {
			if (tmin > tmax) tmin = T;
			tmax = T;
		}
	}
	return {tmin, tmax};
}

static void occupy_packed_in(std::unordered_set<int>& occ, int lo, int S, const std::vector<int>& packed)
{
	int base = S - mod_line(S);
	for (int wrap = -2; wrap <= 1; ++wrap) {
		for (int r : packed) {
			int C = base + wrap * LINE + r;
			if (lo <= C && C < S) occ.insert(C);
		}
	}
}

// Packed-slot mismatches without trusting /CSR: T-window from observed CPU RAS.
static int run_mismatch(const fs::path& slots_dir)
{
	std::vector<int> cpu_slot[3], packed_slot[3], cpu_wait[3];
	std::unordered_set<int> packed_set[3];
	for (int m = 0; m < 3; ++m) {
		cpu_slot[m] = cpu_slots_of(CMD_TABLE[m]);
		packed_slot[m] = packed_slots_of(CMD_TABLE[m]);
		packed_set[m] = {packed_slot[m].begin(), packed_slot[m].end()};
		cpu_wait[m] = make_wait(cpu_slot[m], CMD_TABLE[m], NEED);
	}

	std::vector<fs::path> files;
	for (const auto& ent : fs::directory_iterator(slots_dir)) {
		if (!ent.is_regular_file()) continue;
		auto name = ent.path().filename().string();
		if (is_cpu_file(name, "all") && name.find("-stop-") == std::string::npos)
			files.push_back(ent.path());
	}
	std::sort(files.begin(), files.end());

	struct Row {
		int n = 0, ok = 0, hit = 0, tot = 0;
	};
	Row base[6][3], p1[6][3], p2[6][3], rd[6][3], p3[6][3], p4[6][3], p5[6][3], p6[6][3];
	Row p8[6][3], p10[6][3], p11[6][3], p12[6][3], p14[6][3], p15[6][3], p18[6][3];
	Row combo[6][3], combo_p3[6][3], combo_p5only[6][3], combo_p11[6][3], combo_p96[6][3];
	int packed_hit = 0, packed_skip_d = 0, packed_skip_i = 0, packed_skip_cpu = 0;
	int skip_must = 0, skip_maybe = 0, skip_never = 0, skip_nocpu = 0;
	int hit_must = 0, hit_maybe = 0, hit_never = 0, hit_nocpu = 0;
	int dummy_must = 0, dummy_maybe = 0, dummy_never = 0;
	int mh_lastP = 0, mi_lastP = 0, mh_cpu26 = 0, mi_cpu26 = 0, mh_d32 = 0, mi_d32 = 0;
	int n_idle_print = 0;
	std::map<int, int> dum_d, hitm_d, idle_d, hit_all_d;
	int dum_lastP = 0, dum_cpu26 = 0, dum_tight = 0;
	int mh_tight = 0, hit_tight = 0, hit_d32 = 0, hit_cpu26 = 0;
	int mh_slack_cpu26 = 0, dum_slack_cpu26 = 0;
	int p5_left_print = 0, n_dmh_print = 0;
	std::map<std::string, int> dum_key, mh_key;
	std::map<int, int> dum_tmaxc, mh_tmaxc, dum_ctmin, mh_ctmin, dum_ed, mh_ed;

	std::cout << "Packed-cand steps (unconstrained model wants packed +6).\n";
	std::cout << "T-window from next CPU RAS after last engine access (no /CSR).\n";
	std::cout << "  must  = Tmax <= C < S   CPU pending at C for every T mapping to S\n";
	std::cout << "  maybe = Tmin <= C < S   depends on exact T (/CSR-sensitive)\n";
	std::cout << "  never = C < Tmin or C >= S\n\n";
	std::cout << "Idle packed skips (pred empty, not R..) and dummy/maybe-hit samples:\n";

	for (const auto& txt : files) {
		Capture cap = load_txt_only(txt);
		Cmd cmd = cmd_of(cap.name);
		if (cmd == Cmd::Unknown || cap.eng.size() < 2 || cap.obs.empty()) continue;
		int mi = mode_index(cap.mode);
		int ci = int(cmd);
		Variant var = parse_variant(cap.name);
		const auto& wait = cpu_wait[mi];
		const auto& packed = packed_slot[mi];
		std::unordered_set<int> occ_cpu(cap.obs.begin(), cap.obs.end());
		std::unordered_set<int> occ_rdd = occ_cpu;
		for (int d : cap.dummy) occ_rdd.insert(d);
		std::unordered_set<int> occ_p1 = occ_cpu, occ_p2 = occ_cpu;
		for (int S : cap.obs) {
			auto [tmin, tmax] = cpu_t_window(wait, S);
			if (tmin > tmax) continue;
			occupy_packed_in(occ_p1, tmin, S, packed);
			occupy_packed_in(occ_p2, tmax, S, packed);
		}

		std::vector<int> obs_e;
		for (auto& e : cap.eng) obs_e.push_back(e.ras);
		auto eval = [&](const std::unordered_set<int>& occ, Row& row) {
			auto p = predict_engine(cap.eng, cap.mode, cmd, occ, var, true);
			auto s = score_times(p.pred, obs_e);
			++row.n;
			if (s.extra == 0 && s.miss == 0) ++row.ok;
			row.hit += s.hit;
			row.tot += int(obs_e.size());
		};
		eval(occ_cpu, base[ci][mi]);
		eval(occ_p1, p1[ci][mi]);
		eval(occ_p2, p2[ci][mi]);
		eval(occ_rdd, rd[ci][mi]);

		const SlotTable& tabmi = CMD_TABLE[mi];
		auto edist = [&](int a, int b) {
			return engine_dist(a, b, tabmi);
		};
		auto run_skip_b = [&](Row& row, const std::unordered_set<int>& bocc, auto should_skip) {
			HmmvPred out;
			out.pred.push_back(cap.eng.front().ras);
			int lastp = cap.eng.front().ras;
			for (size_t k = 1; k < cap.eng.size(); ++k) {
				auto w = step_wait(cmd, cap.mode, cap.eng, int(k), lastp, var);
				int delta = pick_delta(w, lastp, tabmi, bocc, cap.eng[k].ras);
				if (delta < 0) delta = 24 + spr_addend(cap.mode);
				std::unordered_set<int> occ = bocc;
				int cand0 = next_cmd_slot(lastp, delta, tabmi, occ);
				int Snext = -1;
				auto it = std::upper_bound(cap.obs.begin(), cap.obs.end(), lastp);
				if (it != cap.obs.end()) Snext = *it;
				if (should_skip(lastp, cand0, delta, cap.eng[k - 1].kind, cap.eng[k].kind, Snext))
					occ.insert(cand0);
				out.pred.push_back(next_cmd_slot(lastp, delta, tabmi, occ));
				lastp = cap.eng[k].ras;
			}
			auto sc = score_times(out.pred, obs_e);
			++row.n;
			if (sc.extra == 0 && sc.miss == 0) ++row.ok;
			row.hit += sc.hit;
			row.tot += int(obs_e.size());
		};
		auto run_skip = [&](Row& row, auto should_skip) {
			run_skip_b(row, occ_cpu, should_skip);
		};
		run_skip(p3[ci][mi], [&](int lastp, int cand0, int delta, char, char, int) {
			return packed_set[mi].count(mod_line(lastp)) &&
			       packed_set[mi].count(mod_line(cand0)) &&
			       delta == 32 && occ_cpu.count(cand0 + 26);
		});
		run_skip(p4[ci][mi], [&](int lastp, int cand0, int delta, char, char, int) {
			return packed_set[mi].count(mod_line(cand0)) &&
			       edist(lastp, cand0) == delta && occ_cpu.count(cand0 + 26);
		});
		run_skip(p5[ci][mi], [&](int lastp, int cand0, int delta, char, char, int Snext) {
			return packed_set[mi].count(mod_line(lastp)) &&
			       packed_set[mi].count(mod_line(cand0)) &&
			       delta == 32 && Snext > cand0;
		});
		run_skip(p6[ci][mi], [&](int, int cand0, int, char a, char b, int) {
			return packed_set[mi].count(mod_line(cand0)) &&
			       a == 's' && b == 'd' && occ_cpu.count(cand0 + 26);
		});
		run_skip(p8[ci][mi], [&](int, int cand0, int, char, char, int) {
			return packed_set[mi].count(mod_line(cand0)) && occ_cpu.count(cand0 + 26);
		});
		run_skip(p10[ci][mi], [&](int lastp, int cand0, int delta, char, char, int) {
			return packed_set[mi].count(mod_line(cand0)) &&
			       occ_cpu.count(cand0 + 26) &&
			       edist(lastp, cand0) > delta;
		});
		auto cpu_in = [&](int lo, int hi) {
			auto it = std::upper_bound(cap.obs.begin(), cap.obs.end(), lo);
			return it != cap.obs.end() && *it <= hi;
		};
		run_skip(p11[ci][mi], [&](int lastp, int cand0, int delta, char, char, int) {
			return packed_set[mi].count(mod_line(lastp)) &&
			       packed_set[mi].count(mod_line(cand0)) &&
			       delta == 32 && cpu_in(cand0, cand0 + 64);
		});
		run_skip(p12[ci][mi], [&](int lastp, int cand0, int delta, char, char, int) {
			return packed_set[mi].count(mod_line(lastp)) &&
			       packed_set[mi].count(mod_line(cand0)) &&
			       delta == 32;
		});
		run_skip(p14[ci][mi], [&](int lastp, int cand0, int delta, char, char, int) {
			if (packed_set[mi].count(mod_line(lastp)) &&
			    packed_set[mi].count(mod_line(cand0)) &&
			    delta == 32 && cpu_in(cand0, cand0 + 64))
				return true;
			return packed_set[mi].count(mod_line(lastp)) &&
			       packed_set[mi].count(mod_line(cand0)) &&
			       is_nl_delta(delta) && cpu_in(lastp, cand0);
		});
		run_skip(p18[ci][mi], [&](int lastp, int cand0, int delta, char, char, int Snext) {
			bool lastP = packed_set[mi].count(mod_line(lastp));
			bool candP = packed_set[mi].count(mod_line(cand0));
			if (lastP && candP && delta == 32 && Snext > cand0) return true;
			return lastP && candP && occ_cpu.count(cand0 + 26) && delta == 60;
		});
		run_skip(p15[ci][mi], [&](int lastp, int cand0, int delta, char, char, int Snext) {
			// P19 reused p15 slot: P5 + HMMM newline idle
			bool lastP = packed_set[mi].count(mod_line(lastp));
			bool candP = packed_set[mi].count(mod_line(cand0));
			if (lastP && candP && delta == 32 && Snext > cand0) return true;
			return lastP && candP && is_nl_delta(delta) && cpu_in(lastp, cand0);
		});
		run_skip_b(combo[ci][mi], occ_rdd, [&](int lastp, int cand0, int delta, char, char, int Snext) {
			bool lastP = packed_set[mi].count(mod_line(lastp));
			bool candP = packed_set[mi].count(mod_line(cand0));
			if (lastP && candP && delta == 32 && Snext > cand0) return true;
			return lastP && candP && is_nl_delta(delta) && cpu_in(lastp, cand0);
		});
		run_skip_b(combo_p3[ci][mi], occ_rdd, [&](int lastp, int cand0, int delta, char, char, int) {
			bool lastP = packed_set[mi].count(mod_line(lastp));
			bool candP = packed_set[mi].count(mod_line(cand0));
			if (lastP && candP && delta == 32 && occ_cpu.count(cand0 + 26)) return true;
			return lastP && candP && is_nl_delta(delta) && cpu_in(lastp, cand0);
		});
		run_skip_b(combo_p5only[ci][mi], occ_rdd, [&](int lastp, int cand0, int delta, char, char, int Snext) {
			return packed_set[mi].count(mod_line(lastp)) &&
			       packed_set[mi].count(mod_line(cand0)) &&
			       delta == 32 && Snext > cand0;
		});
		run_skip_b(combo_p11[ci][mi], occ_rdd, [&](int lastp, int cand0, int delta, char, char, int) {
			bool lastP = packed_set[mi].count(mod_line(lastp));
			bool candP = packed_set[mi].count(mod_line(cand0));
			if (lastP && candP && delta == 32 && cpu_in(cand0, cand0 + 64)) return true;
			return lastP && candP && is_nl_delta(delta) && cpu_in(lastp, cand0);
		});
		run_skip_b(combo_p96[ci][mi], occ_rdd, [&](int lastp, int cand0, int delta, char, char, int) {
			bool lastP = packed_set[mi].count(mod_line(lastp));
			bool candP = packed_set[mi].count(mod_line(cand0));
			if (lastP && candP && delta == 32 && cpu_in(cand0, cand0 + 96)) return true;
			return lastP && candP && is_nl_delta(delta) && cpu_in(lastp, cand0);
		});
		if (cmd == Cmd::Lmmm && cap.mode == Mode::SprOff) {
			HmmvPred out;
			out.pred.push_back(cap.eng.front().ras);
			int lastp = cap.eng.front().ras;
			for (size_t k = 1; k < cap.eng.size(); ++k) {
				auto w = step_wait(cmd, cap.mode, cap.eng, int(k), lastp, var);
				int delta = pick_delta(w, lastp, tabmi, occ_rdd, cap.eng[k].ras);
				if (delta < 0) delta = 24;
				std::unordered_set<int> occ = occ_rdd;
				int cand0 = next_cmd_slot(lastp, delta, tabmi, occ);
				auto it = std::upper_bound(cap.obs.begin(), cap.obs.end(), lastp);
				int Snext = (it == cap.obs.end()) ? -1 : *it;
				bool lastP = packed_set[mi].count(mod_line(lastp));
				bool candP = packed_set[mi].count(mod_line(cand0));
				if ((lastP && candP && delta == 32 && cpu_in(cand0, cand0 + 64)) ||
				    (lastP && candP && is_nl_delta(delta) && cpu_in(lastp, cand0)))
					occ.insert(cand0);
				int pred = next_cmd_slot(lastp, delta, tabmi, occ);
				if (pred != cap.eng[k].ras) {
					std::cout << "  C11-left " << cap.name
						  << " " << cap.eng[k - 1].kind << "->" << cap.eng[k].kind
						  << " Δ=" << delta
						  << " last=" << lastp << " r" << mod_line(lastp)
						  << (lastP ? "P" : "")
						  << " cand=" << cand0 << " r" << mod_line(cand0)
						  << " pred=" << pred << " obs=" << cap.eng[k].ras
						  << " CPU=" << Snext
						  << " dCPU=" << (Snext > 0 ? Snext - cand0 : -1)
						  << "\n";
				}
				lastp = cap.eng[k].ras;
			}
		}
		if ((cmd == Cmd::Lmmm || cmd == Cmd::Hmmm) && cap.mode == Mode::SprOff) {
			HmmvPred out;
			out.pred.push_back(cap.eng.front().ras);
			int lastp = cap.eng.front().ras;
			int nmiss = 0;
			for (size_t k = 1; k < cap.eng.size(); ++k) {
				auto w = step_wait(cmd, cap.mode, cap.eng, int(k), lastp, var);
				int delta = pick_delta(w, lastp, tabmi, occ_rdd, cap.eng[k].ras);
				if (delta < 0) delta = 24;
				std::unordered_set<int> occ = occ_rdd;
				int cand0 = next_cmd_slot(lastp, delta, tabmi, occ);
				auto it = std::upper_bound(cap.obs.begin(), cap.obs.end(), lastp);
				int Snext = (it == cap.obs.end()) ? -1 : *it;
				bool lastP = packed_set[mi].count(mod_line(lastp));
				bool candP = packed_set[mi].count(mod_line(cand0));
				if ((lastP && candP && delta == 32 && Snext > cand0) ||
				    (lastP && candP && is_nl_delta(delta) && cpu_in(lastp, cand0)))
					occ.insert(cand0);
				int pred = next_cmd_slot(lastp, delta, tabmi, occ);
				if (pred != cap.eng[k].ras && nmiss < 3) {
					++nmiss;
					std::cout << "  combo-left " << cap.name
						  << " " << cap.eng[k - 1].kind << "->" << cap.eng[k].kind
						  << " Δ=" << delta
						  << " last=" << lastp << " r" << mod_line(lastp)
						  << (lastP ? "P" : "")
						  << " cand=" << cand0 << " r" << mod_line(cand0)
						  << " pred=" << pred << " r" << mod_line(pred)
						  << " obs=" << cap.eng[k].ras << " r" << mod_line(cap.eng[k].ras)
						  << " CPU=" << Snext << "\n";
				}
				lastp = cap.eng[k].ras;
			}
		}

		if (cmd == Cmd::Lmmm && cap.mode == Mode::SprOff && p5_left_print < 20) {
			HmmvPred out;
			out.pred.push_back(cap.eng.front().ras);
			int lastp = cap.eng.front().ras;
			for (size_t k = 1; k < cap.eng.size(); ++k) {
				auto w = step_wait(cmd, cap.mode, cap.eng, int(k), lastp, var);
				int delta = pick_delta(w, lastp, tabmi, occ_cpu, cap.eng[k].ras);
				if (delta < 0) delta = 24;
				std::unordered_set<int> occ = occ_cpu;
				int cand0 = next_cmd_slot(lastp, delta, tabmi, occ);
				auto it = std::upper_bound(cap.obs.begin(), cap.obs.end(), lastp);
				int Snext = (it == cap.obs.end()) ? -1 : *it;
				if (packed_set[mi].count(mod_line(lastp)) &&
				    packed_set[mi].count(mod_line(cand0)) &&
				    delta == 32 && Snext > cand0)
					occ.insert(cand0);
				int pred = next_cmd_slot(lastp, delta, tabmi, occ);
				if (pred != cap.eng[k].ras && p5_left_print < 20) {
					++p5_left_print;
					std::cout << "  P5-left " << cap.name
						  << " " << cap.eng[k - 1].kind << "->" << cap.eng[k].kind
						  << " Δ=" << delta
						  << " last=" << lastp << " r" << mod_line(lastp)
						  << " cand=" << cand0 << " r" << mod_line(cand0)
						  << " pred=" << pred << " r" << mod_line(pred)
						  << " obs=" << cap.eng[k].ras << " r" << mod_line(cap.eng[k].ras)
						  << " CPU=" << Snext << "\n";
				}
				lastp = cap.eng[k].ras;
			}
		}

		std::unordered_set<int> dummy_set(cap.dummy.begin(), cap.dummy.end());
		int last = cap.eng.front().ras;
		for (size_t k = 1; k < cap.eng.size(); ++k) {
			auto w = step_wait(cmd, cap.mode, cap.eng, int(k), last, var);
			int delta = pick_delta(w, last, CMD_TABLE[mi], occ_cpu, cap.eng[k].ras);
			if (delta < 0) delta = 24 + spr_addend(cap.mode);
			int cand = next_cmd_slot(last, delta, CMD_TABLE[mi], occ_cpu);
			int obs = cap.eng[k].ras;
			if (packed_set[mi].count(mod_line(cand))) {
				auto it = std::upper_bound(cap.obs.begin(), cap.obs.end(), last);
				int S = (it == cap.obs.end()) ? -1 : *it;
				int tmin = 1, tmax = 0;
				int rel = 3; // nocpu
				if (S >= 0) {
					auto tw = cpu_t_window(wait, S);
					tmin = tw.first;
					tmax = tw.second;
					if (tmin > tmax) rel = 3;
					else if (cand >= S || cand < tmin) rel = 2; // never
					else if (tmax <= cand) rel = 0; // must
					else rel = 1; // maybe
				}
				bool dummy = dummy_set.count(cand);
				bool hit = cand == obs;
				bool lastP = packed_set[mi].count(mod_line(last));
				bool tight = edist(last, cand) == delta;
				bool cpu26 = S == cand + 26;
				if (hit) {
					++packed_hit;
					hit_all_d[delta]++;
					if (tight) ++hit_tight;
					if (delta == 32) ++hit_d32;
					if (cpu26) ++hit_cpu26;
					if (rel == 1) hitm_d[delta]++;
					if (rel == 0) ++hit_must;
					else if (rel == 1) {
						++hit_maybe;
						if (lastP) ++mh_lastP;
						if (delta == 32) ++mh_d32;
						if (cpu26) ++mh_cpu26;
						if (tight) ++mh_tight;
						if (!tight && cpu26) ++mh_slack_cpu26;
						mh_tmaxc[tmax - cand]++;
						mh_ctmin[cand - tmin]++;
						mh_ed[edist(last, cand) - delta]++;
						mh_key[std::string(cmd_name(cmd)) + " " +
						       cap.eng[k - 1].kind + std::string("->") + cap.eng[k].kind +
						       " Δ" + std::to_string(delta) +
						       (lastP ? " lastP" : " lastC") +
						       (cpu26 ? " cpu26" : "") +
						       (cap.name.find("rdCpu") != std::string::npos ? " rd" : " wr") +
						       " r" + std::to_string(mod_line(cand))]++;
					}
					else if (rel == 2) ++hit_never;
					else ++hit_nocpu;
				} else if (dummy) {
					++packed_skip_d;
					dum_d[delta]++;
					if (lastP) ++dum_lastP;
					if (cpu26) ++dum_cpu26;
					if (tight) ++dum_tight;
					if (!tight && cpu26) ++dum_slack_cpu26;
					dum_tmaxc[tmax - cand]++;
					dum_ctmin[cand - tmin]++;
					dum_ed[edist(last, cand) - delta]++;
					dum_key[std::string(cmd_name(cmd)) + " " +
					        cap.eng[k - 1].kind + std::string("->") + cap.eng[k].kind +
					        " Δ" + std::to_string(delta) +
					        (lastP ? " lastP" : " lastC") +
					        (cpu26 ? " cpu26" : "") +
					        (cap.name.find("rdCpu") != std::string::npos ? " rd" : " wr") +
					        " r" + std::to_string(mod_line(cand))]++;
					if (rel == 0) ++dummy_must;
					else if (rel == 1) ++dummy_maybe;
					else ++dummy_never;
				} else if (occ_cpu.count(cand)) {
					++packed_skip_cpu;
				} else {
					++packed_skip_i;
					idle_d[delta]++;
					if (rel == 0) ++skip_must;
					else if (rel == 1) {
						++skip_maybe;
						if (lastP) ++mi_lastP;
						if (delta == 32) ++mi_d32;
						if (cpu26) ++mi_cpu26;
					}
					else if (rel == 2) ++skip_never;
					else ++skip_nocpu;
					bool p3hit = lastP && delta == 32 && cpu26;
					if (!p3hit && n_idle_print < 40) {
						++n_idle_print;
						const char* rs[] = {"must", "maybe", "never", "nocpu"};
						std::cout << "  leftover-idle " << cap.name
							  << " " << cap.eng[k - 1].kind << "->" << cap.eng[k].kind
							  << " Δ=" << delta
							  << " last=" << last << " r" << mod_line(last)
							  << (lastP ? "P" : "")
							  << " cand=" << cand << " r" << mod_line(cand)
							  << " obs=" << obs << " r" << mod_line(obs)
							  << (packed_set[mi].count(mod_line(obs)) ? "P" : "")
							  << " CPU=" << S
							  << (S >= 0 ? " r" + std::to_string(mod_line(S)) : "")
							  << " Twin=[" << tmin << "," << tmax << "] "
							  << rs[rel]
							  << (tight ? " tight" : "")
							  << "\n";
					}
				}
			}
			last = obs;
		}
	}

	std::cout << "\nPacked-cand totals: hit " << packed_hit
		  << "  skip-R.. " << packed_skip_d
		  << "  skip-idle " << packed_skip_i
		  << "  skip-cpu " << packed_skip_cpu << "\n";
	std::cout << "  hits   must/maybe/never/nocpu " << hit_must << '/' << hit_maybe << '/'
		  << hit_never << '/' << hit_nocpu << "\n";
	std::cout << "  skip-R.. must/maybe/never " << dummy_must << '/' << dummy_maybe << '/'
		  << dummy_never << "\n";
	std::cout << "  maybe-hit  lastPacked=" << mh_lastP << " Δ32=" << mh_d32
		  << " CPU=cand+26=" << mh_cpu26 << "/" << hit_maybe << "\n";
	std::cout << "  maybe-idle lastPacked=" << mi_lastP << " Δ32=" << mi_d32
		  << " CPU=cand+26=" << mi_cpu26 << "/" << skip_maybe << "\n";
	std::cout << "  dummy     lastPacked=" << dum_lastP << " CPU+26=" << dum_cpu26
		  << " tight=" << dum_tight << "/" << packed_skip_d << "\n";
	auto dump_hist = [&](const char* title, const std::map<int, int>& h) {
		std::cout << "  " << title;
		int n = 0;
		for (auto [d, c] : h) {
			if (n++ >= 16) {
				std::cout << " ...";
				break;
			}
			std::cout << " Δ" << d << "=" << c;
		}
		std::cout << "\n";
	};
	dump_hist("dummy Δ", dum_d);
	dump_hist("maybe-hit Δ", hitm_d);
	dump_hist("all-hit Δ", hit_all_d);
	dump_hist("idle Δ", idle_d);
	std::cout << "  all-hit tight=" << hit_tight << " Δ32=" << hit_d32
		  << " CPU+26=" << hit_cpu26 << "/" << packed_hit << "\n";
	std::cout << "  maybe-hit tight=" << mh_tight << " slack+CPU26=" << mh_slack_cpu26
		  << "/" << hit_maybe << "\n";
	std::cout << "  dummy slack+CPU26=" << dum_slack_cpu26 << "/" << packed_skip_d << "\n";
	dump_hist("dummy tmax-C", dum_tmaxc);
	dump_hist("maybe-hit tmax-C", mh_tmaxc);
	dump_hist("dummy C-tmin", dum_ctmin);
	dump_hist("maybe-hit C-tmin", mh_ctmin);
	dump_hist("dummy slack edist-Δ", dum_ed);
	dump_hist("maybe-hit slack edist-Δ", mh_ed);
	std::cout << "  dummy keys:\n";
	for (auto& [k, c] : dum_key) std::cout << "    " << k << "  " << c << "\n";
	std::cout << "  maybe-hit keys:\n";
	for (auto& [k, c] : mh_key) std::cout << "    " << k << "  " << c << "\n";

	auto dump = [&](const char* title, Row a[6][3]) {
		std::cout << "\n" << title << "\n";
		int nf = 0, ok = 0, h = 0, t = 0;
		for (int ci = 0; ci < 6; ++ci) {
			for (int mi = 0; mi < 3; ++mi) {
				if (!a[ci][mi].n) continue;
				nf += a[ci][mi].n;
				ok += a[ci][mi].ok;
				h += a[ci][mi].hit;
				t += a[ci][mi].tot;
				std::cout << "  " << std::left << std::setw(6) << cmd_name(Cmd(ci))
					  << std::setw(8) << mode_name(Mode(mi))
					  << std::right << a[ci][mi].ok << '/' << a[ci][mi].n
					  << "  " << a[ci][mi].hit << '/' << a[ci][mi].tot << "\n";
			}
		}
		std::cout << "  TOTAL " << ok << '/' << nf << " files  " << h << '/' << t << "\n";
	};
	dump("A  occupy CPU RAS only", base);
	dump("P1 occupy packed if Tmin<=C<S (maybe pending)", p1);
	dump("P2 occupy packed if Tmax<=C<S (must pending)", p2);
	dump("P3 skip packed cand if last packed, Δ=32, CPU at cand+26", p3);
	dump("P4 skip packed cand if tight wait (edist==Δ) and CPU at cand+26", p4);
	dump("P5 skip packed cand if last packed, Δ=32, any CPU RAS after cand", p5);
	dump("P6 skip packed cand if s→d and CPU at cand+26", p6);
	dump("P8 skip packed cand if CPU at cand+26 (no other cond)", p8);
	dump("P10 skip packed cand if CPU at cand+26 and wait is slack (edist>Δ)", p10);
	dump("P11 last packed, Δ=32, CPU in (cand, cand+64]", p11);
	dump("P12 last packed, Δ=32 (ignore CPU) — mixed files only", p12);
	dump("P14 P11 plus Δ=128 if CPU already fired in (last, cand]", p14);
	dump("P15 P5 plus Δ=128 if CPU already fired in (last, cand]", p15);
	dump("P18 P5 or (last packed, CPU+26, Δ=60)", p18);
	dump("B  occupy CPU RAS + observed R..", rd);
	dump("C  R.. occupy + P5 + newline idle (dummy oracle + best skip)", combo);
	dump("C3 R.. occupy + P3 (CPU+26) + newline", combo_p3);
	dump("C11 R.. occupy + CPU in (cand, cand+64] Δ=32 + newline", combo_p11);
	dump("C96 R.. occupy + CPU in (cand, cand+96] Δ=32 + newline", combo_p96);

	// noCpu: skip rules must not fire without CPU, except P12 which ignores CPU.
	{
		std::vector<fs::path> nf;
		for (const auto& ent : fs::directory_iterator(slots_dir)) {
			if (!ent.is_regular_file()) continue;
			auto name = ent.path().filename().string();
			if (is_nocpu_file(name, "all")) nf.push_back(ent.path());
		}
		std::sort(nf.begin(), nf.end());
		Row nbase, np12, np14;
		int p12_fire = 0, p12_eq = 0, p12_ne = 0;
		auto nscore = [&](Row& row, auto should_skip, int* fire, int* feq, int* fne) {
			for (const auto& txt : nf) {
				Capture cap = load_txt_only(txt);
				Cmd cmd = cmd_of(cap.name);
				if (cmd == Cmd::Unknown || cap.eng.size() < 2) continue;
				int mi = mode_index(cap.mode);
				Variant var = parse_variant(cap.name);
				const SlotTable& tab = CMD_TABLE[mi];
				std::vector<int> obs_e;
				for (auto& e : cap.eng) obs_e.push_back(e.ras);
				HmmvPred out;
				out.pred.push_back(cap.eng.front().ras);
				int lastp = cap.eng.front().ras;
				for (size_t k = 1; k < cap.eng.size(); ++k) {
					auto w = step_wait(cmd, cap.mode, cap.eng, int(k), lastp, var);
					int delta = pick_delta(w, lastp, tab, {}, cap.eng[k].ras);
					if (delta < 0) delta = 24 + spr_addend(cap.mode);
					std::unordered_set<int> occ;
					int cand0 = next_cmd_slot(lastp, delta, tab, occ);
					if (should_skip(lastp, cand0, delta, mi)) {
						if (fire) {
							++*fire;
							if (cand0 == cap.eng[k].ras) ++*feq;
							else ++*fne;
						}
						occ.insert(cand0);
					}
					out.pred.push_back(next_cmd_slot(lastp, delta, tab, occ));
					lastp = cap.eng[k].ras;
				}
				auto sc = score_times(out.pred, obs_e);
				++row.n;
				if (sc.extra == 0 && sc.miss == 0) ++row.ok;
				row.hit += sc.hit;
				row.tot += int(obs_e.size());
			}
		};
		nscore(nbase, [](int, int, int, int) { return false; }, nullptr, nullptr, nullptr);
		nscore(np12, [&](int lastp, int cand0, int delta, int mi) {
			return packed_set[mi].count(mod_line(lastp)) &&
			       packed_set[mi].count(mod_line(cand0)) &&
			       delta == 32;
		}, &p12_fire, &p12_eq, &p12_ne);
		nscore(np14, [&](int lastp, int cand0, int delta, int mi) {
			return packed_set[mi].count(mod_line(lastp)) &&
			       packed_set[mi].count(mod_line(cand0)) &&
			       is_nl_delta(delta);
		}, nullptr, nullptr, nullptr);
		std::cout << "\nnoCpu sanity (from observed last, empty occupy):\n";
		std::cout << "  baseline (no skip)     " << nbase.ok << '/' << nbase.n
			  << "  " << nbase.hit << '/' << nbase.tot << "\n";
		std::cout << "  P12 Δ=32 ignore CPU    " << np12.ok << '/' << np12.n
			  << "  " << np12.hit << '/' << np12.tot
			  << "  fires=" << p12_fire << " cand==obs " << p12_eq
			  << " cand!=obs " << p12_ne << "\n";
		std::cout << "  skip packed Δ=128 always " << np14.ok << '/' << np14.n
			  << "  " << np14.hit << '/' << np14.tot << "\n";
	}
	return 0;
}

// Iteration 8+: dummy as CPU D16 on the full table; G1 mid-wait CPU; stop traces.
static int run_iter8(const fs::path& slots_dir)
{
	std::vector<int> cpu_slot[3], packed_slot[3], cpu_wait[3], full_wait[3],
			 full_wait19[3], full_wait_packed[3];
	std::unordered_set<int> packed_set[3];
	for (int m = 0; m < 3; ++m) {
		cpu_slot[m] = cpu_slots_of(CMD_TABLE[m]);
		packed_slot[m] = packed_slots_of(CMD_TABLE[m]);
		packed_set[m] = {packed_slot[m].begin(), packed_slot[m].end()};
		cpu_wait[m] = make_wait(cpu_slot[m], CMD_TABLE[m], NEED);
		std::vector<int> all;
		all.reserve(CMD_TABLE[m].n);
		for (int i = 0; i < CMD_TABLE[m].n; ++i) all.push_back(CMD_TABLE[m].data[i]);
		full_wait[m] = make_wait(all, CMD_TABLE[m], NEED);
		full_wait19[m] = make_wait(all, CMD_TABLE[m], 19);
		full_wait_packed[m] = make_wait(all, CMD_TABLE[m], PACKED_NEED);
	}

	std::cout << "\n======== Iteration 8 probes ========\n";

	// LUT: 6-cycle window where full table grants packed C and CPU-legal table grants C+26.
	std::cout << "\nD16 window (sprOff): T where first_full(T)=C packed and first_cpu(T)=C+26\n";
	{
		int mi = 1; // sprOff
		int C0 = 220 + 2 * LINE;
		int nwin = 0;
		std::cout << "  C=" << C0 << " S=" << (C0 + 26) << "  T:";
		for (int T = C0 - 30; T <= C0 + 5; ++T) {
			int ff = first_slot(full_wait[mi], T);
			int fc = first_slot(cpu_wait[mi], T);
			if (ff == C0 && fc == C0 + 26) {
				std::cout << " " << (T - C0);
				++nwin;
			}
		}
		std::cout << "  (n=" << nwin << ", theory (C-22,C-16] relative  -21..-16)\n";
		for (int need : {18, 19, 20}) {
			std::vector<int> all;
			for (int i = 0; i < CMD_TABLE[mi].n; ++i) all.push_back(CMD_TABLE[mi].data[i]);
			auto fw = make_wait(all, CMD_TABLE[mi], need);
			std::cout << "  full NEED=" << need << " T:";
			int n = 0;
			for (int T = C0 - 30; T <= C0 + 5; ++T) {
				int ff = first_slot(fw, T);
				int fc = first_slot(cpu_wait[mi], T);
				if (ff == C0 && fc == C0 + 26) {
					std::cout << " " << (T - C0);
					++n;
				}
			}
			std::cout << "  (n=" << n << ")\n";
		}
	}

	std::vector<fs::path> mixed, stopf;
	for (const auto& ent : fs::directory_iterator(slots_dir)) {
		if (!ent.is_regular_file()) continue;
		auto name = ent.path().filename().string();
		if (is_cpu_file(name, "stop")) stopf.push_back(ent.path());
		else if (is_cpu_file(name, "all") && name.find("-stop-") == std::string::npos)
			mixed.push_back(ent.path());
	}
	std::sort(mixed.begin(), mixed.end());
	std::sort(stopf.begin(), stopf.end());

	// --- stop: dummy vs CPU at C+26 ---
	int st_cpu26 = 0, st_dum26 = 0, st_empty26 = 0, st_other26 = 0;
	int st_cpu54 = 0, st_dum54 = 0;
	int st_dummy = 0, st_dum_cpu26 = 0, st_dum_cpu54 = 0, st_dum_else = 0;
	for (const auto& txt : stopf) {
		Capture cap = load_txt_only(txt);
		if (cap.mode != Mode::SprOff) continue;
		int mi = 1;
		std::unordered_set<int> dum(cap.dummy.begin(), cap.dummy.end());
		std::unordered_set<int> cpu(cap.obs.begin(), cap.obs.end());
		st_dummy += int(cap.dummy.size());
		for (int C : cap.dummy) {
			if (cpu.count(C + 26)) ++st_dum_cpu26;
			else if (cpu.count(C + 54)) ++st_dum_cpu54;
			else ++st_dum_else;
		}
		for (int S : cap.obs) {
			int C = S - 26;
			if (!packed_set[mi].count(mod_line(C))) continue;
			++st_cpu26;
			if (dum.count(C)) ++st_dum26;
			else if (cpu.count(C)) ++st_other26;
			else ++st_empty26;
			int C54 = S - 54;
			if (packed_set[mi].count(mod_line(C54))) {
				++st_cpu54;
				if (dum.count(C54)) ++st_dum54;
			}
		}
	}
	std::cout << "\nStop sprites-off (CPU-only, no command engine):\n";
	std::cout << "  dummy RAS " << st_dummy << "  CPU at +26 " << st_dum_cpu26
		  << "  +54 " << st_dum_cpu54 << "  else " << st_dum_else << "\n";
	std::cout << "  CPU RAS with packed S-26: n=" << st_cpu26
		  << " dummy@S-26=" << st_dum26
		  << " empty=" << st_empty26
		  << " other=" << st_other26 << "\n";
	std::cout << "  CPU RAS with packed S-54: n=" << st_cpu54
		  << " dummy@S-54=" << st_dum54 << "\n";
	if (st_cpu26)
		std::cout << "  If dummy@S-26 / n is ~1, dummy is determined by CPU RAS alone"
			  << " (mixed hits would then be command overriding).\n";

	// --- mixed packed-cand: C-6 occupancy, G1 hit risk, P15 leftovers ---
	int dum_c6_cpu = 0, dum_c6_eng = 0, dum_c6_dum = 0, dum_c6_empty = 0, dum_n = 0;
	int hit_c6_cpu = 0, hit_c6_eng = 0, hit_c6_dum = 0, hit_c6_empty = 0, mh_n = 0;
	int g1_hit = 0, g1_dum = 0, g1_idle = 0, g1_mh = 0;
	int p15_miss_print = 0;

	struct Row {
		int n = 0, ok = 0, hit = 0, tot = 0;
	};
	Row p15, g1, p5g1, p5only;

	auto cpu_midwait = [&](const std::vector<int>& obs, int lastp, int cand0) {
		auto it = std::upper_bound(obs.begin(), obs.end(), lastp);
		while (it != obs.end() && *it <= cand0) {
			if (*it != cand0 - 6) return true;
			++it;
		}
		return false;
	};

	for (const auto& txt : mixed) {
		Capture cap = load_txt_only(txt);
		Cmd cmd = cmd_of(cap.name);
		if (cmd == Cmd::Unknown || cap.eng.size() < 2 || cap.obs.empty()) continue;
		int mi = mode_index(cap.mode);
		Variant var = parse_variant(cap.name);
		const SlotTable& tabmi = CMD_TABLE[mi];
		auto edist = [&](int a, int b) {
			return engine_dist(a, b, tabmi);
		};
		std::unordered_set<int> occ_cpu(cap.obs.begin(), cap.obs.end());
		std::unordered_set<int> dummy_set(cap.dummy.begin(), cap.dummy.end());
		std::unordered_set<int> eng_set;
		for (auto& e : cap.eng) eng_set.insert(e.ras);
		std::vector<int> obs_e;
		for (auto& e : cap.eng) obs_e.push_back(e.ras);

		auto run = [&](Row& row, auto should_skip) {
			HmmvPred out;
			out.pred.push_back(cap.eng.front().ras);
			int lastp = cap.eng.front().ras;
			for (size_t k = 1; k < cap.eng.size(); ++k) {
				auto w = step_wait(cmd, cap.mode, cap.eng, int(k), lastp, var);
				int delta = pick_delta(w, lastp, tabmi, occ_cpu, cap.eng[k].ras);
				if (delta < 0) delta = 24 + spr_addend(cap.mode);
				std::unordered_set<int> occ = occ_cpu;
				int cand0 = next_cmd_slot(lastp, delta, tabmi, occ);
				int Snext = -1;
				auto it = std::upper_bound(cap.obs.begin(), cap.obs.end(), lastp);
				if (it != cap.obs.end()) Snext = *it;
				if (should_skip(lastp, cand0, delta, Snext)) occ.insert(cand0);
				out.pred.push_back(next_cmd_slot(lastp, delta, tabmi, occ));
				lastp = cap.eng[k].ras;
			}
			auto sc = score_times(out.pred, obs_e);
			++row.n;
			if (sc.extra == 0 && sc.miss == 0) ++row.ok;
			row.hit += sc.hit;
			row.tot += int(obs_e.size());
			return sc.extra == 0 && sc.miss == 0;
		};

		auto is_p5 = [&](int lastp, int cand0, int delta, int Snext) {
			return packed_set[mi].count(mod_line(lastp)) &&
			       packed_set[mi].count(mod_line(cand0)) &&
			       delta == 32 && Snext > cand0;
		};
		auto is_g1 = [&](int lastp, int cand0, int, int) {
			return packed_set[mi].count(mod_line(cand0)) &&
			       cpu_midwait(cap.obs, lastp, cand0);
		};
		auto is_nl = [&](int lastp, int cand0, int delta, int) {
			if (!packed_set[mi].count(mod_line(lastp)) ||
			    !packed_set[mi].count(mod_line(cand0)) || !is_nl_delta(delta))
				return false;
			auto it = std::upper_bound(cap.obs.begin(), cap.obs.end(), lastp);
			return it != cap.obs.end() && *it <= cand0;
		};

		run(p5only, is_p5);
		bool ok15 = run(p15, [&](int a, int b, int d, int s) { return is_p5(a, b, d, s) || is_nl(a, b, d, s); });
		run(g1, is_g1);
		run(p5g1, [&](int a, int b, int d, int s) { return is_p5(a, b, d, s) || is_g1(a, b, d, s); });

		if (!ok15 && p15_miss_print < 20) {
			++p15_miss_print;
			std::cout << "  P15-fail " << cap.name << "  dummy=" << cap.dummy.size()
				  << " cpu=" << cap.obs.size() << " eng=" << cap.eng.size() << "\n";
		}

		int last = cap.eng.front().ras;
		for (size_t k = 1; k < cap.eng.size(); ++k) {
			auto w = step_wait(cmd, cap.mode, cap.eng, int(k), last, var);
			int delta = pick_delta(w, last, tabmi, occ_cpu, cap.eng[k].ras);
			if (delta < 0) delta = 24;
			int cand = next_cmd_slot(last, delta, tabmi, occ_cpu);
			int obs = cap.eng[k].ras;
			if (packed_set[mi].count(mod_line(cand))) {
				bool dummy = dummy_set.count(cand);
				bool hit = cand == obs;
				int c6 = cand - 6;
				bool g1h = cpu_midwait(cap.obs, last, cand);
				auto c6kind = [&]() {
					if (occ_cpu.count(c6)) return 0;
					if (eng_set.count(c6)) return 1;
					if (dummy_set.count(c6)) return 2;
					return 3;
				};
				if (dummy) {
					++dum_n;
					int ck = c6kind();
					if (ck == 0) ++dum_c6_cpu;
					else if (ck == 1) ++dum_c6_eng;
					else if (ck == 2) ++dum_c6_dum;
					else ++dum_c6_empty;
					if (g1h) ++g1_dum;
				} else if (hit) {
					int rel_maybe = 0;
					auto it = std::upper_bound(cap.obs.begin(), cap.obs.end(), last);
					int S = (it == cap.obs.end()) ? -1 : *it;
					if (S >= 0) {
						auto tw = cpu_t_window(cpu_wait[mi], S);
						if (tw.first <= cand && cand < S && tw.second > cand) rel_maybe = 1;
					}
					if (rel_maybe) {
						++mh_n;
						int ck = c6kind();
						if (ck == 0) ++hit_c6_cpu;
						else if (ck == 1) ++hit_c6_eng;
						else if (ck == 2) ++hit_c6_dum;
						else ++hit_c6_empty;
						if (g1h) ++g1_mh;
					}
					if (g1h) ++g1_hit;
				} else if (!occ_cpu.count(cand)) {
					if (g1h) ++g1_idle;
				}
			}
			last = obs;
		}
	}

	auto pr = [&](const char* t, Row r) {
		std::cout << "  " << t << "  " << r.ok << '/' << r.n << " files  "
			  << r.hit << '/' << r.tot << "\n";
	};
	std::cout << "\nC-6 occupancy (packed cand, dummy vs maybe-hit):\n";
	std::cout << "  dummy n=" << dum_n << "  C-6 cpu/eng/dum/empty "
		  << dum_c6_cpu << '/' << dum_c6_eng << '/' << dum_c6_dum << '/' << dum_c6_empty << "\n";
	std::cout << "  maybe-hit n=" << mh_n << "  C-6 cpu/eng/dum/empty "
		  << hit_c6_cpu << '/' << hit_c6_eng << '/' << hit_c6_dum << '/' << hit_c6_empty << "\n";
	std::cout << "\nG1 (CPU in (last,C] other than C-6) among packed-cand steps:\n";
	std::cout << "  hits " << g1_hit << "  maybe-hits " << g1_mh
		  << "  dummy " << g1_dum << "  idle " << g1_idle << "\n";
	std::cout << "  If hits>>0, G1 over-blocks real packed command accesses.\n";
	std::cout << "\nScores (CPU RAS occupy, from observed last):\n";
	pr("P5 only", p5only);
	pr("P15 P5+newline", p15);
	pr("G1 only", g1);
	pr("P5+G1", p5g1);

	// --- /CSR T vs 6-cycle dummy window (diagnostic) + D4 score + stop VCD ---
	fs::path vcd_dir = slots_dir / ".." / "1.vcd";
	if (!fs::exists(vcd_dir)) vcd_dir = slots_dir.parent_path() / "1.vcd";
	struct STMap {
		bool ok = false;
		std::unordered_map<int, int> S_to_T;
		std::vector<int> posts;
		std::vector<int> armT;
	};
	auto map_ST = [&](const Capture& cap) -> STMap {
		STMap out;
		int dummy_g[2][DELTA_HI - DELTA_LO + 1] = {};
		int dummy_e[2][DELTA_HI - DELTA_LO + 1] = {};
		int dummy_m[2][DELTA_HI - DELTA_LO + 1] = {};
		int dummy_n[2][DELTA_HI - DELTA_LO + 1] = {};
		FileFit best{};
		try {
			best = fit_file(cap, cpu_wait, dummy_g, dummy_e, dummy_m, dummy_n);
		} catch (...) {
			return out;
		}
		if (best.n <= 0) return out;
		int mi = mode_index(cap.mode);
		const auto& w = cpu_wait[mi];
		out.posts = make_posts(cap.t2[best.edge_i], best.delta, w, cap.tmin, cap.tmax);
		auto sim = vdp_cpu_posts(out.posts, w, cap.tmin, cap.tmax);
		for (auto& s : sim.pred) {
			out.S_to_T[s.S] = s.T;
			out.armT.push_back(s.T);
		}
		out.ok = true;
		return out;
	};
	auto in_dummy_win = [](int T, int C) {
		int rel = T - C;
		return rel > -22 && rel <= -16;
	};

	if (fs::exists(vcd_dir)) {
		std::cout << "\n/CSR arming T (vdp_cpu_posts) vs dummy window (C-22,C-16].\n";
		int dum_in = 0, dum_out = 0, dum_nomatch = 0;
		int hit_in = 0, hit_out = 0, hit_nomatch = 0;
		int n_vcd = 0, n_vcd_fail = 0;
		std::map<int, int> dum_rel, hit_rel;
		Row d4, d4p5, d6, d6p5, d6p5nl;
		Row a16any, a16maj, a16all, a19any, a19maj, a19all;
		Row a16any_p5, a16maj_p5, a19any_p5, a19maj_p5, a19all_p5;
		int dum_g16[4] = {}, hit_g16[4] = {}, dum_g19[4] = {}, hit_g19[4] = {};
		std::map<int, int> post_dt, arm_dt;
		int n7171 = 0, n7272 = 0, n7172 = 0, n7271 = 0, n_other_pair = 0;
		auto ngrant = [&](const std::vector<int>& fw, int T, int C, int k) {
			int n = 0;
			for (int d = -k; d <= k; ++d)
				if (first_slot(fw, T + d) == C) ++n;
			return n;
		};
		std::map<std::string, int> inwin_dum_k, inwin_hit_k;

		for (const auto& txt : mixed) {
			bool spr_off = txt.filename().string().find("sprOff") != std::string::npos;
			Capture cap;
			std::unordered_map<int, int> S_to_T;
			bool have_t = false;
			if (spr_off) {
				fs::path vcd = vcd_dir / (txt.stem().string() + ".vcd");
				try {
					cap = load_capture(txt, vcd);
					auto p = map_ST(cap);
					have_t = p.ok;
					S_to_T = std::move(p.S_to_T);
					auto add_gaps = [&](const std::vector<int>& ts, std::map<int, int>& hist) {
						for (size_t i = 1; i < ts.size(); ++i) {
							int dt = ts[i] - ts[i - 1];
							hist[dt]++;
						}
					};
					add_gaps(p.posts, post_dt);
					add_gaps(p.armT, arm_dt);
					for (size_t i = 2; i < p.posts.size(); ++i) {
						int a = p.posts[i - 1] - p.posts[i - 2];
						int b = p.posts[i] - p.posts[i - 1];
						auto bun = [](int d) {
							return d == 71 || d == 72;
						};
						if (!bun(a) || !bun(b)) {
							++n_other_pair;
							continue;
						}
						if (a == 71 && b == 71) ++n7171;
						else if (a == 72 && b == 72) ++n7272;
						else if (a == 71 && b == 72) ++n7172;
						else ++n7271;
					}
					++n_vcd;
				} catch (...) {
					++n_vcd_fail;
					cap = load_txt_only(txt);
				}
			} else {
				cap = load_txt_only(txt);
			}
			Cmd cmd = cmd_of(cap.name);
			if (cmd == Cmd::Unknown || cap.eng.size() < 2 || cap.obs.empty()) continue;
			int mi = mode_index(cap.mode);
			Variant var = parse_variant(cap.name);
			const SlotTable& tabmi = CMD_TABLE[mi];
			std::unordered_set<int> occ_cpu(cap.obs.begin(), cap.obs.end());
			std::unordered_set<int> dummy_set(cap.dummy.begin(), cap.dummy.end());
			std::vector<int> obs_e;
			for (auto& e : cap.eng) obs_e.push_back(e.ras);

			auto is_next_cpu = [](int cand0, int Snext) {
				return Snext == cand0 + 26 || Snext == cand0 + 54;
			};
			auto skip_d4 = [&](int lastp, int cand0, int delta, int Snext) {
				bool p5 = packed_set[mi].count(mod_line(lastp)) &&
					  packed_set[mi].count(mod_line(cand0)) &&
					  delta == 32 && Snext > cand0;
				if (p5) return true;
				if (!have_t || !packed_set[mi].count(mod_line(cand0))) return false;
				if (!is_next_cpu(cand0, Snext)) return false;
				auto jt = S_to_T.find(Snext);
				return jt != S_to_T.end() && in_dummy_win(jt->second, cand0);
			};
			auto skip_d4only = [&](int, int cand0, int, int Snext) {
				if (!have_t || !packed_set[mi].count(mod_line(cand0))) return false;
				if (!is_next_cpu(cand0, Snext)) return false;
				auto jt = S_to_T.find(Snext);
				return jt != S_to_T.end() && in_dummy_win(jt->second, cand0);
			};
			auto tight_win = [&](int T, int C) {
				int rel = T - C;
				return rel <= -PACKED_NEED && rel > -(NEED + 6);
			};
			auto skip_d6 = [&](int lastp, int cand0, int delta, int Snext) {
				bool p5 = packed_set[mi].count(mod_line(lastp)) &&
					  packed_set[mi].count(mod_line(cand0)) &&
					  delta == 32 && Snext > cand0;
				if (p5) return true;
				if (!have_t || !packed_set[mi].count(mod_line(cand0))) return false;
				if (!is_next_cpu(cand0, Snext)) return false;
				auto jt = S_to_T.find(Snext);
				return jt != S_to_T.end() && tight_win(jt->second, cand0);
			};
			auto skip_d6only = [&](int, int cand0, int, int Snext) {
				if (!have_t || !packed_set[mi].count(mod_line(cand0))) return false;
				if (!is_next_cpu(cand0, Snext)) return false;
				auto jt = S_to_T.find(Snext);
				return jt != S_to_T.end() && tight_win(jt->second, cand0);
			};
			auto grant_skip = [&](int lastp, int cand0, int delta, int Snext,
					      const std::vector<int>& fw, int k, int min_n, bool with_p5) {
				if (with_p5 && packed_set[mi].count(mod_line(lastp)) &&
				    packed_set[mi].count(mod_line(cand0)) &&
				    delta == 32 && Snext > cand0)
					return true;
				if (!have_t || !packed_set[mi].count(mod_line(cand0))) return false;
				if (!is_next_cpu(cand0, Snext)) return false;
				auto jt = S_to_T.find(Snext);
				if (jt == S_to_T.end()) return false;
				return ngrant(fw, jt->second, cand0, k) >= min_n;
			};
			auto run = [&](Row& row, auto should_skip) {
				HmmvPred out;
				out.pred.push_back(cap.eng.front().ras);
				int lastp = cap.eng.front().ras;
				for (size_t k = 1; k < cap.eng.size(); ++k) {
					auto w = step_wait(cmd, cap.mode, cap.eng, int(k), lastp, var);
					int delta = pick_delta(w, lastp, tabmi, occ_cpu, cap.eng[k].ras);
					if (delta < 0) delta = 24 + spr_addend(cap.mode);
					std::unordered_set<int> occ = occ_cpu;
					int cand0 = next_cmd_slot(lastp, delta, tabmi, occ);
					int Snext = -1;
					auto it = std::upper_bound(cap.obs.begin(), cap.obs.end(), lastp);
					if (it != cap.obs.end()) Snext = *it;
					if (should_skip(lastp, cand0, delta, Snext)) occ.insert(cand0);
					out.pred.push_back(next_cmd_slot(lastp, delta, tabmi, occ));
					lastp = cap.eng[k].ras;
				}
				auto sc = score_times(out.pred, obs_e);
				++row.n;
				if (sc.extra == 0 && sc.miss == 0) ++row.ok;
				row.hit += sc.hit;
				row.tot += int(obs_e.size());
				return sc.extra == 0 && sc.miss == 0;
			};
			run(d4, skip_d4only);
			run(d4p5, skip_d4);
			run(d6, skip_d6only);
			run(d6p5, skip_d6);
			run(a16any, [&](int a, int b, int d, int s) { return grant_skip(a, b, d, s, full_wait[mi], 1, 1, false); });
			run(a16maj, [&](int a, int b, int d, int s) { return grant_skip(a, b, d, s, full_wait[mi], 1, 2, false); });
			run(a16all, [&](int a, int b, int d, int s) { return grant_skip(a, b, d, s, full_wait[mi], 1, 3, false); });
			run(a19any, [&](int a, int b, int d, int s) { return grant_skip(a, b, d, s, full_wait19[mi], 1, 1, false); });
			run(a19maj, [&](int a, int b, int d, int s) { return grant_skip(a, b, d, s, full_wait19[mi], 1, 2, false); });
			run(a19all, [&](int a, int b, int d, int s) { return grant_skip(a, b, d, s, full_wait19[mi], 1, 3, false); });
			run(a16any_p5, [&](int a, int b, int d, int s) { return grant_skip(a, b, d, s, full_wait[mi], 1, 1, true); });
			run(a16maj_p5, [&](int a, int b, int d, int s) { return grant_skip(a, b, d, s, full_wait[mi], 1, 2, true); });
			run(a19any_p5, [&](int a, int b, int d, int s) { return grant_skip(a, b, d, s, full_wait19[mi], 1, 1, true); });
			run(a19maj_p5, [&](int a, int b, int d, int s) { return grant_skip(a, b, d, s, full_wait19[mi], 1, 2, true); });
			run(a19all_p5, [&](int a, int b, int d, int s) { return grant_skip(a, b, d, s, full_wait19[mi], 1, 3, true); });
			bool okd6 = run(d6p5nl, [&](int lastp, int cand0, int delta, int Snext) {
				if (skip_d6(lastp, cand0, delta, Snext)) return true;
				if (!packed_set[mi].count(mod_line(lastp)) ||
				    !packed_set[mi].count(mod_line(cand0)) || !is_nl_delta(delta))
					return false;
				auto it = std::upper_bound(cap.obs.begin(), cap.obs.end(), lastp);
				return it != cap.obs.end() && *it <= cand0;
			});
			if (!okd6)
				std::cout << "  D6+P5+nl-fail " << cap.name << "\n";

			if (!have_t) continue;
			int last = cap.eng.front().ras;
			for (size_t k = 1; k < cap.eng.size(); ++k) {
				auto sw = step_wait(cmd, cap.mode, cap.eng, int(k), last, var);
				int delta = pick_delta(sw, last, tabmi, occ_cpu, cap.eng[k].ras);
				if (delta < 0) delta = 24;
				int cand = next_cmd_slot(last, delta, tabmi, occ_cpu);
				int obs = cap.eng[k].ras;
				if (packed_set[mi].count(mod_line(cand))) {
					auto it = std::upper_bound(cap.obs.begin(), cap.obs.end(), last);
					int S = (it == cap.obs.end()) ? -1 : *it;
					if (S == cand + 26 || S == cand + 54) {
						bool dummy = dummy_set.count(cand);
						bool hit = cand == obs;
						auto jt = S_to_T.find(S);
						if (dummy || hit) {
							if (jt == S_to_T.end()) {
								if (dummy) ++dum_nomatch;
								else ++hit_nomatch;
							} else {
								int rel = jt->second - cand;
								bool inwin = in_dummy_win(jt->second, cand);
								std::string key = std::string(cmd_name(cmd)) + " Δ" +
										  std::to_string(delta);
								if (dummy) {
									dum_rel[rel]++;
									int g16 = ngrant(full_wait[mi], jt->second, cand, 1);
									int g19 = ngrant(full_wait19[mi], jt->second, cand, 1);
									if (g16 >= 0 && g16 <= 3) dum_g16[g16]++;
									if (g19 >= 0 && g19 <= 3) dum_g19[g19]++;
									if (inwin) {
										++dum_in;
										inwin_dum_k[key]++;
									} else ++dum_out;
								} else {
									hit_rel[rel]++;
									int g16 = ngrant(full_wait[mi], jt->second, cand, 1);
									int g19 = ngrant(full_wait19[mi], jt->second, cand, 1);
									if (g16 >= 0 && g16 <= 3) hit_g16[g16]++;
									if (g19 >= 0 && g19 <= 3) hit_g19[g19]++;
									if (inwin) {
										++hit_in;
										inwin_hit_k[key]++;
									} else ++hit_out;
								}
							}
						}
					}
				}
				last = obs;
			}
		}
		std::cout << "  vcd files " << n_vcd << " fail " << n_vcd_fail << "\n";
		std::cout << "  dummy T in window " << dum_in << "  out " << dum_out
			  << "  no sim S " << dum_nomatch << "\n";
		std::cout << "  packed-hit cpu26 T in window " << hit_in << "  out " << hit_out
			  << "  no sim S " << hit_nomatch << "\n";
		std::cout << "  dummy T-C:";
		for (auto [r, c] : dum_rel) std::cout << " " << r << "=" << c;
		std::cout << "\n  hit T-C:";
		for (auto [r, c] : hit_rel) std::cout << " " << r << "=" << c;
		std::cout << "\n  in-window dummy keys:\n";
		for (auto& [k, c] : inwin_dum_k) std::cout << "    " << k << "  " << c << "\n";
		std::cout << "  in-window hit keys:\n";
		for (auto& [k, c] : inwin_hit_k) std::cout << "    " << k << "  " << c << "\n";
		std::cout << "  D4 skip packed if CPU+26 and T in dummy window: "
			  << d4.ok << '/' << d4.n << "  " << d4.hit << '/' << d4.tot << "\n";
		std::cout << "  D4+P5: " << d4p5.ok << '/' << d4p5.n << "  " << d4p5.hit << '/'
			  << d4p5.tot << "\n";
		std::cout << "  D6 skip if T-C in (" << -(NEED + 6) << ','
			  << -PACKED_NEED << "] (integer approximation): "
			  << d6.ok << '/' << d6.n << "  " << d6.hit << '/' << d6.tot << "\n";
		std::cout << "  D6+P5: " << d6p5.ok << '/' << d6p5.n << "  " << d6p5.hit << '/'
			  << d6p5.tot << "\n";
		std::cout << "  D6+P5+newline: " << d6p5nl.ok << '/' << d6p5nl.n << "  "
			  << d6p5nl.hit << '/' << d6p5nl.tot << "\n";

		auto gpr = [](const char* t, const int g[4]) {
			std::cout << "  " << t << "  ngrant±1  0=" << g[0] << " 1=" << g[1]
				  << " 2=" << g[2] << " 3=" << g[3] << "\n";
		};
		std::cout << "\nAsync T±1 vs first_full(T+d)==C (ngrant 0=none .. 3=all):\n";
		gpr("NEED=16 dummy", dum_g16);
		gpr("NEED=16 hit  ", hit_g16);
		gpr("NEED=19 dummy", dum_g19);
		gpr("NEED=19 hit  ", hit_g19);
		std::cout << "  1=tie (some T±1 grant, some don't). 3=unambiguous grant. 0=unambiguous no.\n";
		auto prr = [&](const char* t, Row r) {
			std::cout << "  " << t << "  " << r.ok << '/' << r.n << "  "
				  << r.hit << '/' << r.tot << "\n";
		};
		std::cout << "Scores skip packed if CPU+26 and ngrant(T±1)>=min (no P5):\n";
		prr("NEED=16 any (>=1)", a16any);
		prr("NEED=16 maj (>=2)", a16maj);
		prr("NEED=16 all (=3)", a16all);
		prr("NEED=19 any (>=1)", a19any);
		prr("NEED=19 maj (>=2)", a19maj);
		prr("NEED=19 all (=3)", a19all);
		std::cout << "Same + P5:\n";
		prr("NEED=16 any+P5", a16any_p5);
		prr("NEED=16 maj+P5", a16maj_p5);
		prr("NEED=19 any+P5", a19any_p5);
		prr("NEED=19 maj+P5", a19maj_p5);
		prr("NEED=19 all+P5", a19all_p5);
		std::cout << "\nConsecutive request spacing (fitted posts = floor(t2)+δ), sprOff mixed:\n";
		std::cout << "  post Δ 68..76:";
		for (int d = 68; d <= 76; ++d)
			if (post_dt[d]) std::cout << " " << d << "=" << post_dt[d];
		std::cout << "\n  post Δ 248..255:";
		for (int d = 248; d <= 255; ++d)
			if (post_dt[d]) std::cout << " " << d << "=" << post_dt[d];
		std::cout << "\n  other Δ with n>=20:";
		for (auto [d, c] : post_dt)
			if (c >= 20 && (d < 68 || (d > 76 && d < 248) || d > 255))
				std::cout << " " << d << "=" << c;
		std::cout << "\n  adjacent 71/72 pairs: 71-71=" << n7171
			  << " 72-72=" << n7272
			  << " 71-72=" << n7172
			  << " 72-71=" << n7271
			  << " other=" << n_other_pair << "\n";
		std::cout << "  arming-T Δ 68..76:";
		for (int d = 68; d <= 76; ++d)
			if (arm_dt[d]) std::cout << " " << d << "=" << arm_dt[d];
		std::cout << "\n";

		int st_in_dum = 0, st_in_empty = 0, st_out_dum = 0, st_out_empty = 0, st_nom = 0;
		int st_early_dum = 0, st_early_empty = 0, st_late_dum = 0, st_late_empty = 0;
		std::map<int, int> st_dum_rel, st_empty_rel;
		int st19_dum[4] = {}, st19_emp[4] = {}, st16_dum[4] = {}, st16_emp[4] = {};
		for (const auto& txt : stopf) {
			if (txt.filename().string().find("sprOff") == std::string::npos) continue;
			fs::path vcd = vcd_dir / (txt.stem().string() + ".vcd");
			Capture cap;
			try {
				cap = load_capture(txt, vcd);
			} catch (...) {
				continue;
			}
			auto p = map_ST(cap);
			if (!p.ok) continue;
			std::unordered_set<int> dum(cap.dummy.begin(), cap.dummy.end());
			int mi = 1;
			for (int S : cap.obs) {
				int C = S - 26;
				if (!packed_set[mi].count(mod_line(C))) continue;
				auto jt = p.S_to_T.find(S);
				if (jt == p.S_to_T.end()) {
					++st_nom;
					continue;
				}
				int rel = jt->second - C;
				bool d = dum.count(C);
				int g16 = ngrant(full_wait[mi], jt->second, C, 1);
				int g19 = ngrant(full_wait19[mi], jt->second, C, 1);
				if (g16 >= 0 && g16 <= 3) (d ? st16_dum : st16_emp)[g16]++;
				if (g19 >= 0 && g19 <= 3) (d ? st19_dum : st19_emp)[g19]++;
				st_dum_rel[rel] += d ? 1 : 0;
				st_empty_rel[rel] += d ? 0 : 1;
				bool inw = in_dummy_win(jt->second, C);
				bool early = rel <= -PACKED_NEED && rel > -(NEED + 6);
				if (inw && d) ++st_in_dum;
				else if (inw && !d) ++st_in_empty;
				else if (!inw && d) ++st_out_dum;
				else ++st_out_empty;
				if (early && d) ++st_early_dum;
				else if (early && !d) ++st_early_empty;
				else if (inw && !early && d) ++st_late_dum;
				else if (inw && !early && !d) ++st_late_empty;
			}
		}
		std::cout << "\nStop sprOff VCD: packed S-26 vs T in dummy window:\n";
		std::cout << "  in-window  dummy " << st_in_dum << "  empty " << st_in_empty << "\n";
		std::cout << "  out-window dummy " << st_out_dum << "  empty " << st_out_empty << "\n";
		std::cout << "  no sim S " << st_nom << "\n";
		std::cout << "  D6 T-C<=-" << PACKED_NEED << "  dummy " << st_early_dum
			  << "  empty " << st_early_empty << "\n";
		std::cout << "  later broad-window cases dummy " << st_late_dum
			  << "  empty " << st_late_empty << "\n";
		std::cout << "  stop dummy T-C:";
		for (auto [r, c] : st_dum_rel) if (c) std::cout << " " << r << "=" << c;
		std::cout << "\n  stop empty T-C:";
		for (auto [r, c] : st_empty_rel) if (c) std::cout << " " << r << "=" << c;
		std::cout << "\n  stop NEED=16 dummy ngrant±1  0/1/2/3 "
			  << st16_dum[0] << '/' << st16_dum[1] << '/' << st16_dum[2] << '/' << st16_dum[3]
			  << "\n  stop NEED=16 empty ngrant±1  0/1/2/3 "
			  << st16_emp[0] << '/' << st16_emp[1] << '/' << st16_emp[2] << '/' << st16_emp[3]
			  << "\n  stop NEED=19 dummy ngrant±1  0/1/2/3 "
			  << st19_dum[0] << '/' << st19_dum[1] << '/' << st19_dum[2] << '/' << st19_dum[3]
			  << "\n  stop NEED=19 empty ngrant±1  0/1/2/3 "
			  << st19_emp[0] << '/' << st19_emp[1] << '/' << st19_emp[2] << '/' << st19_emp[3]
			  << "\n";
	}

	// noCpu: G1 must not fire
	{
		int g1_fire = 0, n_ok = 0, n_run = 0, h = 0, t = 0;
		for (const auto& ent : fs::directory_iterator(slots_dir)) {
			if (!ent.is_regular_file()) continue;
			auto name = ent.path().filename().string();
			if (!is_nocpu_file(name, "all")) continue;
			Capture cap = load_txt_only(ent.path());
			Cmd cmd = cmd_of(cap.name);
			if (cmd == Cmd::Unknown || cap.eng.size() < 2) continue;
			int mi = mode_index(cap.mode);
			Variant var = parse_variant(cap.name);
			const SlotTable& tab = CMD_TABLE[mi];
			std::vector<int> obs_e;
			for (auto& e : cap.eng) obs_e.push_back(e.ras);
			HmmvPred out;
			out.pred.push_back(cap.eng.front().ras);
			int lastp = cap.eng.front().ras;
			std::vector<int> no;
			for (size_t k = 1; k < cap.eng.size(); ++k) {
				auto w = step_wait(cmd, cap.mode, cap.eng, int(k), lastp, var);
				int delta = pick_delta(w, lastp, tab, {}, cap.eng[k].ras);
				if (delta < 0) delta = 24;
				std::unordered_set<int> occ;
				int cand0 = next_cmd_slot(lastp, delta, tab, occ);
				if (packed_set[mi].count(mod_line(cand0)) && cpu_midwait(no, lastp, cand0)) {
					++g1_fire;
					occ.insert(cand0);
				}
				out.pred.push_back(next_cmd_slot(lastp, delta, tab, occ));
				lastp = cap.eng[k].ras;
			}
			auto sc = score_times(out.pred, obs_e);
			++n_run;
			if (sc.extra == 0 && sc.miss == 0) ++n_ok;
			h += sc.hit;
			t += int(obs_e.size());
		}
		std::cout << "\nnoCpu + G1: " << n_ok << '/' << n_run << "  " << h << '/' << t
			  << "  G1 fires=" << g1_fire << " (want 0)\n";
	}

	// --- Beat-phase uncertainty: T_i = floor(t2_i + ε), one ε for the whole burst ---
	{
		fs::path vcd_dir = slots_dir / ".." / "1.vcd";
		if (!fs::exists(vcd_dir)) vcd_dir = slots_dir.parent_path() / "1.vcd";
		if (!fs::exists(vcd_dir)) {
			std::cout << "\nBeat-phase: no VCD dir, skip.\n";
			return 0;
		}
		std::cout << "\n======== Beat-phase model T=floor(t2+ε) ========\n";
		std::cout << "One ε per file (global analog offset before floor). "
			  "This slides 71/72 doubles; it does not jitter requests independently.\n";

		struct Row {
			int n = 0, ok = 0, hit = 0, tot = 0;
		};
		Row r_int, r_d16, r_eng, r_worst, r_any, r_all, r_maj;
		int n_slide = 0, n_slide_files = 0, n_all_shift = 0, n_partial = 0;
		int n7171_lo = 0, n7272_lo = 0, n7171_hi = 0, n7272_hi = 0;
		int n_print = 0, n_eps_ne_delta = 0, n_spr = 0;
		long long n_post_cmp = 0;
		int n7171_int = 0, n7272_int = 0, n7171_half = 0, n7272_half = 0;
		double sum_abs_d16 = 0, sum_abs_eng = 0;
		std::vector<std::string> oracle_fail;

		auto dummy_grant = [&](const std::unordered_map<int, int>& ST, int mi,
				       int cand, int Snext) {
			if (Snext != cand + 26 && Snext != cand + 54) return false;
			if (!packed_set[mi].count(mod_line(cand))) return false;
			auto it = ST.find(Snext);
			if (it == ST.end()) return false;
			return first_slot(full_wait_packed[mi], it->second) == cand;
		};

		auto score_eng = [&](const Capture& cap, const std::unordered_set<int>& occ_cpu,
				     auto should_skip) -> std::pair<int, int> {
			Cmd cmd = cmd_of(cap.name);
			int mi = mode_index(cap.mode);
			Variant var = parse_variant(cap.name);
			const SlotTable& tabmi = CMD_TABLE[mi];
			std::vector<int> obs_e;
			for (auto& e : cap.eng) obs_e.push_back(e.ras);
			HmmvPred out;
			out.pred.push_back(cap.eng.front().ras);
			int lastp = cap.eng.front().ras;
			for (size_t k = 1; k < cap.eng.size(); ++k) {
				auto w = step_wait(cmd, cap.mode, cap.eng, int(k), lastp, var);
				int delta = pick_delta(w, lastp, tabmi, occ_cpu, cap.eng[k].ras);
				if (delta < 0) delta = 24 + spr_addend(cap.mode);
				std::unordered_set<int> occ = occ_cpu;
				int cand0 = next_cmd_slot(lastp, delta, tabmi, occ);
				int Snext = -1;
				auto it = std::upper_bound(cap.obs.begin(), cap.obs.end(), lastp);
				if (it != cap.obs.end()) Snext = *it;
				bool nl = packed_set[mi].count(mod_line(lastp)) &&
					  packed_set[mi].count(mod_line(cand0)) &&
					  is_nl_delta(delta) && Snext > lastp && Snext <= cand0;
				if (nl || should_skip(lastp, cand0, delta, Snext)) occ.insert(cand0);
				out.pred.push_back(next_cmd_slot(lastp, delta, tabmi, occ));
				lastp = cap.eng[k].ras;
			}
			auto sc = score_times(out.pred, obs_e);
			return {sc.hit, int(obs_e.size())};
		};

		auto acc = [](Row& r, int hit, int tot) {
			++r.n;
			if (hit == tot) ++r.ok;
			r.hit += hit;
			r.tot += tot;
		};

		for (const auto& txt : mixed) {
			bool spr_off = txt.filename().string().find("sprOff") != std::string::npos;
			Capture cap;
			std::unordered_set<int> occ_cpu;
			if (!spr_off) {
				cap = load_txt_only(txt);
				Cmd cmd = cmd_of(cap.name);
				if (cmd == Cmd::Unknown || cap.eng.size() < 2 || cap.obs.empty()) continue;
				occ_cpu.insert(cap.obs.begin(), cap.obs.end());
				int mi = mode_index(cap.mode);
				auto skip_p5 = [&](int lastp, int cand0, int delta, int Snext) {
					return packed_set[mi].count(mod_line(lastp)) &&
					       packed_set[mi].count(mod_line(cand0)) &&
					       delta == 32 && Snext > cand0;
				};
				auto [h, t] = score_eng(cap, occ_cpu, skip_p5);
				acc(r_int, h, t);
				acc(r_d16, h, t);
				acc(r_eng, h, t);
				acc(r_worst, h, t);
				acc(r_any, h, t);
				acc(r_all, h, t);
				acc(r_maj, h, t);
				continue;
			}
			fs::path vcd = vcd_dir / (txt.stem().string() + ".vcd");
			try {
				cap = load_capture(txt, vcd);
			} catch (...) {
				continue;
			}
			Cmd cmd = cmd_of(cap.name);
			if (cmd == Cmd::Unknown || cap.eng.size() < 2 || cap.obs.empty()) continue;
			occ_cpu.insert(cap.obs.begin(), cap.obs.end());
			int mi = mode_index(cap.mode);
			int dummy_g[2][DELTA_HI - DELTA_LO + 1] = {};
			int dummy_e[2][DELTA_HI - DELTA_LO + 1] = {};
			int dummy_m[2][DELTA_HI - DELTA_LO + 1] = {};
			int dummy_n[2][DELTA_HI - DELTA_LO + 1] = {};
			FileFit best{};
			try {
				best = fit_file(cap, cpu_wait, dummy_g, dummy_e, dummy_m, dummy_n);
			} catch (...) {
				continue;
			}
			if (best.n <= 0) continue;
			const auto& w = cpu_wait[mi];
			const auto& t2s = cap.t2[best.edge_i];

			constexpr double STEP = 0.1;
			constexpr double SPAN = 2.0;
			std::vector<double> epsv;
			for (double e = best.delta - SPAN; e <= best.delta + SPAN + 1e-9; e += STEP)
				epsv.push_back(e);
			const int ne = int(epsv.size());
			std::vector<std::unordered_map<int, int>> ST(ne);
			std::vector<int> d16_score(ne), eng_hit(ne), n71(ne), n72(ne), n7171(ne), n7272(ne);
			std::vector<std::vector<int>> post_at(ne);

			for (int i = 0; i < ne; ++i) {
				auto posts = make_posts_eps(t2s, epsv[i], w, cap.tmin, cap.tmax);
				post_at[i] = posts;
				auto sim = vdp_cpu_posts(posts, w, cap.tmin, cap.tmax);
				for (auto& s : sim.pred) ST[i][s.S] = s.T;
				auto ev = align_d16(sim.pred, cap.obs);
				auto sc = score_d16(ev, cap.tmin, cap.tmax, 0, 0);
				d16_score[i] = sc.hit - sc.extra - sc.miss;
				for (size_t j = 1; j < posts.size(); ++j) {
					int dt = posts[j] - posts[j - 1];
					if (dt == 71) ++n71[i];
					if (dt == 72) ++n72[i];
					if (j >= 2) {
						int a = posts[j - 1] - posts[j - 2];
						if (a == 71 && dt == 71) ++n7171[i];
						if (a == 72 && dt == 72) ++n7272[i];
					}
				}
				auto skip = [&](int lastp, int cand0, int delta, int Snext) {
					if (packed_set[mi].count(mod_line(lastp)) &&
					    packed_set[mi].count(mod_line(cand0)) &&
					    delta == 32 && Snext > cand0)
						return true;
					return dummy_grant(ST[i], mi, cand0, Snext);
				};
				auto [h, t] = score_eng(cap, occ_cpu, skip);
				eng_hit[i] = h;
				if (i == 0) {
					// tot from first
				}
			}

			auto tot_e = int(cap.eng.size());
			int i_int = -1;
			for (int i = 0; i < ne; ++i)
				if (std::abs(epsv[i] - best.delta) < 0.001) i_int = i;
			if (i_int < 0) i_int = ne / 2;
			int i_d16 = 0, i_eng = 0;
			for (int i = 1; i < ne; ++i) {
				if (d16_score[i] > d16_score[i_d16]) i_d16 = i;
				if (eng_hit[i] > eng_hit[i_eng]) i_eng = i;
			}
			int i_worst = 0;
			for (int i = 1; i < ne; ++i)
				if (eng_hit[i] < eng_hit[i_worst]) i_worst = i;
			acc(r_int, eng_hit[i_int], tot_e);
			acc(r_d16, eng_hit[i_d16], tot_e);
			acc(r_eng, eng_hit[i_eng], tot_e);
			acc(r_worst, eng_hit[i_worst], tot_e);
			++n_spr;
			if (i_d16 != i_int) ++n_eps_ne_delta;
			sum_abs_d16 += std::abs(epsv[i_d16] - best.delta);
			sum_abs_eng += std::abs(epsv[i_eng] - best.delta);
			if (eng_hit[i_eng] < tot_e)
				oracle_fail.push_back(cap.name + " " +
					std::to_string(eng_hit[i_eng]) + "/" +
					std::to_string(tot_e) + " ε=" +
					std::to_string(epsv[i_eng]));
			n7171_int += n7171[i_int];
			n7272_int += n7272[i_int];
			int i_half = i_int;
			for (int i = 0; i < ne; ++i)
				if (std::abs(epsv[i] - (best.delta + 0.5)) < 0.06) i_half = i;
			n7171_half += n7171[i_half];
			n7272_half += n7272[i_half];

			// any/all/maj over ε in [ε_d16-1, ε_d16+1]
			double lo = epsv[i_d16] - 1.0, hi = epsv[i_d16] + 1.0;
			std::vector<int> band;
			for (int i = 0; i < ne; ++i)
				if (epsv[i] >= lo - 1e-9 && epsv[i] <= hi + 1e-9) band.push_back(i);

			auto skip_frac = [&](int min_n) {
				return [&, min_n](int lastp, int cand0, int delta, int Snext) {
					if (packed_set[mi].count(mod_line(lastp)) &&
					    packed_set[mi].count(mod_line(cand0)) &&
					    delta == 32 && Snext > cand0)
						return true;
					int g = 0;
					for (int i : band)
						if (dummy_grant(ST[i], mi, cand0, Snext)) ++g;
					return g >= min_n;
				};
			};
			int nb = int(band.size());
			auto [ha, ta] = score_eng(cap, occ_cpu, skip_frac(1));
			auto [hl, tl] = score_eng(cap, occ_cpu, skip_frac(nb));
			auto [hm, tm] = score_eng(cap, occ_cpu, skip_frac((nb + 1) / 2));
			acc(r_any, ha, ta);
			acc(r_all, hl, tl);
			acc(r_maj, hm, tm);

			n7171_lo += n7171.front();
			n7272_lo += n7272.front();
			n7171_hi += n7171.back();
			n7272_hi += n7272.back();
			++n_slide_files;
			int ip = i_int;
			if (ip + 1 < ne && std::abs(epsv[ip + 1] - epsv[i_int] - STEP) < 0.02)
				++ip;
			if (ip != i_int && post_at[i_int].size() == post_at[ip].size()) {
				int ch = 0;
				for (size_t j = 0; j < post_at[i_int].size(); ++j)
					if (post_at[ip][j] != post_at[i_int][j]) ++ch;
				n_slide += ch;
				n_post_cmp += int(post_at[i_int].size());
				if (ch == int(post_at[i_int].size()) && ch) ++n_all_shift;
				else if (ch > 0) ++n_partial;
			}

			if (n_print < 3 && cap.name.find("ymmm") != std::string::npos) {
				++n_print;
				std::cout << "  " << cap.name << "  integer δ=" << best.delta
					  << "  ε_D16=" << epsv[i_d16] << " d16=" << d16_score[i_d16]
					  << " eng=" << eng_hit[i_d16] << "/" << tot_e
					  << "  ε_eng=" << epsv[i_eng] << " eng=" << eng_hit[i_eng]
					  << "/" << tot_e
					  << "  71-71@δ " << n7171[i_int] << " 72-72 " << n7272[i_int]
					  << "  @ε_D16 71-71 " << n7171[i_d16] << " 72-72 " << n7272[i_d16]
					  << "\n";
			}
		}

		auto prb = [&](const char* t, Row r) {
			std::cout << "  " << t << "  " << r.ok << '/' << r.n << " files  "
				  << r.hit << '/' << r.tot << "\n";
		};
		std::cout << "Engine P5+newline + dummy if full-table NEED=" << PACKED_NEED
			  << " selects C with CPU at next slot (+26/+54):\n";
		prb("ε = integer δ (current D6)", r_int);
		prb("ε best for CPU D16", r_d16);
		prb("ε best for engine (oracle phase)", r_eng);
		prb("ε worst for engine (adversarial phase)", r_worst);
		prb("dummy if ANY ε in [ε_D16±1] grants", r_any);
		prb("dummy if MAJ ε in [ε_D16±1] grants", r_maj);
		prb("dummy if ALL ε in [ε_D16±1] grants", r_all);
		std::cout << "  sprOff files " << n_spr << "  ε_D16≠integer δ " << n_eps_ne_delta
			  << "  mean |ε_D16−δ|=" << (n_spr ? sum_abs_d16 / n_spr : 0)
			  << "  mean |ε_eng−δ|=" << (n_spr ? sum_abs_eng / n_spr : 0) << "\n";
		std::cout << "  71-71 at integer δ: " << n7171_int << "  at δ+0.5: " << n7171_half
			  << "  72-72 " << n7272_int << "/" << n7272_half << "\n";
		std::cout << "  (δ±2 are both integers so gaps match: 71-71 "
			  << n7171_lo << "/" << n7171_hi << " — that is not a slide test.)\n";
		std::cout << "  +0.1 step from integer δ: T changed " << n_slide << "/" << n_post_cmp
			  << "  files partial " << n_partial << " all-shift " << n_all_shift
			  << " (of " << n_slide_files << ")\n";
		std::cout << "  (Partial T-changes = doubles sliding. All-shift = just δ+1.)\n";
		if (!oracle_fail.empty()) {
			std::cout << "  still wrong at best-engine ε:\n";
			for (auto& s : oracle_fail) std::cout << "    " << s << "\n";
		}
	}
	return 0;
}

// Line-origin consistency check (FINDINGS7 §11.6). Absolute time in a `.txt` is
// `LINE * column + row`, so the line origin is implicit in the column index and
// every consumer has to re-derive it from the `.vcd` refresh bursts. When the
// two disagree by a whole line the CPU fit does not fail loudly — it settles on
// a nonsense `δ` and loses most accesses. Score every CPU capture at `.txt`
// line shifts -1 / 0 / +1 and complain if a non-zero shift wins.
static int run_origin_check(const fs::path& slots_dir, const fs::path& vcd_dir)
{
	std::vector<int> cpu_wait[3];
	for (int m = 0; m < 3; ++m) {
		auto cs = cpu_slots_of(CMD_TABLE[m]);
		cpu_wait[m] = make_wait(cs, CMD_TABLE[m], NEED);
	}

	std::vector<fs::path> files;
	for (const auto& ent : fs::directory_iterator(slots_dir)) {
		if (!ent.is_regular_file()) continue;
		if (is_cpu_file(ent.path().filename().string(), "all"))
			files.push_back(ent.path());
	}
	std::sort(files.begin(), files.end());

	constexpr int ND = DELTA_HI - DELTA_LO + 1;
	int n_run = 0, n_bad = 0;
	std::cout << "line-origin check: CPU fit with the .txt shifted -1 / 0 / +1 lines\n"
		     "against the .vcd. A non-zero winner means the .txt column\n"
		     "numbering is off by that many scan lines.\n";
	for (const auto& txt : files) {
		Capture cap;
		try {
			cap = load_capture(txt, vcd_dir / (txt.stem().string() + ".vcd"));
		} catch (const std::exception&) {
			continue;
		}
		if (cap.obs.empty()) continue;
		++n_run;
		const std::vector<double> t2_orig[2] = {cap.t2[0], cap.t2[1]};
		int best_sh = 0, best_hit = -1, best_bad = 0, best_delta = 0;
		int hit0 = -1;
		for (int sh = -1; sh <= 1; ++sh) {
			// Shifting the request axis by +LINE is the same as shifting
			// the observed accesses by -LINE, and leaves tmin/tmax alone.
			for (int ei = 0; ei < 2; ++ei) {
				cap.t2[ei] = t2_orig[ei];
				for (auto& v : cap.t2[ei]) v += sh * double(LINE);
			}
			int g[2][ND] = {}, e[2][ND] = {}, m[2][ND] = {}, n[2][ND] = {};
			FileFit f{};
			try {
				f = fit_file(cap, cpu_wait, g, e, m, n);
			} catch (...) {
				continue;
			}
			if (sh == 0) hit0 = f.hit;
			if (f.hit > best_hit ||
			    (f.hit == best_hit && f.extra + f.miss < best_bad)) {
				best_hit = f.hit;
				best_bad = f.extra + f.miss;
				best_sh = sh;
				best_delta = f.delta;
			}
		}
		if (best_sh != 0) {
			++n_bad;
			std::cout << "  " << std::left << std::setw(44) << cap.name
				  << std::right << " shift " << std::showpos << best_sh
				  << std::noshowpos << ": " << best_hit << '/'
				  << cap.obs.size() << " (delta=" << best_delta
				  << ")   shift 0: " << hit0 << '/' << cap.obs.size()
				  << "\n";
		}
	}
	std::cout << "  " << n_run << " CPU captures, " << n_bad
		  << " with a line-origin mismatch\n";
	return n_bad ? 1 : 0;
}

// --- --rw: independent /CSR and /CSW pin delays -------------------------
//
// The interleaved rdwrCpu captures are the only ones that carry both edges at
// the same time, so they are the only ones that can separate the two delays.

static std::vector<int> make_posts2(
	const std::vector<double>& tr, int dr,
	const std::vector<double>& tw, int dw,
	const std::vector<int>& wait, int tmin, int tmax)
{
	std::vector<int> posts;
	posts.reserve(tr.size() + tw.size());
	auto add = [&](const std::vector<double>& ts, int d) {
		for (double t2 : ts) {
			int T = int(std::floor(t2)) + d;
			if (first_slot(wait, T) + BUSY < tmin) continue;
			if (T > tmax + NEED) continue;
			posts.push_back(T);
		}
	};
	add(tr, dr);
	add(tw, dw);
	std::sort(posts.begin(), posts.end());
	return posts;
}

static std::vector<fs::path> rw_files(const fs::path& slots_dir, std::string_view sel)
{
	std::vector<fs::path> files;
	for (const auto& ent : fs::directory_iterator(slots_dir)) {
		if (!ent.is_regular_file()) continue;
		auto n = ent.path().filename().string();
		if (n.size() < 4 || n.substr(n.size() - 4) != ".txt") continue;
		if (n.find(sel) == std::string::npos) continue;
		files.push_back(ent.path());
	}
	std::sort(files.begin(), files.end());
	return files;
}

// --needoff=a,b,c: per-mode lookahead, to ask whether the sprites-on offset
// lives in the arbiter's deadline rather than in the pin delay (FINDINGS7 §8.2).
int NEED_OFF[3] = {0, 0, 0};

static void rw_wait(std::vector<int> (&w)[3])
{
	for (int m = 0; m < 3; ++m) {
		auto cs = cpu_slots_of(CMD_TABLE[m]);
		w[m] = make_wait(cs, CMD_TABLE[m], NEED + NEED_OFF[m]);
	}
}

// At a fixed pin delay, list every mismatch with its position in the line: a
// structural error in the slot table shows up as one row repeating, jitter as
// a scatter.
static int run_rwdiag(const fs::path& slots_dir, const fs::path& vcd_dir,
		      std::string_view sel, int force)
{
	std::vector<int> cpu_wait[3];
	rw_wait(cpu_wait);

	std::cout << "mismatches at forced delta=" << force
		  << " (rise edge), NEED=" << NEED << "\n";
	std::map<int, int> miss_row, extra_row, pred_row;
	int tot_n = 0, tot_hit = 0, tot_e = 0, tot_m = 0;
	// A request may already be pending when the capture starts, which the
	// simulator cannot know. Count how much of the residue that explains.
	int n_files = 0, first_miss = 0, second_miss = 0;
	for (const auto& txt : rw_files(slots_dir, sel)) {
		Capture cap;
		try {
			cap = load_capture(txt, vcd_dir / (txt.stem().string() + ".vcd"));
		} catch (const std::exception&) { continue; }
		if (cap.obs.empty()) continue;
		const auto& w = cpu_wait[mode_index(cap.mode)];
		auto posts = make_posts2(cap.t2r[1], force, cap.t2w[1], force,
					 w, cap.tmin, cap.tmax);
		auto sim = vdp_cpu_posts(posts, w, cap.tmin, cap.tmax);
		auto ev = align_d16(sim.pred, cap.obs);
		auto sc = score_d16(ev, cap.tmin, cap.tmax, 0, 0);
		tot_n += int(cap.obs.size());
		tot_hit += sc.hit;
		tot_e += sc.extra;
		tot_m += sc.miss;
		for (auto& p : sim.pred) ++pred_row[mod_line(p.S)];
		++n_files;
		for (auto& e : ev) {
			if (e.kind != Align::Miss) continue;
			if (e.t == cap.obs.front()) ++first_miss;
			else if (cap.obs.size() > 1 && e.t == cap.obs[1]) ++second_miss;
		}
		if (sc.extra + sc.miss == 0) continue;
		std::cout << "  " << cap.name << "  " << sc.hit << "/" << cap.obs.size()
			  << " extra " << sc.extra << " miss " << sc.miss << "\n";
		for (auto& e : ev) {
			if (e.kind == Align::Miss) {
				++miss_row[mod_line(e.t)];
				std::cout << "     miss  obs row " << std::setw(4)
					  << mod_line(e.t) << "  (pred "
					  << mod_line(e.t_other) << ")\n";
			} else if (e.kind == Align::Extra) {
				++extra_row[mod_line(e.t)];
				std::cout << "     extra pred row " << std::setw(4)
					  << mod_line(e.t) << "  (obs "
					  << mod_line(e.t_other) << ")\n";
			}
		}
	}
	std::cout << "\ntotal " << tot_hit << "/" << tot_n << "  extra " << tot_e
		  << " miss " << tot_m << "\n";
	std::cout << "files " << n_files << "  miss on 1st access " << first_miss
		  << "  on 2nd " << second_miss << "\n";
	auto dump = [&](const char* lbl, const std::map<int, int>& h) {
		std::cout << lbl;
		for (auto [k, v] : h) {
			auto it = pred_row.find(k);
			std::cout << ' ' << k << ':' << v << '/'
				  << (it == pred_row.end() ? 0 : it->second);
		}
		std::cout << "\n";
	};
	std::cout << "row histogram as row:errors/times-that-row-was-predicted\n";
	dump("miss  ", miss_row);
	dump("extra ", extra_row);
	return 0;
}

static int run_rw(const fs::path& slots_dir, const fs::path& vcd_dir, std::string_view sel)
{
	std::vector<int> cpu_wait[3];
	rw_wait(cpu_wait);

	std::cout << "independent /CSR and /CSW pin delays, NEED=" << NEED << "\n"
		  << "dr = /CSR edge to arbiter, dw = /CSW edge to arbiter.\n"
		  << "A train with no edges of one kind leaves that delay free,\n"
		  << "so its plateau then spans the whole scan range.\n\n";
	std::cout << std::left << std::setw(38) << "file" << std::right
		  << std::setw(5) << "nR" << std::setw(5) << "nW"
		  << std::setw(6) << "nCSR" << std::setw(6) << "nCSW"
		  << std::setw(6) << "edge" << std::setw(4) << "dr" << std::setw(4) << "dw"
		  << std::setw(9) << "hit" << std::setw(7) << "extra" << std::setw(6) << "miss"
		  << "  dr plateau / dw plateau\n";

	int tot_n = 0, tot_hit = 0, tot_extra = 0, tot_miss = 0, nfile = 0, nperf = 0;
	std::map<int, int> hist_dr, hist_dw, hist_diff;
	for (const auto& txt : rw_files(slots_dir, sel)) {
		Capture cap;
		try {
			cap = load_capture(txt, vcd_dir / (txt.stem().string() + ".vcd"));
		} catch (const std::exception& e) {
			std::cout << "  SKIP " << e.what() << "\n";
			continue;
		}
		if (cap.obs.empty()) continue;
		const auto& w = cpu_wait[mode_index(cap.mode)];
		int nR = 0;
		for (char ch : cap.obs_rd) nR += ch;

		int bi = 1, bdr = 0, bdw = 0, bhit = -1, bex = 0, bmi = 0;
		for (int ei = 1; ei >= 0; --ei) {
			for (int dr = DELTA_LO; dr <= DELTA_HI; ++dr) {
				for (int dw = DELTA_LO; dw <= DELTA_HI; ++dw) {
					auto posts = make_posts2(cap.t2r[ei], dr, cap.t2w[ei], dw,
								 w, cap.tmin, cap.tmax);
					auto sim = vdp_cpu_posts(posts, w, cap.tmin, cap.tmax);
					auto ev = align_d16(sim.pred, cap.obs);
					auto sc = score_d16(ev, cap.tmin, cap.tmax, 0, 0);
					int err = sc.extra + sc.miss;
					if (sc.hit > bhit || (sc.hit == bhit && err < bex + bmi)) {
						bhit = sc.hit; bex = sc.extra; bmi = sc.miss;
						bdr = dr; bdw = dw; bi = ei;
					}
				}
			}
		}
		auto plateau = [&](bool vary_r) {
			std::string s;
			for (int d = DELTA_LO; d <= DELTA_HI; ++d) {
				auto posts = make_posts2(cap.t2r[bi], vary_r ? d : bdr,
							 cap.t2w[bi], vary_r ? bdw : d,
							 w, cap.tmin, cap.tmax);
				auto sim = vdp_cpu_posts(posts, w, cap.tmin, cap.tmax);
				auto ev = align_d16(sim.pred, cap.obs);
				auto sc = score_d16(ev, cap.tmin, cap.tmax, 0, 0);
				if (sc.hit == bhit && sc.extra + sc.miss == bex + bmi)
					s += (s.empty() ? "" : ",") + std::to_string(d);
			}
			return s;
		};
		std::cout << std::left << std::setw(38) << cap.name << std::right
			  << std::setw(5) << nR << std::setw(5) << int(cap.obs.size()) - nR
			  << std::setw(6) << cap.t2r[bi].size()
			  << std::setw(6) << cap.t2w[bi].size()
			  << std::setw(6) << (bi ? "rise" : "fall")
			  << std::setw(4) << bdr << std::setw(4) << bdw
			  << std::setw(5) << bhit << "/" << std::setw(3) << cap.obs.size()
			  << std::setw(7) << bex << std::setw(6) << bmi
			  << "  " << plateau(true) << " / " << plateau(false) << "\n";
		tot_n += int(cap.obs.size());
		tot_hit += bhit;
		tot_extra += bex;
		tot_miss += bmi;
		++nfile;
		if (bex == 0 && bmi == 0) ++nperf;
		++hist_dr[bdr];
		++hist_dw[bdw];
		if (!cap.t2r[bi].empty() && !cap.t2w[bi].empty()) ++hist_diff[bdw - bdr];
	}
	std::cout << "\ntotal " << nperf << "/" << nfile << " perfect  "
		  << tot_hit << "/" << tot_n << "  extra " << tot_extra
		  << " miss " << tot_miss << "\n";
	auto dump = [](const char* lbl, const std::map<int, int>& h) {
		std::cout << lbl;
		for (auto [k, v] : h) std::cout << ' ' << k << ':' << v;
		std::cout << "\n";
	};
	dump("best dr ", hist_dr);
	dump("best dw ", hist_dw);
	dump("dw - dr, both-edge captures only ", hist_diff);
	return 0;
}

// ---------------------------------------------------------------------------
// Trellis reconstruction of the exact request cycles.
//
// Everywhere else this program takes T = floor(t2) + delta with one integer
// delta, and treats the leftover mismatches as measurement noise. Here the pin
// delay is instead one *real* constant phi per capture:
//
//   T_i = floor(t2_i + phi) = floor(t2_i) + delta + [frac(t2_i) >= theta]
//
// with delta = floor(phi) and theta = 1 - frac(phi). One (delta, theta) fixes
// every T_i, and as theta sweeps (0,1] only n+1 assignments exist, so the whole
// family of constant delays is a finite list. Two further unknowns are physical
// rather than noise:
//
//   * a request may already have been pending when the capture started, so the
//     first grant in the .txt can belong to an edge that was never recorded;
//   * t2_i carries analyzer quantisation and refresh-interpolation error, so an
//     edge whose frac(t2_i) sits right next to theta may arm a cycle either way.
//
// For a nominal (delta, theta) the DP below finds the cheapest set of per-edge
// deviations that reproduces the observed grant sequence *exactly*, with cost
// sum|deviation| in VDP cycles. Cost 0 means one real constant delay explains
// the capture with no slack at all; the reported phi interval is then a
// measurement of the pin delay rather than of its integer part.
// --qdepth=2 keeps a second request behind the one being served instead of
// discarding it. The fast 12 T loops are the only captures that can tell the
// two apart, because only there does a request arrive while the arbiter is
// still busy.
int QDEPTH = 1;

struct TrelState {
	int j = 0;      // grants matched so far
	int pend = -1;  // scheduled but not yet taken slot, -1 = arbiter free
	int pend2 = -1; // request latched behind it (QDEPTH 2), as its arming cycle
	int last = -1;  // last taken slot, for the BUSY holdoff
	bool operator<(const TrelState& o) const
	{
		return std::tie(j, pend, pend2, last) <
		       std::tie(o.j, o.pend, o.pend2, o.last);
	}
};

struct TrelSol {
	bool ok = false;
	int cost = 1 << 30;
	bool phantom = false;
	int delta = 0;
	double phi_lo = 0, phi_hi = 0;
	std::vector<std::pair<int, int>> dev; // edge index, deviation in cycles
	std::vector<int> ts;                  // reconstructed request cycle per edge
};

// Earliest arming cycle that schedules onto slot S0, or NO_ARM if no request
// can land there at all (some slots are never the first legal one for any
// cycle). A pre-capture request sits before the origin of the .txt grid, so
// the answer is routinely negative and NO_ARM cannot be -1.
constexpr int NO_ARM = INT_MIN;
static int phantom_arm(const std::vector<int>& wait, int S0)
{
	for (int t = S0 - LINE; t <= S0 - NEED; ++t)
		if (first_slot(wait, t) == S0) return t;
	return NO_ARM;
}

// One (phi, eps) hypothesis: the arming cycle of edge i is
//   T_i = floor(t2_i + e_i + phi),  |e_i| <= eps,
// i.e. one real pin delay shared by every edge, plus a per-edge timing
// tolerance that stands for the analyzer quantisation and the residual error of
// the refresh interpolation. For eps < 1/2 that leaves at most two candidate
// cycles per edge, and only for edges that sit near a clock boundary.
static TrelSol trellis_fit(
	const std::vector<double>& t2s, const std::vector<int>& obs,
	const std::vector<int>& wait, int tmin, int tmax,
	double phi, double eps, bool phantom)
{
	const int n = int(t2s.size());
	const int nobs = int(obs.size());
	TrelSol bad;
	if (nobs == 0) return bad;

	// Emit a due grant, then arm the new request, exactly as vdp_cpu_posts
	// does: the holdoff is measured against the grant that just completed.
	auto advance = [&](TrelState s, int T, TrelState& out) -> bool {
		if (QDEPTH == 1) {
			// One inequality is the whole rule, and the grant can be
			// matched as soon as it is handed out: its slot always lies
			// later than the previous one, so the order the .txt records
			// is the order the requests were taken in.
			int dt = s.last < 0 ? 0 :
				(THRESH_ENGINE_DIST
					? signed_engine_dist(s.last, T, CMD_TABLE[ACC_MODE_INDEX])
					: T - s.last);
			if (s.last >= 0 && dt < thresh_at(s.last)) {
				out = s;
				return true;
			}
			int S = first_slot(wait, T);
			if (S >= tmin && S <= tmax) {
				if (s.j >= nobs || obs[s.j] != S) return false;
				++s.j;
			}
			s.last = S;
			out = s;
			return true;
		}
		// Retire every grant that is already due, promoting a queued
		// request as each one completes.
		while (s.pend >= 0 && s.pend <= T) {
			if (s.pend >= tmin && s.pend <= tmax) {
				if (s.j >= nobs || obs[s.j] != s.pend) return false;
				++s.j;
			}
			s.last = s.pend;
			s.pend = -1;
			if (s.pend2 >= 0) {
				s.pend = first_slot(wait, std::max(s.pend2, s.last));
				s.pend2 = -1;
			}
		}
		if (s.pend < 0) {
			bool drop = false;
			if (s.last >= 0) {
				int dt = T - s.last;
				if (dt >= 0 && dt < BUSY) drop = true;
			}
			if (!drop) s.pend = first_slot(wait, T);
		} else if (QDEPTH >= 2 && s.pend2 < 0) {
			s.pend2 = T;
		}
		out = s;
		return true;
	};

	struct Node {
		TrelState s;
		int cost = 0;
		int prev = -1;
		int dev = 0;
	};
	std::vector<std::vector<Node>> layers(n + 1);
	std::map<TrelState, int> ix;
	auto push = [](std::vector<Node>& lay, std::map<TrelState, int>& m,
		       const TrelState& s, int cost, int prev, int dev) {
		auto it = m.find(s);
		if (it == m.end()) {
			m.emplace(s, int(lay.size()));
			lay.push_back({s, cost, prev, dev});
		} else if (cost < lay[it->second].cost) {
			lay[it->second] = {s, cost, prev, dev};
		}
	};

	TrelState init;
	if (phantom) {
		// The unrecorded request must be a request the arbiter could
		// really have taken: an arming cycle that schedules onto the
		// first observed slot, and one that precedes the first recorded
		// edge -- otherwise the "pending" request is just a free grant.
		int t0 = n ? int(std::floor(t2s[0] + phi - eps)) : obs.front();
		int t = phantom_arm(wait, obs.front());
		if (t == NO_ARM || t >= t0) return bad;
		if (QDEPTH == 1) {
			init.last = obs.front();
			init.j = 1;
		} else {
			init.pend = obs.front();
		}
	}
	push(layers[0], ix, init, 0, -1, 0);

	for (int i = 0; i < n; ++i) {
		std::map<TrelState, int> nix;
		int tlo = int(std::floor(t2s[i] + phi - eps));
		int thi = int(std::floor(t2s[i] + phi + eps));
		int nom = int(std::floor(t2s[i] + phi));
		for (int k = 0; k < int(layers[i].size()); ++k) {
			const Node& nd = layers[i][k];
			for (int T = tlo; T <= thi; ++T) {
				TrelState ns;
				if (!advance(nd.s, T, ns)) continue;
				push(layers[i + 1], nix, ns, nd.cost + std::abs(T - nom),
				     k, T - nom);
			}
		}
		ix = std::move(nix);
		if (layers[i + 1].empty()) return bad;
	}

	int best = -1;
	for (int k = 0; k < int(layers[n].size()); ++k) {
		TrelState s = layers[n][k].s;
		bool bad_tail = false;
		while (s.pend >= 0) {
			if (s.pend >= tmin && s.pend <= tmax) {
				if (s.j >= nobs || obs[s.j] != s.pend) { bad_tail = true; break; }
				++s.j;
			}
			s.last = s.pend;
			s.pend = -1;
			if (s.pend2 >= 0) {
				s.pend = first_slot(wait, std::max(s.pend2, s.last));
				s.pend2 = -1;
			}
		}
		if (bad_tail) continue;
		if (s.j != nobs) continue;
		if (best < 0 || layers[n][k].cost < layers[n][best].cost) best = k;
	}
	if (best < 0) return bad;

	TrelSol sol;
	sol.ok = true;
	sol.cost = layers[n][best].cost;
	sol.phantom = phantom;
	sol.delta = int(std::floor(phi));
	sol.ts.resize(n);
	for (int i = n, k = best; i > 0; --i) {
		const Node& nd = layers[i][k];
		if (nd.dev != 0) sol.dev.emplace_back(i - 1, nd.dev);
		sol.ts[i - 1] = int(std::floor(t2s[i - 1] + phi)) + nd.dev;
		k = nd.prev;
	}
	std::reverse(sol.dev.begin(), sol.dev.end());
	sol.phi_lo = phi;
	sol.phi_hi = phi;
	return sol;
}

// At a fixed eps the candidate cycle set of every edge only changes when some
// t2_i + phi +- eps crosses an integer, so the phi axis is cut into finitely
// many cells on which feasibility is constant. Walking those cells is an exact
// scan of all real pin delays, not a grid search.
static double median_pace(const std::vector<double>& t2s);

// A pre-capture request need not be a free parameter. Every /CSx pulse in a
// capture has the same width and the request loop runs at a fixed pace, so the
// requests that ran before the window opened sit at t2[0] - k*pace. Prepending
// them turns the "one pending request, any reachable slot" hypothesis (a window
// 31 cycles wide with sprites off, 65 with sprites on) into a single cycle.
static std::vector<double> aug_t2(const Capture& cap)
{
	const auto& t2 = cap.t2[1];
	if (PRE_PACE <= 0 || t2.size() < 4) return t2;
	double p = median_pace(t2);
	std::vector<double> out;
	for (int k = PRE_PACE; k >= 1; --k) out.push_back(t2.front() - k * p);
	out.insert(out.end(), t2.begin(), t2.end());
	return out;
}

struct TrelIv {
	double lo = 0, hi = 0;
	int cost = -1; // -1 = no exact reconstruction anywhere in [lo, hi)
	std::vector<std::pair<int, int>> dev;
};

static std::vector<TrelIv> trellis_profile(
	const Capture& cap, const std::vector<int>& wait, int dlo, int dhi,
	double eps, bool phantom)
{
	const auto t2s = aug_t2(cap);
	auto wrap1 = [](double x) { return x - std::floor(x); };
	std::vector<double> bp{0.0};
	for (double t : t2s) {
		double f = wrap1(t);
		bp.push_back(wrap1(eps - f));
		bp.push_back(wrap1(-eps - f));
	}
	std::sort(bp.begin(), bp.end());
	bp.erase(std::unique(bp.begin(), bp.end(),
			     [](double a, double b) { return std::abs(a - b) < 1e-12; }),
		 bp.end());
	bp.push_back(1.0);

	std::vector<TrelIv> out;
	for (int d = dlo; d <= dhi; ++d) {
		for (size_t k = 0; k + 1 < bp.size(); ++k) {
			TrelIv iv;
			iv.lo = d + bp[k];
			iv.hi = d + bp[k + 1];
			if (iv.hi - iv.lo < 1e-12) continue;
			auto s = trellis_fit(t2s, cap.obs, wait, cap.tmin, cap.tmax,
					     0.5 * (iv.lo + iv.hi), eps, phantom);
			if (s.ok) {
				iv.cost = s.cost;
				iv.dev = s.dev;
			}
			out.push_back(iv);
		}
	}
	// Merge neighbours of equal cost so the report shows plateaus.
	std::vector<TrelIv> m;
	for (auto& iv : out) {
		if (!m.empty() && m.back().cost == iv.cost &&
		    std::abs(m.back().hi - iv.lo) < 1e-9) {
			m.back().hi = iv.hi;
			if (iv.cost > 0 && m.back().dev.empty()) m.back().dev = iv.dev;
		} else {
			m.push_back(iv);
		}
	}
	return m;
}

// Every phi that admits an exact reconstruction. eps already bounds the error
// on each edge, so a cell that needs a few edges away from their nearest cycle
// is not a worse fit, only a more informative one; the deviation count is
// reported separately.
static std::vector<TrelIv> pick_feasible(const std::vector<TrelIv>& p)
{
	std::vector<TrelIv> out;
	for (auto& iv : p) if (iv.cost >= 0) out.push_back(iv);
	return out;
}

static int min_cost(const std::vector<TrelIv>& p)
{
	int best = -1;
	for (auto& iv : p)
		if (iv.cost >= 0 && (best < 0 || iv.cost < best)) best = iv.cost;
	return best;
}

static std::string fmt_ivs(const std::vector<TrelIv>& v)
{
	std::ostringstream os;
	os << std::fixed << std::setprecision(3);
	for (size_t i = 0; i < v.size(); ++i) {
		if (i) os << " U ";
		os << '[' << v[i].lo << ',' << v[i].hi << ')';
	}
	return os.str();
}

// Independent check: feed the reconstructed request cycles to the same
// vdp_cpu_posts() the rest of this program uses and demand the grant list come
// back equal to the .txt. The trellis has its own copy of the arbiter rules, so
// this catches a DP that quietly exploits a hole in its own transition.
struct TrelCheck {
	bool ok = false;
	int n_out = 0;  // grants the .txt cannot confirm (outside its window)
	int n_drop = 0; // requests the arbiter discarded (busy or holdoff)
};

// A plain forward simulation, written out rather than shared with the DP so
// that the two implementations can disagree.
// A discarded request, with the slot granted to the request before it. The
// The drop decision uses signed engine distance from sprev to T.
struct DropEv { int T, sprev; };
static std::vector<DropEv>* DROP_LOG = nullptr;

static std::vector<int> queue_sim(const std::vector<int>& posts,
				  const std::vector<int>& wait, int& n_drop,
				  std::map<int, int>* slot_of = nullptr)
{
	n_drop = 0;
	if (QDEPTH == 1) {
		// With one buffer entry the whole rule is a single inequality: take
		// the request only if it arrives at least ACC_THRESH engine cycles
		// after the slot the previous one was granted. Waiting for the pending
		// grant and the holdoff after it are the two halves of that one
		// test, so they need not be simulated separately.
		std::vector<int> out;
		int sprev = -1;
		for (int T : posts) {
			int dt = sprev < 0 ? 0 :
				(THRESH_ENGINE_DIST
					? signed_engine_dist(sprev, T, CMD_TABLE[ACC_MODE_INDEX])
					: T - sprev);
			if (sprev >= 0 && dt < thresh_at(sprev)) {
				++n_drop;
				if (DROP_LOG) DROP_LOG->push_back({T, sprev});
				continue;
			}
			int S = first_slot(wait, T);
			out.push_back(S);
			if (slot_of) (*slot_of)[T] = S;
			sprev = S;
		}
		return out;
	}

	std::vector<int> out;
	int pend = -1, pend2 = -1, last = -1, pend_T = -1;
	auto retire = [&] {
		out.push_back(pend);
		if (slot_of) (*slot_of)[pend_T] = pend;
		last = pend;
		pend = -1;
		if (pend2 >= 0) {
			pend_T = pend2;
			pend = first_slot(wait, std::max(pend2, last));
			pend2 = -1;
		}
	};
	for (int T : posts) {
		while (pend >= 0 && pend <= T) retire();
		if (pend < 0) {
			int dt = last >= 0 ? T - last : BUSY;
			if (dt >= 0 && dt < BUSY) {
				++n_drop;
			} else {
				pend = first_slot(wait, T);
				pend_T = T;
			}
		} else if (QDEPTH >= 2 && pend2 < 0) {
			pend2 = T;
		} else {
			++n_drop;
		}
	}
	while (pend >= 0) retire();
	return out;
}

static TrelCheck trellis_verify(const Capture& cap, const std::vector<int>& wait,
				const TrelSol& sol)
{
	TrelCheck ck;
	std::vector<int> posts = sol.ts;
	if (sol.phantom) {
		int T = phantom_arm(wait, cap.obs.front());
		if (T == NO_ARM) return ck;
		posts.insert(posts.begin(), T);
	}
	std::sort(posts.begin(), posts.end());
	auto all = queue_sim(posts, wait, ck.n_drop);
	std::vector<int> got;
	for (int S : all) if (S >= cap.tmin && S <= cap.tmax) got.push_back(S);
	ck.ok = got == cap.obs;
	ck.n_out = int(all.size()) - int(got.size());
	return ck;
}

using IvSet = std::vector<std::pair<double, double>>;

static IvSet iv_set(const std::vector<TrelIv>& v)
{
	IvSet s;
	for (auto& iv : v) s.emplace_back(iv.lo, iv.hi);
	return s;
}

static IvSet iv_norm(IvSet s)
{
	std::sort(s.begin(), s.end());
	IvSet out;
	for (auto& x : s) {
		if (x.second - x.first <= 1e-9) continue;
		if (!out.empty() && x.first <= out.back().second + 1e-9)
			out.back().second = std::max(out.back().second, x.second);
		else
			out.push_back(x);
	}
	return out;
}

static IvSet iv_intersect(const IvSet& a, const IvSet& b)
{
	IvSet out;
	for (auto& x : a)
		for (auto& y : b) {
			double lo = std::max(x.first, y.first);
			double hi = std::min(x.second, y.second);
			if (hi - lo > 1e-9) out.emplace_back(lo, hi);
		}
	return iv_norm(std::move(out));
}

// Which single phi satisfies the most captures. Intersecting greedily in file
// order would make the answer depend on that order, so instead every endpoint
// is a candidate boundary and each elementary interval is scored.
struct Cover {
	double lo = 0, hi = 0;
	int n = -1;
};

static Cover best_cover(const std::vector<IvSet>& sets)
{
	std::vector<double> pts;
	for (auto& s : sets)
		for (auto& iv : s) {
			pts.push_back(iv.first);
			pts.push_back(iv.second);
		}
	std::sort(pts.begin(), pts.end());
	pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
	Cover best;
	for (size_t k = 0; k + 1 < pts.size(); ++k) {
		if (pts[k + 1] - pts[k] < 1e-9) continue;
		double mid = 0.5 * (pts[k] + pts[k + 1]);
		int n = 0;
		for (auto& s : sets)
			for (auto& iv : s)
				if (iv.first <= mid && mid < iv.second) { ++n; break; }
		if (n > best.n) best = {pts[k], pts[k + 1], n};
	}
	return best;
}

static std::string fmt_set(const IvSet& s)
{
	if (s.empty()) return "EMPTY";
	std::ostringstream os;
	os << std::fixed << std::setprecision(3);
	for (size_t i = 0; i < s.size(); ++i) {
		if (i) os << " U ";
		os << '[' << s[i].first << ',' << s[i].second << ')';
	}
	return os.str();
}

// Tolerances are held in 1/256 of a VDP cycle so that a cache key is exact.
constexpr int EPS_Q[] = {0, 2, 4, 8, 12, 16, 20, 24, 32, 40, 48, 64, 80, 96, 128};
constexpr double EPS_UNIT = 1.0 / 256.0;
// The logic analyzer ran at 80 MHz against the 21.477 MHz VDP clock, so one
// sample is this many VDP cycles. It is the natural unit for eps: quantisation
// of the /CSx edge and of the refresh anchors both come in this size, and the
// VDP's own input setup window and the slow drift between the two crystals add
// to it. A tolerance of a sample or so is measurement, not model error.

struct TrelFile {
	std::string name;
	Mode mode = Mode::DispOff;
	int ncs = 0, nacc = 0;
	int eps_q = -1;   // smallest tolerance with an exact reconstruction
	bool phantom = false;
	IvSet set;        // phi set at eps_q
	int ndev = 0;     // edges that had to leave the nearest cycle
	double pace = 0;  // median VDP cycles between consecutive requests
	TrelCheck chk;
};

// Median rather than mean: the loops have a longer iteration every so often
// (the outer counter), and the median gives the inner pace on its own.
static double median_pace(const std::vector<double>& t2s)
{
	if (t2s.size() < 2) return 0;
	std::vector<double> g;
	for (size_t i = 1; i < t2s.size(); ++i) g.push_back(t2s[i] - t2s[i - 1]);
	std::sort(g.begin(), g.end());
	return g[g.size() / 2];
}

// Everything a reconstruction depends on except the tolerance and the
// pre-capture request, which are searched over and so are named separately.
static std::string trel_key()
{
	// v4: a pre-capture request is no longer rejected for having a negative
	// arming cycle, which every one of them has when the first grant sits
	// early in the line, so feasibility itself changed.
	std::string k = "v7_n" + std::to_string(NEED) + "_o" + std::to_string(NEED_OFF[0]) +
			"," + std::to_string(NEED_OFF[1]) + "," + std::to_string(NEED_OFF[2]);
	for (auto [r, n] : NEED_ROW) k += "_r" + std::to_string(r) + ":" + std::to_string(n);
	for (auto [r, n] : THRESH_ROW) k += "_R" + std::to_string(r) + ":" + std::to_string(n);
	if (THRESH_ENGINE_DIST) k += "_E";
	k += "_q" + std::to_string(QDEPTH);
	// The preprocessing variants change t2 itself, so they are part of the key.
	if (!ANCHOR_FIX) k += "_A";
	if (!PULSE_FIX) k += "_P";
	if (!WIDTH_FIX) k += "_W";
	if (PRE_PACE) k += "_x" + std::to_string(PRE_PACE);
	if (PAD_SILICON)
		k += "_S" + std::to_string(PAD_SHIFT) + ":" + std::to_string(PAD_PAIR);
	for (int m = 0; m < 3; ++m)
		if (THRESH_MODE[m] != BUSY)
			k += "_t" + std::to_string(m) + ":" + std::to_string(THRESH_MODE[m]);
	return k;
}

struct TrelCache {
	fs::path path;
	std::map<std::string, std::string> line;
	bool dirty = false;

	void load()
	{
		if (!fs::exists(path)) return;
		std::ifstream in(path);
		std::string l;
		while (std::getline(in, l)) {
			if (l.empty() || l[0] == '#') continue;
			std::istringstream is(l);
			std::string nm, key;
			is >> nm >> key;
			line[nm + ' ' + key] = l;
		}
	}
	void save()
	{
		if (!dirty) return;
		std::ofstream os(path);
		os << "# name  eE_pP_nN_oA,B,C[_rROW:N]*_qQ  ndev  n_intervals  (phi_lo phi_hi)*\n"
		      "#   E = eps in 1/256 VDP cycle, P = a pre-capture request was allowed,\n"
		      "#   N = NEED, A,B,C = per-mode NEED offset, ROW:N = per-slot lookahead,\n"
		      "#   Q = request buffer depth.  ndev = fewest edges off their nearest\n"
		      "#   cycle.  The phi intervals are half-open and in VDP cycles; zero of\n"
		      "#   them means no exact reconstruction exists under that key.\n";
		for (auto& [k, l] : line) os << l << "\n";
	}
};

// One (capture, eps, phantom) query, memoised: the phi set and the deviation
// count for an exact reconstruction, or an empty set if there is none.
static bool trel_query(TrelCache& cache, const Capture& cap,
		       const std::vector<int>& wait, int eps_q, bool phantom,
		       IvSet& set, int& ndev)
{
	std::string key = "e" + std::to_string(eps_q) + "_p" + (phantom ? "1" : "0") +
			  "_" + trel_key();
	auto it = cache.line.find(cap.name + ' ' + key);
	if (it != cache.line.end()) {
		std::istringstream is(it->second);
		std::string nm, k;
		int niv;
		is >> nm >> k >> ndev >> niv;
		set.clear();
		for (int i = 0; i < niv; ++i) {
			double lo, hi;
			is >> lo >> hi;
			set.emplace_back(lo, hi);
		}
		return !set.empty();
	}
	auto prof = trellis_profile(cap, wait, DELTA_LO, DELTA_HI,
				    eps_q * EPS_UNIT, phantom);
	int c = min_cost(prof); // -1 = not one cell is feasible
	auto best = pick_feasible(prof);
	set = iv_norm(iv_set(best));
	ndev = c < 0 ? 0 : c;
	std::ostringstream os;
	os << cap.name << ' ' << key << ' ' << ndev << ' ' << set.size()
	   << std::fixed << std::setprecision(5);
	for (auto& s : set) os << ' ' << s.first << ' ' << s.second;
	cache.line[cap.name + ' ' + key] = os.str();
	cache.dirty = true;
	return !set.empty();
}

// The reconstructed request cycles themselves. The cache next door only says
// which pin delays are feasible; this says, for one of them, the exact VDP
// cycle on which the arbiter saw each request and the slot it then took, which
// is what a downstream consumer (a reference test, or openMSX) needs.
static void trellis_write_requests(std::ostream& os, const Capture& cap,
				   const std::vector<int>& wait, const TrelSol& sol,
				   double phi, double eps, const std::string& key)
{
	std::vector<int> posts = sol.ts;
	int phantom_T = -1;
	if (sol.phantom) {
		phantom_T = phantom_arm(wait, cap.obs.front());
		if (phantom_T < 0) return;
		posts.insert(posts.begin(), phantom_T);
	}
	std::sort(posts.begin(), posts.end());
	int n_drop = 0;
	std::map<int, int> slot_of;
	queue_sim(posts, wait, n_drop, &slot_of);

	os << "\ncapture " << cap.name << "  mode " << mode_name(cap.mode) << "  key "
	   << key << "\n  phi " << std::fixed << std::setprecision(5) << phi << "  eps "
	   << eps << "  edges " << cap.t2[1].size() << "  grants " << cap.obs.size()
	   << "  pre-capture request " << (sol.phantom ? "yes" : "no") << "\n";
	if (sol.phantom) {
		auto it = slot_of.find(phantom_T);
		os << "  -1          -  " << std::setw(8) << phantom_T << std::setw(9)
		   << (it == slot_of.end() ? -1 : it->second) << "     0  unrecorded\n";
	}
	const auto t2v = aug_t2(cap);
	for (size_t i = 0; i < t2v.size() && i < sol.ts.size(); ++i) {
		double t2 = t2v[i];
		int T = sol.ts[i];
		auto it = slot_of.find(T);
		int S = it == slot_of.end() ? -1 : it->second;
		os << std::setw(4) << i << std::setw(11) << std::setprecision(3) << t2
		   << std::setw(8) << T << std::setw(9) << S << std::setw(6)
		   << (T - int(std::floor(t2 + phi)));
		if (i < size_t(PRE_PACE)) os << "  before the capture";
		else if (S < 0) os << "  dropped";
		else if (S < cap.tmin || S > cap.tmax) os << "  outside";
		os << "\n";
	}
}

// The reconstruction itself, edge by edge, so that a deviation can be read
// against the fractional position of the edge: quantisation noise sits next to
// a clock boundary, a model gap does not.
static void trellis_dump(const Capture& cap, const std::vector<int>& wait,
			 double phi, double eps, bool phantom)
{
	auto sol = trellis_fit(cap.t2[1], cap.obs, wait, cap.tmin, cap.tmax,
			       phi, eps, phantom);
	std::cout << "\n" << cap.name << "  phi " << std::fixed << std::setprecision(4)
		  << phi << "  eps " << eps << "  phantom " << (phantom ? "yes" : "no")
		  << (sol.ok ? "" : "   NO SOLUTION") << "\n";
	if (!sol.ok) return;
	std::cout << "  i        t2   frac    Tnom     T  dev     slot   row  obs row  gap\n";
	std::vector<int> posts = sol.ts;
	if (phantom) {
		int T = phantom_arm(wait, cap.obs.front());
		if (T != NO_ARM) posts.insert(posts.begin(), T);
		std::cout << "  (one request already pending at capture start, armed at "
			  << T << ", slot " << cap.obs.front() << ")\n";
	}
	auto sim = vdp_cpu_posts(posts, wait, -(1 << 30), 1 << 30);
	size_t si = 0;
	int prev_T = 0;
	for (size_t i = 0; i < cap.t2[1].size(); ++i) {
		double t2 = cap.t2[1][i];
		int nom = int(std::floor(t2 + phi));
		int T = sol.ts[i];
		while (si < sim.pred.size() && sim.pred[si].T < T) ++si;
		bool served = si < sim.pred.size() && sim.pred[si].T == T;
		int S = served ? sim.pred[si].S : -1;
		std::cout << std::setw(3) << i << std::setw(10) << std::setprecision(2) << t2
			  << std::setw(7) << std::setprecision(3) << (t2 - std::floor(t2))
			  << std::setw(8) << nom << std::setw(6) << T << std::setw(5)
			  << (T - nom);
		if (S < 0) {
			std::cout << "   dropped by the arbiter\n";
		} else {
			std::cout << std::setw(9) << S << std::setw(6) << mod_line(S)
				  << std::setw(9);
			auto it = std::lower_bound(cap.obs.begin(), cap.obs.end(), S);
			if (it != cap.obs.end() && *it == S) std::cout << mod_line(S);
			else std::cout << (S < cap.tmin || S > cap.tmax ? "(outside)" : "MISSING");
			std::cout << std::setw(6) << (i ? T - prev_T : 0) << "\n";
		}
		prev_T = T;
	}
}

// rdwrCpu interleaves IN and OUT, so its request train is /CSR and /CSW merged
// and each entry needs its own tag. The merged train was debounced after the
// merge, so match on proximity rather than on equality.
static char edge_kind(const Capture& cap, double t2)
{
	auto near = [&](const std::vector<double>& v) {
		double best = 1e18;
		for (double x : v) best = std::min(best, std::abs(x - t2));
		return best;
	};
	return near(cap.t2r[1]) <= near(cap.t2w[1]) ? 'r' : 'w';
}

// The sibling file: the integer request cycles for one capture, and nothing
// else. Feeding these to the arbiter model has to reproduce the CPU accesses of
// the .txt exactly, with no reference to the .vcd -- see --fromreq.
static void write_reqfile(const fs::path& slots_dir, const Capture& cap,
			  const std::vector<int>& wait, const TrelSol& sol)
{
	std::vector<std::pair<int, char>> req;
	bool rdwr = cap.name.find("rdwr") != std::string::npos;
	if (sol.phantom) {
		int T = phantom_arm(wait, cap.obs.front());
		if (T == NO_ARM) return;
		// Unrecorded, so its kind can only come from the access it caused.
		req.emplace_back(T, cap.obs_rd.empty() || cap.obs_rd.front() ? 'r' : 'w');
	}
	const auto t2v = aug_t2(cap);
	for (size_t i = 0; i < sol.ts.size() && i < t2v.size(); ++i)
		req.emplace_back(sol.ts[i], rdwr ? edge_kind(cap, t2v[i]) : 'r');
	std::sort(req.begin(), req.end());

	auto stem = fs::path(cap.name).stem().string();
	std::ofstream os(slots_dir / (stem + ".cpureq"));
	os << "# " << stem << "  CPU VRAM request cycles\n"
	   << "# Comments carry no information; everything the arbiter needs is on a\n"
	   << "# keyword line. 'requests' comes last and is followed by that many\n"
	   << "# body lines, one per request: the VDP cycle on which the arbiter\n"
	   << "# latched it, in RAS numbering with the same origin as the .txt grid\n"
	   << "# (cas = line*col + row), and for an interleaved rdwrCpu capture an\n"
	   << "# r or w suffix saying whether it was a read or a write.\n"
	   << "# Requests that cause no visible access are included and must be kept:\n"
	   << "# the arbiter drops some, and some are granted outside the .txt\n"
	   << "# window. Which ones is derived by the model, not recorded here.\n"
	   << "# 'margin' is the whole drop rule: a request is taken only if it\n"
	   << "# arrives at least that many cycles after the slot the request\n"
	   << "# before it was granted. It is negative with sprites on.\n"
	   << "capture " << stem << "\n"
	   << "mode " << mode_name(cap.mode) << "\n"
	   << "line " << LINE << "\n"
	   << "need " << (NEED + NEED_OFF[mode_index(cap.mode)]) << "\n";
	for (auto [r, n] : NEED_ROW) os << "need_row " << r << ' ' << n << "\n";
	os << "margin " << ACC_THRESH << "\n"
	   << "buffer " << QDEPTH << "\n"
	   << "policy drop-new\n"
	   << "kind " << (rdwr ? "rw" : "r") << "\n"
	   << "requests " << req.size() << "\n";
	for (auto [T, k] : req) {
		os << T;
		if (rdwr) os << ' ' << k;
		os << '\n';
	}
}

struct ReqFile {
	std::string capture;
	Mode mode = Mode::DispOff;
	int line = LINE;
	int need = 16;
	int margin = BUSY; // cycles a request must clear the previous grant by
	int buffer = 1;
	std::string policy = "drop-new";
	std::string kind = "r";
	int declared = -1; // the 'requests' count, a checksum on the body
	std::map<int, int> need_row;
	std::vector<int> req;
	std::vector<char> tag;
};

// One parser, so that --fromreq and --checkreq cannot disagree about the file.
static ReqFile read_reqfile(const fs::path& p)
{
	ReqFile r;
	std::ifstream is(p);
	std::string ln;
	bool body = false; // set by 'requests', which must come last
	while (std::getline(is, ln)) {
		if (ln.empty() || ln[0] == '#') continue;
		std::istringstream ls(ln);
		if (body) {
			int T;
			if (!(ls >> T))
				throw std::runtime_error(p.filename().string() +
							 ": body line is not a cycle: " + ln);
			std::string t;
			r.req.push_back(T);
			r.tag.push_back((ls >> t) && !t.empty() ? t[0] : 'r');
			continue;
		}
		std::string kw;
		ls >> kw;
		if (kw == "capture") ls >> r.capture;
		else if (kw == "mode") { std::string m; ls >> m; r.mode = mode_of(m); }
		else if (kw == "line") ls >> r.line;
		else if (kw == "need") ls >> r.need;
		else if (kw == "margin" || kw == "busy") ls >> r.margin;
		else if (kw == "buffer") ls >> r.buffer;
		else if (kw == "policy") ls >> r.policy;
		else if (kw == "kind") ls >> r.kind;
		else if (kw == "need_row") { int a, b; ls >> a >> b; r.need_row[a] = b; }
		else if (kw == "requests") { ls >> r.declared; body = true; }
		else {
			throw std::runtime_error(p.filename().string() +
						 ": unknown keyword '" + kw + "'");
		}
	}
	if (!body) throw std::runtime_error(p.filename().string() + ": no 'requests' line");
	if (r.line != LINE)
		throw std::runtime_error(r.capture + ": line " + std::to_string(r.line) +
					 " is not the built-in " + std::to_string(LINE));
	if (r.declared >= 0 && r.declared != int(r.req.size()))
		throw std::runtime_error(r.capture + ": declared " +
					 std::to_string(r.declared) + " requests, found " +
					 std::to_string(r.req.size()));
	return r;
}

// Is a suspect edge suspect in the .vcd, or only after the timebase has been
// reconstructed? The raw VCD timestamp is wall time straight from the
// analyzer; t2 is that timestamp mapped through the refresh anchors. Printing
// the two gaps side by side separates a bad capture from a bad interpolation:
// a bad capture is off in both columns, a warped timebase only in t2.
static int run_rawdump(const fs::path& slots_dir, const fs::path& vcd_dir)
{
	for (const auto& ent : fs::directory_iterator(slots_dir)) {
		auto name = ent.path().filename().string();
		if (!is_cpu_file(name, "all")) continue;
		if (ONLY.empty() || name.find(ONLY) == std::string::npos) continue;
		Capture cap;
		try {
			cap = load_capture(ent.path(),
					   vcd_dir / (ent.path().stem().string() + ".vcd"));
		} catch (const std::exception& e) {
			std::cout << name << ": " << e.what() << "\n";
			continue;
		}
		const auto& t2 = cap.t2[1];
		if (t2.size() != cap.raw_r.size()) {
			std::cout << "raw and t2 are not parallel\n";
			continue;
		}
		double pace = median_pace(t2);
		// The width of a pulse and the position of its own falling edge say
		// whether a displaced rise moved with its pulse or on its own.
		auto pulse = [&](int rise) {
			int bw = -1, bf = 0;
			for (int k = 0; k < 2; ++k) {
				auto it = std::upper_bound(cap.raw_f[k].begin(),
							   cap.raw_f[k].end(), rise);
				if (it == cap.raw_f[k].begin()) continue;
				int w = (rise - *(it - 1)) / UNITS_PER_SAMPLE;
				if (w > 0 && (bw < 0 || w < bw)) {
					bw = w;
					bf = *(it - 1);
				}
			}
			return std::pair{bw, bf};
		};
		std::set<size_t> show;
		for (size_t i = 1; i < t2.size(); ++i) {
			double gw = (cap.raw_r[i] - cap.raw_r[i - 1]) * VDP_PER_UNIT - pace;
			double gt = t2[i] - t2[i - 1] - pace;
			if (std::abs(gw) < 4 && std::abs(gt) < 4) continue;
			for (size_t k = i > 2 ? i - 2 : 0; k <= i + 2 && k < t2.size(); ++k)
				show.insert(k);
		}
		std::cout << name << ": " << t2.size() << " rising /CSx edges, pace "
			  << std::fixed << std::setprecision(2) << pace << " cycles\n"
			  << "  edge      rise_us   gap_wall   gap_t2   width  fall_gap_wall\n";
		size_t last = 0;
		for (size_t i : show) {
			if (i && i != last + 1) std::cout << "       ...\n";
			last = i;
			// The nominal clock ratio turns wall time into cycles without
			// consulting a single refresh burst.
			double gw = i ? (cap.raw_r[i] - cap.raw_r[i - 1]) * VDP_PER_UNIT - pace
				      : 0.0;
			double gt = i ? t2[i] - t2[i - 1] - pace : 0.0;
			auto [w, f] = pulse(cap.raw_r[i]);
			auto [w0, f0] = i ? pulse(cap.raw_r[i - 1]) : std::pair{-1, 0};
			std::cout << std::setw(6) << i << std::setw(13)
				  << std::setprecision(3) << cap.raw_r[i] * 1e-4
				  << std::setw(11) << std::setprecision(2) << gw
				  << std::setw(9) << gt << std::setw(8) << w;
			if (w0 > 0 && f0)
				std::cout << std::setw(15) << (f - f0) * VDP_PER_UNIT - pace;
			std::cout << "\n";
		}
	}
	return 0;
}

// Every decision the arbiter makes, from the .cpureq files, so that the
// mispredicted ones can be compared against a base rate rather than just
// described. Per decision: the mode, the margin it was decided on, and the
// geometry of the slot the margin was measured from -- its row and the gap in
// front of that row, which is what distinguishes the regular part of the
// lattice from the blanking region.
static int run_margins(const fs::path& slots_dir)
{
	std::vector<int> cpu_wait[3];
	rw_wait(cpu_wait);
	// row_got is the row the request actually reached, which is what makes a
	// contradiction checkable: two files disagree only if the same margin at
	// the same previous row leads to the same slot in one and to nothing in
	// the other.
	std::cout << "mode margin taken row_prev gap_prev row_got capture\n";
	for (const auto& ent : fs::directory_iterator(slots_dir)) {
		if (ent.path().extension() != ".cpureq") continue;
		ReqFile rf;
		try {
			rf = read_reqfile(ent.path());
		} catch (const std::exception&) { continue; }
		int mi = mode_index(rf.mode);
		NEED_ROW = rf.need_row;
		auto wait = make_wait(cpu_slots_of(CMD_TABLE[mi]), CMD_TABLE[mi], rf.need);
		const auto& slots = cpu_slots_of(CMD_TABLE[mi]);
		auto gap_before = [&](int row) {
			int prev = slots.back() - LINE;
			for (int s : slots)
				if (s < row) prev = s;
			return row - prev;
		};
		int sprev = -1;
		for (int T : rf.req) {
			if (sprev >= 0) {
				int row = mod_line(sprev);
				bool taken = T - sprev >= rf.margin;
				std::cout << mode_name(rf.mode) << ' ' << (T - sprev) << ' '
					  << (taken ? 1 : 0) << ' ' << row << ' '
					  << gap_before(row) << ' '
					  << (taken ? mod_line(first_slot(wait, T)) : -1)
					  << ' ' << ent.path().stem().string() << "\n";
			}
			if (sprev < 0 || T - sprev >= rf.margin) sprev = first_slot(wait, T);
		}
	}
	return 0;
}

// Does the pulse width really measure the sub-sample phase? The request loop
// emits at a fixed pace, so inside one run the true rise times are an
// arithmetic progression and a straight-line fit leaves only measurement
// error. That is an arbiter-free test of the correction: if it is right the
// residual shrinks, and if the sign is wrong it grows.
static int run_widthcheck(const fs::path& slots_dir, const fs::path& vcd_dir)
{
	double s2 = 0;
	long n = 0, nruns = 0;
	// The straight-line residual of the rise train cannot see a timebase that
	// is smooth but wrong, so the anchors are checked against the fitted line
	// separately. Consistent to within their own sample quantisation, ~0.13
	// cycles, means the line is not hiding a drift.
	double aw_max = 0, aw_sum = 0;
	long aw_n = 0;
	for (const auto& ent : fs::directory_iterator(slots_dir)) {
		if (!is_cpu_file(ent.path().filename().string(), "all")) continue;
		Capture cap;
		try {
			cap = load_capture(ent.path(),
					   vcd_dir / (ent.path().stem().string() + ".vcd"));
		} catch (const std::exception&) { continue; }
		aw_max = std::max(aw_max, cap.anchor_worst);
		aw_sum += cap.anchor_worst;
		++aw_n;
		const auto& t2 = cap.t2[1];
		if (t2.size() < 8) continue;
		double pace = median_pace(t2);
		for (size_t i = 0; i < t2.size();) {
			size_t j = i + 1;
			while (j < t2.size() && std::abs(t2[j] - t2[j - 1] - pace) < 10) ++j;
			if (j - i >= 8) {
				// Least squares over the run: slope and offset are both
				// free, so a clock-ratio error cannot leak into the residual.
				double m = double(j - i), sx = 0, sy = 0, sxx = 0, sxy = 0;
				for (size_t k = i; k < j; ++k) {
					double x = double(k - i), y = t2[k];
					sx += x; sy += y; sxx += x * x; sxy += x * y;
				}
				double b = (m * sxy - sx * sy) / (m * sxx - sx * sx);
				double a = (sy - b * sx) / m;
				for (size_t k = i; k < j; ++k) {
					double r = t2[k] - (a + b * double(k - i));
					s2 += r * r;
					++n;
				}
				++nruns;
			}
			i = j;
		}
	}
	double rms = std::sqrt(s2 / double(n));
	std::cout << "straight-line residual of the /CSx rise train, "
		  << (WIDTH_FIX ? "with" : "without") << " the width correction\n"
		  << "  " << n << " edges in " << nruns << " constant-pace runs\n"
		  << "  rms " << std::fixed << std::setprecision(4) << rms << " cycles = "
		  << std::setprecision(3) << rms / VDP_PER_SAMPLE << " samples\n";
	if (LSQ_TIME && aw_n)
		std::cout << "worst refresh anchor off the fitted line, per capture: mean "
			  << std::setprecision(3) << aw_sum / double(aw_n) << " cycles ("
			  << aw_sum / double(aw_n) / VDP_PER_SAMPLE << " samples), max "
			  << aw_max << " cycles (" << aw_max / VDP_PER_SAMPLE
			  << " samples), over " << aw_n << " captures\n"
			  << "anchors rejected as miscounted refreshes: " << LSQ_DROPPED
			  << " of " << LSQ_ANCHORS << "\n";
	return 0;
}

// The request loop runs at a fixed pace, so an edge that sits off the pace and
// whose neighbour makes the time back is a mismeasured edge, not a real
// request. Reported for every capture, so that the rate among the captures
// that fit can be compared with the rate among those that do not.
static int run_pacescan(const fs::path& slots_dir, const fs::path& vcd_dir)
{
	std::cout << "Displaced /CSx edges: an edge whose gap to its predecessor is off\n"
		     "the loop pace by more than 4 cycles and whose successor makes that\n"
		     "time back to within 2 cycles. fit = an exact reconstruction exists.\n\n"
		  << std::left << std::setw(34) << "capture" << std::right << std::setw(6)
		  << "edges" << std::setw(8) << "pace" << std::setw(5) << "bad"
		  << std::setw(9) << "worst" << std::setw(5) << "fit" << "\n";
	int nf[2] = {0, 0}, nbadf[2] = {0, 0}, nbade[2] = {0, 0};
	int nglitchf = 0, nglitche = 0;
	for (const auto& ent : fs::directory_iterator(slots_dir)) {
		auto name = ent.path().filename().string();
		if (!is_cpu_file(name, "all")) continue;
		Capture cap;
		int h0 = GLITCH_HIGH;
		try {
			cap = load_capture(ent.path(),
					   vcd_dir / (ent.path().stem().string() + ".vcd"));
		} catch (const std::exception&) { continue; }
		if (GLITCH_HIGH > h0) {
			++nglitchf;
			nglitche += GLITCH_HIGH - h0;
			std::cout << "  spike inside an access: " << (GLITCH_HIGH - h0)
				  << " short high pulse(s) cancelled in "
				  << ent.path().stem().string() << "\n";
		}
		const auto& t2 = cap.t2[1];
		if (t2.size() < 4) continue;
		auto req = ent.path();
		req.replace_extension(".cpureq");
		int fit = fs::exists(req) ? 1 : 0;
		double pace = median_pace(t2);
		int nbad = 0;
		double worst = 0;
		for (size_t i = 1; i + 1 < t2.size(); ++i) {
			double d0 = t2[i] - t2[i - 1] - pace;
			double d1 = t2[i + 1] - t2[i] - pace;
			if (std::abs(d0) > 4 && std::abs(d0 + d1) < 2) {
				++nbad;
				if (std::abs(d0) > std::abs(worst)) worst = d0;
			}
		}
		++nf[fit];
		if (nbad) ++nbadf[fit];
		nbade[fit] += nbad;
		if (nbad)
			std::cout << std::left << std::setw(34)
				  << ent.path().stem().string() << std::right << std::setw(6)
				  << t2.size() << std::setw(8) << std::fixed
				  << std::setprecision(1) << pace << std::setw(5) << nbad
				  << std::setw(9) << std::setprecision(2) << worst
				  << std::setw(5) << (fit ? "yes" : "NO") << "\n";
	}
	for (int f = 0; f < 2; ++f)
		std::cout << "\n" << (f ? "captures that fit:     " : "captures that do not:  ")
			  << nbadf[f] << " of " << nf[f] << " hold a displaced edge, "
			  << nbade[f] << " edges in total";
	std::cout << "\n";
	return 0;
}

// What is wrong with the captures that have no exact reconstruction. For each,
// scan the pin delay finely, forward-simulate, align the grants against the
// .txt and report the best alignment plus the character of the first
// divergence. A capture is identified as a failure by having no .cpureq.
static int run_faildiag(const fs::path& slots_dir, const fs::path& vcd_dir)
{
	std::vector<int> cpu_wait[3];
	rw_wait(cpu_wait);

	std::vector<fs::path> files;
	for (const auto& ent : fs::directory_iterator(slots_dir)) {
		auto name = ent.path().filename().string();
		if (!is_cpu_file(name, "all")) continue;
		if (!ONLY.empty() && name.find(ONLY) == std::string::npos) continue;
		auto req = ent.path();
		req.replace_extension(".cpureq");
		if (ONLY.empty() && fs::exists(req)) continue;
		files.push_back(ent.path());
	}
	std::sort(files.begin(), files.end());

	std::cout << "Captures with no exact reconstruction, best alignment over a fine\n"
		     "pin-delay scan. deficit = observed grants minus predicted ones:\n"
		     "positive means the model discarded requests the VDP served.\n\n"
		  << std::left << std::setw(31) << "capture" << std::right << std::setw(4)
		  << "obs" << std::setw(5) << "def" << std::setw(6) << "extra"
		  << std::setw(5) << "miss" << std::setw(6) << "drop" << std::setw(7)
		  << "phi" << std::setw(4) << "pre" << std::setw(5) << "ok"
		  << "   first divergence\n";

	for (const auto& txt : files) {
		Capture cap;
		try {
			cap = load_capture(txt, vcd_dir / (txt.stem().string() + ".vcd"));
		} catch (const std::exception&) { continue; }
		if (cap.obs.empty()) continue;
		const auto& w = cpu_wait[mode_index(cap.mode)];
		ACC_MODE_INDEX = mode_index(cap.mode);
		ACC_THRESH = THRESH_MODE[ACC_MODE_INDEX];

		int b_extra = 1 << 30, b_miss = 0, b_pre = -1, b_got = 0, b_drop = 0;
		double b_phi = 0;
		bool b_ph = false;
		std::string b_first;
		int arm = phantom_arm(w, cap.obs.front());
		for (int qq = 0; qq <= 2 * 4 * 256 + 1; ++qq) {
			// phi in [9, 13) at 1/256 cycle, with and without a request
			// already pending when the capture opened.
			bool ph = qq & 1;
			int q = qq >> 1;
			if (ph && arm == NO_ARM) continue;
			double phi = 9.0 + q / 256.0;
			std::vector<int> posts;
			posts.reserve(cap.t2[1].size() + 1);
			if (ph) posts.push_back(arm);
			for (double t : cap.t2[1]) posts.push_back(int(std::floor(t + phi)));
			std::sort(posts.begin(), posts.end());
			int nd = 0;
			std::map<int, int> slot_of;
			queue_sim(posts, w, nd, &slot_of);
			std::vector<int> got;
			for (auto [T, S] : slot_of)
				if (S >= cap.tmin && S <= cap.tmax) got.push_back(S);
			std::sort(got.begin(), got.end());
			got.erase(std::unique(got.begin(), got.end()), got.end());

			size_t i = 0, j = 0;
			int extra = 0, miss = 0, pre = -1;
			std::string first;
			while (i < got.size() || j < cap.obs.size()) {
				if (i < got.size() && j < cap.obs.size() && got[i] == cap.obs[j]) {
					++i; ++j;
					continue;
				}
				if (pre < 0) {
					pre = int(j);
					std::ostringstream o;
					if (i < got.size() &&
					    (j == cap.obs.size() || got[i] < cap.obs[j])) {
						o << "extra row " << mod_line(got[i]);
					} else {
						// The gap in front of a slot is what
						// decides whether a request can reach it.
						int r = mod_line(cap.obs[j]);
						int prev = -1;
						for (int s : cpu_slots_of(CMD_TABLE[mode_index(cap.mode)]))
							if (s < r) prev = s;
						o << "miss row " << r << ", gap before it "
						  << (prev < 0 ? -1 : r - prev);
					}
					first = o.str();
				}
				if (i < got.size() && (j == cap.obs.size() || got[i] < cap.obs[j])) {
					++i; ++extra;
				} else {
					++j; ++miss;
				}
			}
			if (extra + miss < b_extra + b_miss) {
				b_extra = extra; b_miss = miss; b_phi = phi;
				b_pre = pre; b_first = first; b_got = int(got.size());
				b_drop = nd; b_ph = ph;
			}
		}
		std::cout << std::left << std::setw(31) << txt.stem().string() << std::right
			  << std::setw(4) << cap.obs.size()
			  << std::setw(5) << int(cap.obs.size()) - b_got << std::setw(6)
			  << b_extra << std::setw(5) << b_miss << std::setw(6) << b_drop
			  << std::setw(7) << std::fixed << std::setprecision(2) << b_phi
			  << std::setw(4) << (b_ph ? 'y' : '.') << std::setw(5) << b_pre
			  << "   " << b_first << "\n";
		if (!FAIL_DUMP) continue;

		// Replay the best configuration and attribute every miss to the
		// request the arbiter threw away and to the grant that shadowed it.
		std::vector<int> posts;
		if (b_ph) posts.push_back(arm);
		for (double t : cap.t2[1]) posts.push_back(int(std::floor(t + b_phi)));
		std::sort(posts.begin(), posts.end());
		std::vector<DropEv> log;
		DROP_LOG = &log;
		int nd = 0;
		std::map<int, int> slot_of;
		queue_sim(posts, w, nd, &slot_of);
		DROP_LOG = nullptr;
		std::set<int> got;
		for (auto [T, S] : slot_of)
			if (S >= cap.tmin && S <= cap.tmax) got.insert(S);
		for (int S : cap.obs) {
			if (got.count(S)) continue;
			const DropEv* hit = nullptr;
			for (const auto& d : log)
				if (first_slot(w, d.T) == S) hit = &d;
			std::cout << "      miss " << std::setw(6) << S << " row "
				  << std::setw(4) << mod_line(S) << ": ";
			if (!hit) {
				std::cout << "no discarded request reaches this slot\n";
				continue;
			}
			std::cout << "request " << hit->T << " discarded, previous grant "
				  << hit->sprev << ", margin " << (hit->T - hit->sprev)
				  << " (needs " << ACC_THRESH << ")\n";
		}
		// An extra is the mirror of a miss: the model took a request the VDP
		// discarded, so its margin is the quantity to compare against the
		// margins of the misses. slot_of is in request order, so the
		// previous entry carries the slot the decision was measured from.
		std::set<int> want(cap.obs.begin(), cap.obs.end());
		int sprev = -1;
		for (auto [T, S] : slot_of) {
			if (S >= cap.tmin && S <= cap.tmax && !want.count(S))
				std::cout << "     extra " << std::setw(6) << S << " row "
					  << std::setw(4) << mod_line(S) << ": request " << T
					  << " taken, previous grant " << sprev << ", margin "
					  << (sprev < 0 ? 0 : T - sprev) << " (threshold "
					  << ACC_THRESH << ")\n";
			sprev = S;
		}
		if (ONLY.empty()) continue;

		// One named capture: the whole train, so that a bad edge shows up
		// as a gap that departs from the loop pace.
		double pace = median_pace(cap.t2[1]);
		std::cout << "\n  edge      t2       gap      T   slot   row  note\n";
		for (size_t i = 0; i < cap.t2[1].size(); ++i) {
			double t2 = cap.t2[1][i];
			int T = int(std::floor(t2 + b_phi));
			auto it = slot_of.find(T);
			int S = it == slot_of.end() ? -1 : it->second;
			std::cout << std::setw(6) << i << std::setw(10)
				  << std::setprecision(2) << t2 << std::setw(10)
				  << (i ? t2 - cap.t2[1][i - 1] - pace : 0.0) << std::setw(7)
				  << T << std::setw(7) << S << std::setw(6)
				  << (S < 0 ? -1 : mod_line(S));
			if (S < 0) std::cout << "  discarded";
			else if (S < cap.tmin || S > cap.tmax) std::cout << "  outside";
			else if (!want.count(S)) std::cout << "  EXTRA";
			std::cout << "\n";
		}
	}
	return 0;
}

// A plausibility check of a .cpureq against its .vcd. Deliberately not a
// reconstruction: it never chooses a slot, never runs the arbiter and never
// looks at the .txt. It only asks whether the recorded cycles could have come
// from the /CSx pulses in that capture.
//
// The test needs no knowledge of the pin delay. Since every request cycle is
// T_i = floor(t2_i + phi) for one phi shared by the whole capture, the
// differences T_i - t2_i must all fall inside a band of width 1, widened by
// whatever per-edge tolerance one allows. So: match requests to edges in order,
// then look at the spread of the differences. A file built from a different
// capture, or with an edge inserted or dropped, blows the spread apart.
static int run_checkreq(const fs::path& slots_dir, const fs::path& vcd_dir,
			double tol)
{
	struct Bad { std::string name, why; };
	std::vector<Bad> bad;
	int nf = 0, nok = 0, npre = 0, ntag = 0;
	double worst = 0;
	std::string worst_name;

	std::vector<fs::path> files;
	for (const auto& ent : fs::directory_iterator(slots_dir))
		if (ent.path().extension() == ".cpureq") files.push_back(ent.path());
	std::sort(files.begin(), files.end());

	for (const auto& p : files) {
		auto txt = p;
		txt.replace_extension(".txt");
		if (!fs::exists(txt)) continue;
		++nf;
		ReqFile rf;
		try {
			rf = read_reqfile(p);
		} catch (const std::exception& e) {
			bad.push_back({p.stem().string(), e.what()});
			continue;
		}
		Capture cap;
		try {
			cap = load_capture(txt, vcd_dir / (p.stem().string() + ".vcd"));
		} catch (const std::exception& e) {
			bad.push_back({p.stem().string(), std::string("no usable .vcd: ") + e.what()});
			continue;
		}
		const auto& e2 = cap.t2[1];
		int ne = int(e2.size()), nr = int(rf.req.size());
		if (nr < ne) {
			bad.push_back({rf.capture, "fewer requests (" + std::to_string(nr) +
						   ") than /CSx edges (" + std::to_string(ne) + ")"});
			continue;
		}
		int k = nr - ne;
		double lo = 1e18, hi = -1e18;
		for (int i = 0; i < ne; ++i) {
			double d = rf.req[k + i] - e2[i];
			lo = std::min(lo, d);
			hi = std::max(hi, d);
			if (rf.kind == "rw" && rf.tag[k + i] != edge_kind(cap, e2[i])) ++ntag;
		}
		double spread = ne ? hi - lo : 0.0;
		if (spread > worst) { worst = spread; worst_name = rf.capture; }
		if (spread > 1.0 + 2 * tol) {
			std::ostringstream o;
			o << "pin delay spread " << std::fixed << std::setprecision(2)
			  << spread << " cycles (" << lo << " .. " << hi << ")";
			bad.push_back({rf.capture, o.str()});
			continue;
		}
		// A surplus request is only legitimate if the pulse that caused it
		// fell outside the capture. Its pulse would have been at req - d for
		// some d in the band just measured, so the most favourable case has
		// to still land before the first recorded edge.
		bool ok = true;
		for (int j = 0; j < k; ++j) {
			if (rf.req[j] - lo < e2.front()) continue;
			std::ostringstream o;
			o << "request " << rf.req[j] << " has no /CSx pulse: it implies one at "
			  << std::fixed << std::setprecision(2) << (rf.req[j] - hi) << " .. "
			  << (rf.req[j] - lo) << ", inside the capture, which starts at "
			  << e2.front();
			bad.push_back({rf.capture, o.str()});
			ok = false;
			break;
		}
		if (!ok) continue;
		npre += k;
		++nok;
	}

	std::cout << "--checkreq: are the .cpureq cycles consistent with the /CSx pulses?\n"
		  << "The pin delay is not needed: T_i - t2_i must sit in a band of\n"
		  << "width 1 + 2*tol, tol = " << std::fixed << std::setprecision(2) << tol
		  << " cycle.\n"
		  << "files " << nok << '/' << nf << " plausible, "
		  << npre << " requests predate their capture, widest band "
		  << std::setprecision(2) << worst << " (" << worst_name << ")\n";
	if (ntag) std::cout << "  read/write tags disagreeing with the pulse: " << ntag << "\n";
	for (auto& b : bad) std::cout << "  IMPLAUSIBLE " << b.name << ": " << b.why << "\n";
	return bad.empty() ? 0 : 1;
}

// The other direction, and the actual test of the format: read a .cpureq, run
// the arbiter, and compare with the CPU accesses of the .txt. No .vcd.
static int run_fromreq(const fs::path& slots_dir)
{
	int nf = 0, nok = 0, nacc = 0, nbad = 0, nfile_bad = 0;
	// The margins of the drops the model gets right, to see whether the
	// threshold separates cleanly or sits in the middle of the evidence.
	std::vector<DropEv> drops[3];
	std::vector<std::string> bad;
	for (const auto& ent : fs::directory_iterator(slots_dir)) {
		if (ent.path().extension() != ".cpureq") continue;
		auto txt = ent.path();
		txt.replace_extension(".txt");
		if (!fs::exists(txt)) continue;
		Capture cap = load_txt_only(txt);
		if (cap.obs.empty()) continue;
		// load_txt_only leaves the window unset; the .txt defines it.
		cap.tmin = cap.obs.front();
		cap.tmax = cap.obs.back();
		++nf;

		// Everything the arbiter needs comes out of this file: the cycles,
		// and the parameters that give those integers their meaning.
		ReqFile rf = read_reqfile(ent.path());
		NEED_ROW = rf.need_row;
		QDEPTH = rf.buffer;
		ACC_THRESH = rf.margin;
		ACC_MODE_INDEX = mode_index(rf.mode);
		DROP_LOG = &drops[ACC_MODE_INDEX];
		int mi = ACC_MODE_INDEX;
		auto wait = make_wait(cpu_slots_of(CMD_TABLE[mi]), CMD_TABLE[mi], rf.need);

		int n_drop = 0;
		std::map<int, int> slot_of;
		queue_sim(rf.req, wait, n_drop, &slot_of);

		std::vector<int> got;
		for (auto [T, S] : slot_of)
			if (S >= cap.tmin && S <= cap.tmax) got.push_back(S);
		std::sort(got.begin(), got.end());
		got.erase(std::unique(got.begin(), got.end()), got.end());

		nacc += int(cap.obs.size());
		if (got == cap.obs) {
			++nok;
		} else {
			++nfile_bad;
			size_t i = 0, j = 0, d = 0;
			while (i < got.size() || j < cap.obs.size()) {
				if (i < got.size() && j < cap.obs.size() && got[i] == cap.obs[j]) {
					++i; ++j;
				} else if (i < got.size() &&
					   (j == cap.obs.size() || got[i] < cap.obs[j])) {
					++i; ++d;
				} else {
					++j; ++d;
				}
			}
			nbad += int(d);
			if (bad.size() < 10) bad.push_back(cap.name);
		}
	}
	DROP_LOG = nullptr;
	std::cout << "--fromreq: replay the .cpureq files through the arbiter, no .vcd\n"
		  << "files " << nok << '/' << nf << " reproduced their .txt exactly, "
		  << "accesses " << (nacc - nbad) << '/' << nacc << "\n";

	std::cout << "\nmargin T - S_prev of the correctly discarded requests, so every\n"
		     "value listed must lie below that mode's threshold\n";
	for (int m = 0; m < 3; ++m) {
		std::map<int, int> h;
		for (const auto& d : drops[m]) h[std::min(d.T - d.sprev, 9)]++;
		std::cout << "  " << std::left << std::setw(9) << mode_name(Mode(m))
			  << std::right << " thresh " << std::setw(3) << THRESH_MODE[m]
			  << " ";
		for (auto [k, v] : h)
			std::cout << "  " << (k == 9 ? ">=9" : std::to_string(k)) << ":" << v;
		std::cout << "   total " << drops[m].size() << "\n";
	}
	for (auto& b : bad) std::cout << "  mismatch " << b << "\n";
	return nfile_bad ? 1 : 0;
}

static int run_trellis(const fs::path& slots_dir, const fs::path& vcd_dir,
		       std::string_view sel, int force_q, bool refit, bool dump)
{
	std::vector<int> cpu_wait[3];
	rw_wait(cpu_wait);
	TrelCache cache{slots_dir / "trellis-2026.txt", {}, false};
	if (!refit) cache.load();

	std::vector<int> eps_grid(std::begin(EPS_Q), std::end(EPS_Q));
	if (force_q >= 0) eps_grid = {force_q};

	fs::path req_path = slots_dir / "trellis-requests-2026.txt";
	std::ofstream req_os(req_path);
	req_os << "# Reconstructed CPU request cycles (fit_2026.cc --trellis).\n"
		  "# Per capture: phi is the pin delay used, eps the per-edge tolerance.\n"
		  "# edge  t2 (/CSx rise, VDP cycles)  T (cycle the arbiter saw it)"
		  "  slot (RAS granted)  dev\n"
		  "# dev = T - floor(t2 + phi), non-zero only for an edge that the\n"
		  "# tolerance let tip to the neighbouring cycle. Only captures whose\n"
		  "# reconstruction reproduced the .txt exactly are listed.\n";

	std::cout << "trellis: exact reconstruction of the CPU request cycles.\n"
		  << "T_i = floor(t2_i + e_i + phi), one real phi per capture, |e_i| <= eps,\n"
		  << "optionally one request already pending when the capture started.\n"
		  << (force_q >= 0 ? "eps forced to " : "eps is the smallest tolerance that "
						       "reproduces every grant, up to ")
		  << std::fixed << std::setprecision(4)
		  << eps_grid.back() * EPS_UNIT << " cycle = "
		  << std::setprecision(2) << eps_grid.back() * EPS_UNIT / VDP_PER_SAMPLE
		  << " analyzer samples.\n"
		  << "NEED=" << NEED << ", cache " << cache.path.filename().string() << "\n\n";
	std::cout << std::left << std::setw(38) << "file" << std::right
		  << std::setw(5) << "nCS" << std::setw(5) << "nAcc" << std::setw(8) << "pace"
		  << std::setw(4) << "ph"
		  << std::setw(8) << "eps" << std::setw(7) << "smp" << std::setw(5) << "dev"
		  << std::setw(5) << "drp" << std::setw(5) << "out" << std::setw(5) << "chk"
		  << "  phi set\n";

	std::vector<TrelFile> res;
	std::vector<Capture> caps;
	for (const auto& txt : rw_files(slots_dir, sel)) {
		Capture cap;
		try {
			cap = load_capture(txt, vcd_dir / (txt.stem().string() + ".vcd"));
		} catch (const std::exception&) { continue; }
		if (cap.obs.empty()) continue;

		TrelFile r;
		r.name = cap.name;
		r.mode = cap.mode;
		r.ncs = int(cap.t2[1].size());
		r.nacc = int(cap.obs.size());
		r.pace = median_pace(cap.t2[1]);
		const auto& w = cpu_wait[mode_index(cap.mode)];
		ACC_MODE_INDEX = mode_index(cap.mode);
		ACC_THRESH = THRESH_MODE[ACC_MODE_INDEX];
		// A pre-capture request is physically possible but it is also
		// extra freedom, so only reach for it when nothing else works.
		for (int q : eps_grid) {
			bool done = false;
			for (int ph = 0; ph <= 1 && !done; ++ph) {
				IvSet set;
				int nd = 0;
				if (!trel_query(cache, cap, w, q, ph != 0, set, nd)) continue;
				r.eps_q = q;
				r.phantom = ph != 0;
				r.set = set;
				r.ndev = nd;
				done = true;
			}
			if (done) break;
		}
		if (r.eps_q >= 0 && !r.set.empty()) {
			double phi = 0.5 * (r.set.front().first + r.set.front().second);
			auto sol = trellis_fit(cap.t2[1], cap.obs, w, cap.tmin, cap.tmax,
					       phi, r.eps_q * EPS_UNIT, r.phantom);
			if (sol.ok) r.chk = trellis_verify(cap, w, sol);
			if (sol.ok && r.chk.ok) {
				trellis_write_requests(req_os, cap, w, sol, phi,
						       r.eps_q * EPS_UNIT, trel_key());
				if (WRITE_REQ) write_reqfile(slots_dir, cap, w, sol);
			}
			if (dump) trellis_dump(cap, w, phi, r.eps_q * EPS_UNIT, r.phantom);
		}

		std::cout << std::left << std::setw(38) << r.name << std::right
			  << std::setw(5) << r.ncs << std::setw(5) << r.nacc
			  << std::setw(8) << std::fixed << std::setprecision(1) << r.pace
			  << std::setw(4) << (r.eps_q < 0 ? '?' : (r.phantom ? 'y' : '.'));
		if (r.eps_q < 0) {
			std::cout << "       -      -    -    -    -    -"
				  << "  NO EXACT RECONSTRUCTION\n";
		} else {
			std::cout << std::setw(8) << std::fixed << std::setprecision(4)
				  << r.eps_q * EPS_UNIT << std::setw(7)
				  << std::setprecision(2)
				  << r.eps_q * EPS_UNIT / VDP_PER_SAMPLE
				  << std::setw(5) << r.ndev << std::setw(5) << r.chk.n_drop
				  << std::setw(5) << r.chk.n_out << std::setw(5)
				  << (r.chk.ok ? "ok" : "BAD") << "  " << fmt_set(r.set) << "\n";
		}
		res.push_back(r);
		caps.push_back(std::move(cap));
	}

	int nfail = 0, nclean = 0, nph = 0, tot_acc = 0, worst = 0, nbad = 0;
	int tot_drop = 0, tot_out = 0;
	for (auto& r : res) {
		tot_acc += r.nacc;
		if (r.eps_q < 0) { ++nfail; continue; }
		if (!r.chk.ok) ++nbad;
		if (r.eps_q == 0 && !r.phantom) ++nclean;
		if (r.phantom) ++nph;
		worst = std::max(worst, r.eps_q);
		tot_drop += r.chk.n_drop;
		tot_out += r.chk.n_out;
	}
	std::cout << "\nfiles " << res.size() << "  exact " << (int(res.size()) - nfail)
		  << "  of which eps 0 and no pending request " << nclean
		  << "  needed a pending request " << nph
		  << "  no reconstruction " << nfail << "\n"
		  << "accesses " << tot_acc << "  requests dropped by the arbiter "
		  << tot_drop << "  grants outside the .txt window " << tot_out
		  << "  failed re-simulation " << nbad << "\n"
		  << "largest per-file eps " << std::fixed << std::setprecision(4)
		  << worst * EPS_UNIT << " cycle = " << std::setprecision(2)
		  << worst * EPS_UNIT / VDP_PER_SAMPLE << " samples\n\n";

	// The stronger claim: one phi shared by every capture, not one per
	// capture. Captures that cannot be reconciled at all are named instead
	// of silently emptying the intersection, since one bad access out of a
	// thousand says something different from a wrong model.
	std::cout << "smallest tolerance at which a single phi covers a whole mode\n"
		  << "(NEED " << NEED << " with per-mode offsets " << NEED_OFF[0] << ","
		  << NEED_OFF[1] << "," << NEED_OFF[2];
	if (!NEED_ROW.empty()) {
		std::cout << "; rows";
		for (auto [r, n] : NEED_ROW) std::cout << ' ' << r;
		std::cout << " at " << NEED_ROW.begin()->second;
	}
	std::cout << "):\n";
	auto report = [&](const char* label, int only_mode) {
		std::vector<size_t> idx;
		for (size_t i = 0; i < res.size(); ++i)
			if (only_mode < 0 || mode_index(res[i].mode) == only_mode)
				idx.push_back(i);
		if (idx.empty()) return;
		for (size_t gi = 0; gi < eps_grid.size(); ++gi) {
			int q = eps_grid[gi];
			std::vector<IvSet> sets;
			std::vector<size_t> have;
			for (size_t i : idx) {
				const auto& w = cpu_wait[mode_index(res[i].mode)];
				IvSet set;
				int nd = 0;
				bool f = trel_query(cache, caps[i], w, q, false, set, nd);
				if (!f) f = trel_query(cache, caps[i], w, q, true, set, nd);
				if (!f) continue;
				sets.push_back(set);
				have.push_back(i);
			}
			Cover c = sets.empty() ? Cover{} : best_cover(sets);
			bool all = c.n == int(idx.size());
			if (!all && gi + 1 < eps_grid.size()) continue;
			std::cout << "  " << std::left << std::setw(9) << label << std::right
				  << " eps " << std::fixed << std::setprecision(4)
				  << q * EPS_UNIT << " (" << std::setprecision(2)
				  << q * EPS_UNIT / VDP_PER_SAMPLE << " samples)  phi in ["
				  << std::setprecision(3) << c.lo << ',' << c.hi << ")  fits "
				  << c.n << " of " << idx.size() << " captures";
			if (!all) {
				std::cout << ", not:";
				double mid = 0.5 * (c.lo + c.hi);
				for (size_t k = 0; k < sets.size(); ++k) {
					bool in = false;
					for (auto& iv : sets[k])
						if (iv.first <= mid && mid < iv.second) in = true;
					if (!in) std::cout << ' ' << res[have[k]].name;
				}
				for (size_t i : idx)
					if (std::find(have.begin(), have.end(), i) == have.end())
						std::cout << ' ' << res[i].name << "(none)";
			}
			std::cout << "\n";
			break;
		}
	};
	report("dispOff", 0);
	report("sprOff", 1);
	report("sprOn", 2);
	report("ALL", -1);
	cache.save();
	return 0;
}

int main(int argc, char** argv)
{
	bool do_wiggle = false;
	bool do_diag = false;
	bool do_cmd = false;
	bool do_nocpu = false;
	bool do_hyp = false;
	bool do_idle = false;
	bool do_scratch = false;
	bool do_mismatch = false;
	bool do_origin = false;
	bool do_rw = false;
	bool do_trellis = false;
	bool do_fromreq = false;
	bool do_checkreq = false;
	bool do_faildiag = false;
	bool do_pacescan = false;
	bool do_widthcheck = false;
	bool do_margins = false;
	bool do_rawdump = false;
	double check_tol = 0.5;
	bool trel_refit = false;
	bool trel_dump = false;
	int trel_jit = -1; // --eps= / --smp=, else search the grid
	int rw_force = -1;
	std::string rw_sel = "rdwr";
	std::string cmd_filter = "all";
	std::string diag_filter;
	std::string hyp_filter = "scr5-sprOff-lmmm-rdCpu-1";
	fs::path slots_dir = fs::current_path();
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		if (a == "--wiggle") do_wiggle = true;
		else if (a == "--hmmv") {
			do_cmd = true;
			cmd_filter = "hmmv";
		} else if (a == "--cmd") {
			do_cmd = true;
			if (i + 1 < argc && argv[i + 1][0] != '-' &&
			    !fs::is_directory(argv[i + 1])) {
				cmd_filter = argv[++i];
			} else {
				cmd_filter = "all";
			}
		} else if (a == "--nocpu") {
			do_nocpu = true;
		} else if (a == "--diag") {
			do_diag = true;
			if (i + 1 < argc && argv[i + 1][0] != '-' &&
			    !fs::is_directory(argv[i + 1])) {
				diag_filter = argv[++i];
			}
		} else if (a == "--hyp-search") {
			do_hyp = true;
			if (i + 1 < argc && argv[i + 1][0] != '-' &&
			    !fs::is_directory(argv[i + 1])) {
				hyp_filter = argv[++i];
			}
		} else if (a == "--idle-rdd") {
			do_idle = true;
		} else if (a == "--scratch") {
			do_scratch = true;
		} else if (a == "--mismatch") {
			do_mismatch = true;
		} else if (a == "--origin") {
			do_origin = true;
		} else if (a == "--rw") {
			do_rw = true;
		} else if (a == "--trellis") {
			do_trellis = true;
		} else if (a == "--refit") {
			trel_refit = true;
		} else if (a == "--tdump") {
			trel_dump = true;
		} else if (a == "--csdump") {
			CS_DUMP = true;
		} else if (a == "--noanchorfix") {
			ANCHOR_FIX = false;
		} else if (a == "--pairtime") {
			LSQ_TIME = false;
		} else if (a == "--nopulsefix") {
			PULSE_FIX = false;
		} else if (a.rfind("--prepace=", 0) == 0) {
			PRE_PACE = std::stoi(a.substr(10));
		} else if (a == "--reqfiles") {
			WRITE_REQ = true;
		} else if (a == "--fromreq") {
			do_fromreq = true;
		} else if (a == "--checkreq") {
			do_checkreq = true;
		} else if (a == "--faildiag") {
			do_faildiag = true;
		} else if (a.rfind("--thresh=", 0) == 0) {
			std::istringstream is(a.substr(9));
			std::string tok;
			int m = 0;
			for (; m < 3 && std::getline(is, tok, ','); ++m)
				THRESH_MODE[m] = std::stoi(tok);
			// One value sets all three.
			for (int i = m; i < 3; ++i) THRESH_MODE[i] = THRESH_MODE[m - 1];
		} else if (a == "--rawthreshdist") {
			// Historical diagnostic: compare raw wall-clock coordinates. This
			// needs an ad-hoc -2 at padded row 1330 to mimic the phiL distance.
			THRESH_ENGINE_DIST = false;
		} else if (a == "--rawdump") {
			do_rawdump = true;
		} else if (a == "--margins") {
			do_margins = true;
		} else if (a == "--widthcheck") {
			do_widthcheck = true;
		} else if (a == "--nowidthfix") {
			WIDTH_FIX = false;
		} else if (a == "--pacescan") {
			do_pacescan = true;
		} else if (a.rfind("--only=", 0) == 0) {
			ONLY = a.substr(7);
		} else if (a == "--faildump") {
			do_faildiag = true;
			FAIL_DUMP = true;
		} else if (a.rfind("--tol=", 0) == 0) {
			check_tol = std::stod(a.substr(6));
		} else if (a.rfind("--eps=", 0) == 0) {
			trel_jit = int(std::lround(std::stod(a.substr(6)) * 256.0));
		} else if (a.rfind("--smp=", 0) == 0) {
			trel_jit = int(std::lround(std::stod(a.substr(6)) *
						   VDP_PER_SAMPLE * 256.0));
		} else if (a.rfind("--sel=", 0) == 0) {
			rw_sel = a.substr(6);
		} else if (a.rfind("--force=", 0) == 0) {
			rw_force = std::stoi(a.substr(8));
		} else if (a.rfind("--pad3", 0) == 0) {
			CMD_TABLE[2].pad[0].extra = 1;
			SPR_ADDEND = a.size() > 6 ? std::stoi(a.substr(7)) : 2;
		} else if (a.rfind("--padsil", 0) == 0) {
			// The padding is the phiL divider stall at the line end:
			// gc024 holds the RCC ring for one phiA cycle in each of
			// hcntr 326..329, so four single cycles, not two pairs
			// (IKA9958.md §4d). Positions are derived, not fitted.
			// --padsil[=shift[,pair]] shifts the derived positions,
			// or with pair=1 groups them as two 2-cycle pads on the
			// first and third stall (which is what stretch_cas uses).
			PAD_SHIFT = 0;
			int pair = 0;
			if (a.size() > 8 && a[8] == '=') {
				auto spec = a.substr(9);
				auto comma = spec.find(',');
				PAD_SHIFT = std::stoi(spec.substr(0, comma));
				if (comma != std::string::npos)
					pair = std::stoi(spec.substr(comma + 1));
			}
			PAD_SILICON = true;
			PAD_PAIR = pair;
			static const int c0[4] = {1326, 1331, 1336, 1341};
			static const int c1[4] = {1324, 1329, 1334, 1339};
			for (int m = 0; m < 3; ++m) {
				const int* c = (m == 0) ? c0 : c1;
				if (pair) {
					CMD_TABLE[m].pad[0] = {mod_line(c[0] + PAD_SHIFT), 2};
					CMD_TABLE[m].pad[1] = {mod_line(c[2] + PAD_SHIFT), 2};
					CMD_TABLE[m].npad = 2;
				} else {
					for (int i = 0; i < 4; ++i)
						CMD_TABLE[m].pad[i] =
							{mod_line(c[i] + PAD_SHIFT), 1};
					CMD_TABLE[m].npad = 4;
				}
			}
		} else if (a.rfind("--sprextra=", 0) == 0) {
			SPR_ADDEND = std::stoi(a.substr(11));
		} else if (a.rfind("--need=", 0) == 0) {
			NEED = std::stoi(a.substr(7));
		} else if (a.rfind("--packedneed=", 0) == 0) {
			PACKED_NEED = std::stoi(a.substr(13));
		} else if (a.rfind("--qdepth=", 0) == 0) {
			QDEPTH = std::stoi(a.substr(9));
		} else if (a.rfind("--needrow=", 0) == 0) {
			auto spec = a.substr(10);
			auto col = spec.rfind(':');
			int nd = std::stoi(spec.substr(col + 1));
			std::istringstream is(spec.substr(0, col));
			std::string tok;
			while (std::getline(is, tok, ',')) NEED_ROW[std::stoi(tok)] = nd;
		} else if (a.starts_with("--threshrow=")) {
			auto v = a.substr(12);
			auto c = v.find(':');
			THRESH_ROW[std::stoi(v.substr(0, c))] = std::stoi(v.substr(c + 1));
		} else if (a.rfind("--needoff=", 0) == 0) {
			std::istringstream is(a.substr(10));
			std::string tok;
			for (int m = 0; m < 3 && std::getline(is, tok, ','); ++m)
				NEED_OFF[m] = std::stoi(tok);
		} else slots_dir = a;
	}
	fs::path vcd_dir = slots_dir / ".." / "1.vcd";
	if (!fs::exists(vcd_dir)) {
		vcd_dir = slots_dir.parent_path() / "1.vcd";
	}
	if (do_fromreq) return run_fromreq(slots_dir);
	if (do_checkreq) return run_checkreq(slots_dir, vcd_dir, check_tol);
	if (do_faildiag) return run_faildiag(slots_dir, vcd_dir);
	if (do_pacescan) return run_pacescan(slots_dir, vcd_dir);
	if (do_widthcheck) return run_widthcheck(slots_dir, vcd_dir);
	if (do_margins) return run_margins(slots_dir);
	if (do_rawdump) return run_rawdump(slots_dir, vcd_dir);
	if (do_trellis)
		return run_trellis(slots_dir, vcd_dir, rw_sel, trel_jit, trel_refit, trel_dump);
	if (do_rw) return rw_force >= 0
		? run_rwdiag(slots_dir, vcd_dir, rw_sel, rw_force)
		: run_rw(slots_dir, vcd_dir, rw_sel);
	if (do_origin) return run_origin_check(slots_dir, vcd_dir);
	if (do_hyp) return run_hyp_search(slots_dir, vcd_dir, hyp_filter);
	if (do_idle) return run_idle_rdd(slots_dir, vcd_dir);
	if (do_scratch) return run_scratch_2026(slots_dir);
	if (do_mismatch) {
		int r = run_mismatch(slots_dir);
		if (r) return r;
		return run_iter8(slots_dir);
	}

	std::vector<int> cpu_wait[3];
	for (int m = 0; m < 3; ++m) {
		auto cs = cpu_slots_of(CMD_TABLE[m]);
		cpu_wait[m] = make_wait(cs, CMD_TABLE[m], NEED);
	}

	if (do_nocpu) {
		std::vector<fs::path> files;
		for (const auto& ent : fs::directory_iterator(slots_dir)) {
			if (!ent.is_regular_file()) continue;
			auto name = ent.path().filename().string();
			if (is_nocpu_file(name, cmd_filter)) files.push_back(ent.path());
		}
		std::sort(files.begin(), files.end());
		if (files.empty()) {
			std::cerr << "no scr5-*-{cmd}-noCpu-*.txt in " << slots_dir << "\n";
			return 1;
		}

		int n_run = 0, n_ok = 0, tot_h = 0, tot_n = 0, tot_e = 0, tot_m = 0, tot_unk = 0;
		int n_ok_ch = 0, tot_h_ch = 0, tot_e_ch = 0, tot_m_ch = 0;
		int cmd_n[6] = {}, cmd_ok[6] = {}, cmd_h[6] = {}, cmd_e[6] = {};
		int mode_n[3] = {}, mode_ok[3] = {}, mode_h[3] = {}, mode_e[3] = {};
		std::cout << "noCpu command engine (CPU occupancy empty; next slot from observed last)\n";
		std::cout << "non-perfect files only:\n";

		for (const auto& txt : files) {
			Capture cap;
			try {
				cap = load_engine_txt(txt);
			} catch (const std::exception& ex) {
				std::cout << txt.filename().string() << "  SKIP " << ex.what() << "\n";
				continue;
			}
			if (cap.eng.empty()) {
				std::cout << cap.name << "  SKIP no engine tags\n";
				continue;
			}
			Cmd cmd = cmd_of(cap.name);
			Variant var = parse_variant(cap.name);
			std::vector<int> obs_e;
			obs_e.reserve(cap.eng.size());
			for (auto& e : cap.eng) obs_e.push_back(e.ras);
			auto p = predict_engine(cap.eng, cap.mode, cmd, {}, var, true);
			auto pch = predict_engine(cap.eng, cap.mode, cmd, {}, var, false);
			auto s = score_times(p.pred, obs_e);
			auto sch = score_times(pch.pred, obs_e);
			int ne = int(obs_e.size());
			bool ok = s.extra == 0 && s.miss == 0;
			bool okch = sch.extra == 0 && sch.miss == 0;
			++n_run;
			n_ok += ok;
			n_ok_ch += okch;
			tot_h += s.hit;
			tot_e += s.extra;
			tot_m += s.miss;
			tot_h_ch += sch.hit;
			tot_e_ch += sch.extra;
			tot_m_ch += sch.miss;
			tot_n += ne;
			tot_unk += p.unknown;
			int ci = int(cmd);
			if (ci >= 0 && ci < 6) {
				++cmd_n[ci];
				if (ok) ++cmd_ok[ci];
				cmd_h[ci] += s.hit;
				cmd_e[ci] += ne;
			}
			int mi = mode_index(cap.mode);
			++mode_n[mi];
			if (ok) ++mode_ok[mi];
			mode_h[mi] += s.hit;
			mode_e[mi] += ne;
			if (!ok || p.unknown) {
				std::cout << std::left << std::setw(42) << cap.name << " "
					  << std::right << s.hit << '/' << ne
					  << " extra " << s.extra << " miss " << s.miss
					  << (p.unknown ? " unk " + std::to_string(p.unknown) : "")
					  << "\n";
				if (do_diag) {
					int k = 1;
					for (; k < int(std::min(p.pred.size(), obs_e.size())); ++k) {
						if (p.pred[k] != obs_e[k]) break;
					}
					if (k >= 1 && k < int(cap.eng.size())) {
						int last = obs_e[k - 1];
						auto sw = step_wait(cmd, cap.mode, cap.eng, k, last, var);
						int delta = pick_delta(sw, last, CMD_TABLE[mi], {}, obs_e[k]);
						int cand = (delta >= 0)
							? next_cmd_slot(last, delta, CMD_TABLE[mi], {})
							: -1;
						std::cout << "    step " << (k - 1) << "->" << k
							  << " kinds " << cap.eng[k - 1].kind
							  << "->" << cap.eng[k].kind
							  << " last=" << last << " row " << (last % LINE)
							  << " Δ=" << delta
							  << " alt_nl=" << sw.alt_nl
							  << " cand=" << cand
							  << (cand >= 0 ? " row " + std::to_string(cand % LINE) : "")
							  << " obs=" << obs_e[k]
							  << " pred=" << p.pred[k]
							  << " addr " << std::hex << cap.eng[k - 1].addr
							  << "->" << cap.eng[k].addr << std::dec
							  << "\n";
					}
				}
			}
		}

		std::cout << "\nnoCpu      " << n_ok << '/' << n_run << " perfect  "
			  << tot_h << '/' << tot_n;
		if (tot_n) std::cout << " (" << std::fixed << std::setprecision(1)
				     << (100.0 * tot_h / tot_n) << "%)";
		std::cout << " extra " << tot_e << " miss " << tot_m
			  << " unk " << tot_unk << "\n";
		std::cout << "chained    " << n_ok_ch << '/' << n_run << " perfect  "
			  << tot_h_ch << '/' << tot_n;
		if (tot_n) std::cout << " (" << std::fixed << std::setprecision(1)
				     << (100.0 * tot_h_ch / tot_n) << "%)";
		std::cout << " extra " << tot_e_ch << " miss " << tot_m_ch << "\n";
		std::cout << "per command:\n";
		for (int ci = 0; ci < 6; ++ci) {
			if (!cmd_n[ci]) continue;
			std::cout << "  " << std::left << std::setw(6) << cmd_name(Cmd(ci))
				  << std::right << cmd_ok[ci] << '/' << cmd_n[ci] << " files  "
				  << cmd_h[ci] << '/' << cmd_e[ci];
			if (cmd_e[ci]) std::cout << " (" << std::fixed << std::setprecision(1)
						 << (100.0 * cmd_h[ci] / cmd_e[ci]) << "%)";
			std::cout << "\n";
		}
		std::cout << "per mode:\n";
		for (int mi = 0; mi < 3; ++mi) {
			if (!mode_n[mi]) continue;
			std::cout << "  " << std::left << std::setw(8) << mode_name(Mode(mi))
				  << std::right << mode_ok[mi] << '/' << mode_n[mi] << " files  "
				  << mode_h[mi] << '/' << mode_e[mi];
			if (mode_e[mi]) std::cout << " (" << std::fixed << std::setprecision(1)
						 << (100.0 * mode_h[mi] / mode_e[mi]) << "%)";
			std::cout << "\n";
		}
		return (n_ok == n_run && n_ok_ch == n_run && tot_unk == 0) ? 0 : 1;
	}

	{
		std::vector<fs::path> files;
		for (const auto& ent : fs::directory_iterator(slots_dir)) {
			if (!ent.is_regular_file()) continue;
			auto name = ent.path().filename().string();
			if (is_cpu_file(name, cmd_filter)) files.push_back(ent.path());
		}
		std::sort(files.begin(), files.end());
		if (files.empty()) {
			std::cerr << "no scr5-*-{rd,wr}Cpu-*.txt in " << slots_dir << "\n";
			return 1;
		}

		constexpr int ND = DELTA_HI - DELTA_LO + 1;
		int glob_hit[2][ND] = {}, glob_extra[2][ND] = {}, glob_miss[2][ND] = {}, glob_n[2][ND] = {};

		struct Loaded {
			Capture cap;
			FileFit fit;
		};
		std::vector<Loaded> loaded;

		std::cout << std::left << std::setw(36) << "file"
			  << "  CPU D16      eng none      eng RAS+P5    eng R..+P5    eng occ-pend\n";
		int n_cpu_ok = 0, n_cpu_run = 0, n_stop_ok = 0, n_stop_run = 0;
		int n_none_ok = 0, n_obs_ok = 0, n_rdd_ok = 0, n_pred_ok = 0, n_eng_run = 0;
		int tot_cpu_h = 0, tot_cpu_n = 0, tot_cpu_e = 0, tot_cpu_m = 0;
		int tot_stop_h = 0, tot_stop_n = 0, tot_stop_e = 0, tot_stop_m = 0;
		int tot_n0 = 0, tot_h0 = 0, tot_e0 = 0, tot_m0 = 0;
		int tot_no = 0, tot_ho = 0, tot_eo = 0, tot_mo = 0;
		int tot_np = 0, tot_hp = 0, tot_ep = 0, tot_mp = 0;
		int tot_nr = 0, tot_hr = 0, tot_er = 0, tot_mr = 0;
		int tot_sk_o = 0, tot_sk_p = 0, tot_sk_r = 0, tot_col = 0;
		int tot_dummy = 0;
		int n_no_cpu = 0, n_no_eng = 0, n_files = 0;
		int cmd_n[6] = {}, cmd_ok[6] = {}, cmd_h[6] = {}, cmd_e[6] = {};
		int cmd_ok_r[6] = {}, cmd_h_r[6] = {};
		int mode_cpu_n[3] = {}, mode_cpu_ok[3] = {}, mode_cpu_h[3] = {}, mode_cpu_e[3] = {};

		auto cell = [](std::ostream& o, int h, int n, bool ok) {
			o << std::right << std::setw(3) << h << '/' << std::left << std::setw(3) << n
			  << (ok ? "OK" : "  ");
		};

		for (const auto& txt : files) {
			auto stem = txt.stem().string();
			fs::path vcd = vcd_dir / (stem + ".vcd");
			Capture cap;
			try {
				cap = load_capture(txt, vcd);
			} catch (const std::exception& ex) {
				std::cout << stem << "  SKIP " << ex.what() << "\n";
				continue;
			}
			++n_files;
			bool is_stop = cap.name.find("-stop-") != std::string::npos;
			bool have_cpu = !cap.obs.empty();
			bool have_eng = !cap.eng.empty();
			if (!have_cpu) ++n_no_cpu;
			if (!have_eng) ++n_no_eng;

			Cmd cmd = cmd_of(cap.name);
			Variant var = parse_variant(cap.name);
			FileFit fit;
			fit.name = cap.name;
			fit.mode = cap.mode;
			fit.n = int(cap.obs.size());
			bool cpu_ok = false;
			if (have_cpu) {
				fit = fit_file(cap, cpu_wait, glob_hit, glob_extra, glob_miss, glob_n);
				cpu_ok = fit.extra == 0 && fit.miss == 0;
				++n_cpu_run;
				n_cpu_ok += cpu_ok;
				tot_cpu_h += fit.hit;
				tot_cpu_n += fit.n;
				tot_cpu_e += fit.extra;
				tot_cpu_m += fit.miss;
				int mi = mode_index(cap.mode);
				++mode_cpu_n[mi];
				if (cpu_ok) ++mode_cpu_ok[mi];
				mode_cpu_h[mi] += fit.hit;
				mode_cpu_e[mi] += fit.n;
				if (is_stop) {
					++n_stop_run;
					n_stop_ok += cpu_ok;
					tot_stop_h += fit.hit;
					tot_stop_n += fit.n;
					tot_stop_e += fit.extra;
					tot_stop_m += fit.miss;
				}
			}

			std::unordered_set<int> occ_obs(cap.obs.begin(), cap.obs.end());
			std::unordered_set<int> occ_pend, occ_rdd = occ_obs;
			for (int d : cap.dummy) occ_rdd.insert(d);
			if (have_eng) tot_dummy += int(cap.dummy.size());
			if (have_cpu) {
				const auto& w = cpu_wait[mode_index(cap.mode)];
				auto posts = make_posts(cap.t2[fit.edge_i], fit.delta, w, cap.tmin, cap.tmax);
				auto sim = vdp_cpu_posts(posts, w, cap.tmin, cap.tmax);
				std::vector<std::pair<int, int>> ts;
				auto ev = align_d16(sim.pred, cap.obs);
				size_t ip = 0;
				for (auto& e : ev) {
					if (e.kind == Align::Hit || e.kind == Align::Tie) {
						ts.push_back({sim.pred[ip].T, e.t});
						++ip;
					} else if (e.kind == Align::Extra) {
						ts.push_back({sim.pred[ip].T, sim.pred[ip].S});
						++ip;
					}
				}
				occ_pend = occupy_for_command(ts, cap.mode);
			}

			int collide = 0;
			for (auto& e : cap.eng) if (occ_obs.count(e.ras)) ++collide;
			tot_col += collide;

			Score s0{}, so{}, sr{}, sp{};
			HmmvPred p0{}, po{}, pr{}, pp{};
			int ne = int(cap.eng.size());
			bool ok0 = false, oko = false, okr = false, okp = false;
			if (have_eng) {
				std::vector<int> obs_e;
				obs_e.reserve(cap.eng.size());
				for (auto& e : cap.eng) obs_e.push_back(e.ras);
				// P5 and the newline-idle rule are retired (FINDINGS6
				// §10.3, §10.4, §10.5): with unconditional packed-start
				// +1 the engine step needs no CPU predicate. The A/P/B/C
				// comparison in --scratch still exercises them.
				const std::vector<int>* cpu_p5 = nullptr;
				(void)have_cpu;
				p0 = predict_engine(cap.eng, cap.mode, cmd, {}, var);
				po = predict_engine(cap.eng, cap.mode, cmd, occ_obs, var, false, cpu_p5);
				pr = predict_engine(cap.eng, cap.mode, cmd, occ_rdd, var, false, cpu_p5);
				pp = predict_engine(cap.eng, cap.mode, cmd, occ_pend, var, false, cpu_p5);
				s0 = score_times(p0.pred, obs_e);
				so = score_times(po.pred, obs_e);
				sr = score_times(pr.pred, obs_e);
				sp = score_times(pp.pred, obs_e);
				ok0 = s0.extra == 0 && s0.miss == 0;
				oko = so.extra == 0 && so.miss == 0;
				okr = sr.extra == 0 && sr.miss == 0;
				okp = sp.extra == 0 && sp.miss == 0;
				++n_eng_run;
				n_none_ok += ok0;
				n_obs_ok += oko;
				n_rdd_ok += okr;
				n_pred_ok += okp;
				int ci = int(cmd);
				if (ci >= 0 && ci < 6) {
					++cmd_n[ci];
					if (oko) ++cmd_ok[ci];
					cmd_h[ci] += so.hit;
					cmd_e[ci] += ne;
					if (okr) ++cmd_ok_r[ci];
					cmd_h_r[ci] += sr.hit;
				}
				tot_h0 += s0.hit; tot_e0 += s0.extra; tot_m0 += s0.miss; tot_n0 += ne;
				tot_ho += so.hit; tot_eo += so.extra; tot_mo += so.miss; tot_no += ne;
				tot_hr += sr.hit; tot_er += sr.extra; tot_mr += sr.miss; tot_nr += ne;
				tot_hp += sp.hit; tot_ep += sp.extra; tot_mp += sp.miss; tot_np += ne;
				tot_sk_o += po.skips;
				tot_sk_r += pr.skips;
				tot_sk_p += pp.skips;
			}

			std::cout << std::left << std::setw(36) << cap.name << " ";
			if (have_cpu) {
				cell(std::cout, fit.hit, fit.n, cpu_ok);
			} else {
				std::cout << "  no-cpu  ";
			}
			std::cout << "  ";
			if (have_eng) {
				cell(std::cout, s0.hit, ne, ok0);
				std::cout << "  ";
				cell(std::cout, so.hit, ne, oko);
				std::cout << "  ";
				cell(std::cout, sr.hit, ne, okr);
				std::cout << "  ";
				cell(std::cout, sp.hit, ne, okp);
				std::cout << "  o" << po.skips << "/r" << pr.skips << "/p" << pp.skips
					  << "  R.." << cap.dummy.size()
					  << (p0.unknown ? "  unk" : "")
					  << (collide ? "  COLLIDE" : "");
			} else {
				std::cout << (is_stop ? "stop" : "no-eng");
			}
			if (have_cpu) {
				std::cout << "  δ=" << fit.delta << ' ' << fit.edge;
			}
			std::cout << '\n';

			if (do_diag && have_eng) {
				if (!diag_filter.empty() && cap.name.find(diag_filter) == std::string::npos) {
				} else if (diag_filter.empty() && cpu_ok && oko) {
				} else {
					dump_hmmv_diag(cap, fit, cpu_wait, occ_obs, occ_pend);
				}
			}
			if (have_cpu) loaded.push_back({std::move(cap), fit});
		}

		std::vector<std::string> missing_txt;
		if (fs::exists(vcd_dir)) {
			for (const auto& ent : fs::directory_iterator(vcd_dir)) {
				if (!ent.is_regular_file()) continue;
				auto name = ent.path().filename().string();
				if (name.size() < 4 || name.substr(name.size() - 4) != ".vcd") continue;
				auto stem = ent.path().stem().string();
				if (stem.find("rdCpu") == std::string::npos &&
				    stem.find("wrCpu") == std::string::npos) continue;
				if (stem.find("noCpu") != std::string::npos) continue;
				fs::path txt = slots_dir / (stem + ".txt");
				if (!fs::exists(txt)) missing_txt.push_back(stem);
			}
			std::sort(missing_txt.begin(), missing_txt.end());
		}

		std::cout << "\ncoverage    " << n_files << " txt scored"
			  << "  no-cpu " << n_no_cpu
			  << "  no-eng " << n_no_eng
			  << "  missing txt " << missing_txt.size() << "\n";
		if (!missing_txt.empty()) {
			std::cout << "missing 5.slots txt (VCD exists):";
			for (auto& s : missing_txt) std::cout << " " << s;
			std::cout << "\n";
		}

		std::cout << "CPU all     " << n_cpu_ok << '/' << n_cpu_run << " perfect  "
			  << tot_cpu_h << '/' << tot_cpu_n;
		if (tot_cpu_n) std::cout << " (" << std::fixed << std::setprecision(1)
					 << (100.0 * tot_cpu_h / tot_cpu_n) << "%)";
		std::cout << " extra " << tot_cpu_e << " miss " << tot_cpu_m << "\n";
		std::cout << "CPU stop    " << n_stop_ok << '/' << n_stop_run << " perfect  "
			  << tot_stop_h << '/' << tot_stop_n;
		if (tot_stop_n) std::cout << " (" << std::fixed << std::setprecision(1)
					 << (100.0 * tot_stop_h / tot_stop_n) << "%)";
		std::cout << " extra " << tot_stop_e << " miss " << tot_stop_m << "\n";
		int n_cmd_cpu = n_cpu_run - n_stop_run;
		int n_cmd_ok = n_cpu_ok - n_stop_ok;
		int tot_cmd_h = tot_cpu_h - tot_stop_h;
		int tot_cmd_n = tot_cpu_n - tot_stop_n;
		int tot_cmd_e = tot_cpu_e - tot_stop_e;
		int tot_cmd_m = tot_cpu_m - tot_stop_m;
		std::cout << "CPU +cmd    " << n_cmd_ok << '/' << n_cmd_cpu << " perfect  "
			  << tot_cmd_h << '/' << tot_cmd_n;
		if (tot_cmd_n) std::cout << " (" << std::fixed << std::setprecision(1)
					 << (100.0 * tot_cmd_h / tot_cmd_n) << "%)";
		std::cout << " extra " << tot_cmd_e << " miss " << tot_cmd_m << "\n";
		std::cout << "CPU by mode:\n";
		for (int mi = 0; mi < 3; ++mi) {
			if (!mode_cpu_n[mi]) continue;
			std::cout << "  " << std::left << std::setw(8) << mode_name(Mode(mi))
				  << std::right << mode_cpu_ok[mi] << '/' << mode_cpu_n[mi] << " files  "
				  << mode_cpu_h[mi] << '/' << mode_cpu_e[mi];
			if (mode_cpu_e[mi]) std::cout << " (" << std::fixed << std::setprecision(1)
						     << (100.0 * mode_cpu_h[mi] / mode_cpu_e[mi]) << "%)";
			std::cout << "\n";
		}

		auto eline = [](const char* name, int ok, int nf, int h, int n, int e, int m) {
			std::cout << std::left << std::setw(12) << name << std::right
				  << ok << '/' << nf << " perfect  " << h << '/' << n;
			if (n) std::cout << " (" << std::fixed << std::setprecision(1)
					 << (100.0 * h / n) << "%)";
			std::cout << " extra " << e << " miss " << m << "\n";
		};
		std::cout << "\nengine (" << n_eng_run << " files with command tags):\n";
		eline("eng none", n_none_ok, n_eng_run, tot_h0, tot_n0, tot_e0, tot_m0);
		eline("eng RAS", n_obs_ok, n_eng_run, tot_ho, tot_no, tot_eo, tot_mo);
		eline("eng R..", n_rdd_ok, n_eng_run, tot_hr, tot_nr, tot_er, tot_mr);
		eline("eng pend", n_pred_ok, n_eng_run, tot_hp, tot_np, tot_ep, tot_mp);
		std::cout << "observed R.. dummy slots: " << tot_dummy << "\n";
		std::cout << "\nper command (eng occ-RAS / occ-R..):\n";
		for (int ci = 0; ci < 6; ++ci) {
			if (!cmd_n[ci]) continue;
			std::cout << "  " << std::left << std::setw(6) << cmd_name(Cmd(ci))
				  << std::right << cmd_ok[ci] << '/' << cmd_n[ci] << "  "
				  << cmd_h[ci] << '/' << cmd_e[ci];
			if (cmd_e[ci]) std::cout << " (" << std::fixed << std::setprecision(1)
						 << (100.0 * cmd_h[ci] / cmd_e[ci]) << "%)";
			std::cout << "   R.. " << cmd_ok_r[ci] << '/' << cmd_n[ci] << "  "
				  << cmd_h_r[ci] << '/' << cmd_e[ci];
			if (cmd_e[ci]) std::cout << " (" << std::fixed << std::setprecision(1)
						 << (100.0 * cmd_h_r[ci] / cmd_e[ci]) << "%)";
			std::cout << "\n";
		}
		std::cout << "CPU-stolen first-slots: occ-RAS " << tot_sk_o
			  << "  occ-R.. " << tot_sk_r
			  << "  occ-pend " << tot_sk_p
			  << "  same-RAS CPU+eng " << tot_col << "\n";

		int bg_h = -1, bg_e = 0, bg_m = 0, bg_n = 0, bg_d = 0, bg_ei = 0;
		for (int ei = 0; ei < 2; ++ei) {
			for (int gi = 0; gi < ND; ++gi) {
				int h = glob_hit[ei][gi];
				int err = glob_extra[ei][gi] + glob_miss[ei][gi];
				int nerr = bg_e + bg_m;
				if (h > bg_h || (h == bg_h && err < nerr)) {
					bg_h = h;
					bg_e = glob_extra[ei][gi];
					bg_m = glob_miss[ei][gi];
					bg_n = glob_n[ei][gi];
					bg_d = gi + DELTA_LO;
					bg_ei = ei;
				}
			}
		}
		const char* ename[] = {"fall", "rise"};
		std::cout << "global best CPU δ=" << bg_d << " " << ename[bg_ei] << "  "
			  << bg_h << '/' << bg_n;
		if (bg_n) std::cout << " (" << std::fixed << std::setprecision(1)
				    << (100.0 * bg_h / bg_n) << "%)";
		std::cout << " extra " << bg_e << " miss " << bg_m << "\n";

		// Leftovers after D16-tie scoring, and whether they sit at the capture edge.
		std::cout << "\nD16 leftovers (start = first 2 CPU slots or first 72 cycles; "
			  << "end = last 2 or last 72):\n";
		int n_start = 0, n_end = 0, n_mid = 0;
		for (auto& L : loaded) {
			if (L.fit.extra == 0 && L.fit.miss == 0) continue;
			const auto& w = cpu_wait[mode_index(L.cap.mode)];
			auto posts = make_posts(L.cap.t2[L.fit.edge_i], L.fit.delta, w, L.cap.tmin, L.cap.tmax);
			auto sim = vdp_cpu_posts(posts, w, L.cap.tmin, L.cap.tmax);
			auto ev = align_d16(sim.pred, L.cap.obs);
			int tmin = L.cap.tmin, tmax = L.cap.tmax;
			int start_lim = tmin + 72;
			int end_lim = tmax - 72;
			if (L.cap.obs.size() >= 3) {
				start_lim = std::max(start_lim, L.cap.obs[2]);
				end_lim = std::min(end_lim, L.cap.obs[L.cap.obs.size() - 3]);
			}
			std::cout << "  " << L.cap.name << "  tmin=" << tmin << " tmax=" << tmax << "\n";
			for (auto& e : ev) {
				if (e.kind != Align::Extra && e.kind != Align::Miss) continue;
				const char* where = "mid";
				if (e.t <= start_lim) {
					where = "START";
					++n_start;
				} else if (e.t >= end_lim) {
					where = "END";
					++n_end;
				} else {
					++n_mid;
				}
				std::cout << "    " << (e.kind == Align::Extra ? "extra" : "miss ")
					  << " RAS " << e.t << " row " << (e.t % LINE)
					  << "  +" << (e.t - tmin) << " from start"
					  << "  " << (tmax - e.t) << " to end"
					  << "  " << where << "\n";
			}
		}
		std::cout << "  leftover extra+miss: start " << n_start
			  << "  mid " << n_mid << "  end " << n_end << "\n";

		if (!do_wiggle && !do_diag) {
			bool cpu_all = (n_cpu_ok == n_cpu_run);
			bool eng_all = (n_obs_ok == n_eng_run);
			return (cpu_all && eng_all && missing_txt.empty()) ? 0 : 1;
		}

		if (do_diag) {
			for (auto& L : loaded) {
				if (!diag_filter.empty() && L.cap.name.find(diag_filter) == std::string::npos) {
					continue;
				}
				if (diag_filter.empty() && L.fit.extra == 0 && L.fit.miss == 0) continue;
				dump_diag(L.cap, L.fit, cpu_wait);
			}
			if (!do_wiggle) return 0;
		}

		std::cout << "\nHoldoff after RAS (same δ/edge as D16 best; not a new search):\n";
		struct Hoff {
			const char* name;
			int lo;
		};
		Hoff hoffs[] = {
			{"[S, S+2) 2013", 0},
			{"(S, S+2) skip T==S", 1},
			{"off", BUSY},
		};
		for (auto& h : hoffs) {
			int hit = 0, extra = 0, miss = 0, ties = 0, okf = 0, n = 0;
			int h0 = 0, h1 = 0, ow = 0;
			std::cout << "  " << h.name << ":\n";
			for (auto& L : loaded) {
				const auto& w = cpu_wait[mode_index(L.cap.mode)];
				auto posts = make_posts(L.cap.t2[L.fit.edge_i], L.fit.delta, w,
							L.cap.tmin, L.cap.tmax);
				auto sim = vdp_cpu_posts(posts, w, L.cap.tmin, L.cap.tmax, h.lo);
				auto ev = align_d16(sim.pred, L.cap.obs);
				auto sc = score_d16(ev, L.cap.tmin, L.cap.tmax, 0, 0);
				hit += sc.hit;
				extra += sc.extra;
				miss += sc.miss;
				ties += sc.ties;
				n += int(L.cap.obs.size());
				h0 += sim.holdoff_dt0;
				h1 += sim.holdoff_dt1;
				ow += sim.overwrites;
				if (sc.extra == 0 && sc.miss == 0) ++okf;
				if (sc.extra || sc.miss) {
					std::cout << "    " << L.cap.name
						  << "  " << sc.hit << '/' << L.cap.obs.size()
						  << " extra " << sc.extra << " miss " << sc.miss
						  << "  h0/h1 " << sim.holdoff_dt0 << '/' << sim.holdoff_dt1
						  << " ow=" << sim.overwrites << "\n";
				}
			}
			std::cout << "    total " << okf << '/' << loaded.size() << " files  "
				  << hit << '/' << n;
			if (n) std::cout << " (" << std::fixed << std::setprecision(1)
					 << (100.0 * hit / n) << "%)";
			std::cout << " extra " << extra << " miss " << miss
				  << " ties " << ties
				  << "  holdoff T==S/S+1 " << h0 << '/' << h1
				  << " ow=" << ow << "\n";
		}

		std::cout << "\nIgnore start (and optionally end); D16 scoring on the rest:\n";
		struct Guard {
			const char* name;
			int start_g;
			int end_g;
		};
		Guard guards[] = {
			{"full window", 0, 0},
			{"drop first 72 cyc", 72, 0},
			{"drop first 252 cyc", 252, 0},
			{"drop first line", LINE, 0},
			{"drop first+last 72", 72, 72},
			{"drop first+last line", LINE, LINE},
		};
		for (auto& g : guards) {
			int hit = 0, n = 0, extra = 0, miss = 0, ties = 0, okf = 0;
			for (auto& L : loaded) {
				const auto& w = cpu_wait[mode_index(L.cap.mode)];
				auto posts = make_posts(L.cap.t2[L.fit.edge_i], L.fit.delta, w,
							L.cap.tmin, L.cap.tmax);
				auto sim = vdp_cpu_posts(posts, w, L.cap.tmin, L.cap.tmax);
				auto ev = align_d16(sim.pred, L.cap.obs);
				auto sc = score_d16(ev, L.cap.tmin, L.cap.tmax, g.start_g, g.end_g);
				int nn = counted_n(L.cap.obs, L.cap.tmin, L.cap.tmax, g.start_g, g.end_g);
				hit += sc.hit;
				n += nn;
				extra += sc.extra;
				miss += sc.miss;
				ties += sc.ties;
				if (sc.extra == 0 && sc.miss == 0) ++okf;
			}
			std::cout << "  " << std::left << std::setw(22) << g.name
				  << std::right << okf << '/' << loaded.size() << " files  "
				  << hit << '/' << n;
			if (n) std::cout << " (" << std::fixed << std::setprecision(1)
					 << (100.0 * hit / n) << "%)";
			std::cout << " extra " << extra << " miss " << miss
				  << " ties " << ties << "\n";
		}

		struct Pol {
			WigPolicy pol;
			const char* name;
		};
		Pol pols[] = {
			{WigPolicy::Nominal, "nominal"},
			{WigPolicy::AlwaysEarly, "always T-1"},
			{WigPolicy::AlwaysLate, "always T+1"},
			{WigPolicy::BoundOracle, "oracle ±1 bound"},
			{WigPolicy::BoundOracle2, "oracle ±2 bound"},
			{WigPolicy::Oracle, "oracle ±1 always"},
		};

		auto run_set = [&](const char* label, bool use_global) {
			std::cout << "\n--- wiggle, " << label << " ---\n";
			for (auto& p : pols) {
				int hit = 0, n = 0, extra = 0, miss = 0, bound = 0, m1 = 0, z = 0, p1 = 0, hf = 0, okf = 0;
				for (auto& L : loaded) {
					int ei = use_global ? bg_ei : L.fit.edge_i;
					int d = use_global ? bg_d : L.fit.delta;
					const auto& w = cpu_wait[mode_index(L.cap.mode)];
					auto posts = make_posts(L.cap.t2[ei], d, w, L.cap.tmin, L.cap.tmax);
					auto st = vdp_cpu_wiggle(posts, w, L.cap.tmin, L.cap.tmax, L.cap.obs, p.pol);
					auto sc = score_times(st.pred, L.cap.obs);
					hit += sc.hit;
					n += int(L.cap.obs.size());
					extra += sc.extra;
					miss += sc.miss;
					bound += st.n_bound;
					m1 += st.used_m1;
					z += st.used_0;
					p1 += st.used_p1;
					hf += st.holdoff_flip;
					if (sc.hit == int(L.cap.obs.size()) && sc.extra == 0) ++okf;
				}
				std::cout << std::left << std::setw(22) << p.name
					  << std::right << std::setw(5) << okf << '/' << loaded.size()
					  << " files  " << hit << '/' << n;
				if (n) std::cout << " (" << std::fixed << std::setprecision(1)
						 << (100.0 * hit / n) << "%)";
				std::cout << " extra " << extra << " miss " << miss
					  << "  bound " << bound
					  << "  dt -1/0/+1 " << m1 << '/' << z << '/' << p1
					  << "  holdoff± " << hf << "\n";
			}
		};
		run_set("per-file δ/edge", false);
		run_set("global δ/edge", true);
		return 0;
	}
	return 0;
}
