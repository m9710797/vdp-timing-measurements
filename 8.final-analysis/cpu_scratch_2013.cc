// Rebuild the 2013 CPU (and CPU+HMMV) arbiter from traces.
// This file was written with assistance from an AI coding agent.
// Does not change fit_2013.cc. Command waits are FINDINGS4 + packed-start +1.
//
//   g++ -O3 -std=c++20 -o cpu_scratch_2013 cpu_scratch_2013.cc
//   ./cpu_scratch_2013 [dir]
//
// Frozen: CPU slots = command table minus packed +6; Z80 72/252/3060;
// HMMV P=46 / NL=104 (sprites-on +1; sprites-off packed-start +1).
// Everything else is searched.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

constexpr int LINE = 1368;
constexpr int N_IO = 40;
constexpr int IO_VDP = 72;
constexpr int LOOP_VDP = 252;
constexpr int PERIOD = (N_IO - 1) * IO_VDP + LOOP_VDP; // 3060
constexpr int HMMV_P = 46;
constexpr int HMMV_NL = 104;

enum class Mode { DispOff, SprOff, SprOn };
enum class OwPolicy { KeepSlot, Reschedule, DropNew };
enum class DistKind { Engine, Wall };

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

struct SlotTable {
	const int* data;
	int n;
	int stretch0;
	int stretch1;
};

constexpr SlotTable CMD_TABLE[] = {
	{SLOTS_DISPOFF, int(std::size(SLOTS_DISPOFF)), 1334, 1344},
	{SLOTS_SPROFF,  int(std::size(SLOTS_SPROFF)),  1332, 1342},
	{SLOTS_SPRON,   int(std::size(SLOTS_SPRON)),   1332, 1342},
};

static int mode_index(Mode m) { return int(m); }

static Mode mode_of(std::string_view name)
{
	if (name.find("screenoff") != std::string_view::npos) return Mode::DispOff;
	if (name.find("nosprites") != std::string_view::npos) return Mode::SprOff;
	if (name.find("sprite") != std::string_view::npos) return Mode::SprOn;
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

static const char* pol_name(OwPolicy p)
{
	switch (p) {
	case OwPolicy::KeepSlot:   return "keep";
	case OwPolicy::Reschedule: return "resched";
	case OwPolicy::DropNew:    return "dropnew";
	}
	return "?";
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

static int n_stretch(int t, int s, int c0, int c1)
{
	if (s <= t) return 0;
	int n = 0;
	for (int c : {c0, c1}) {
		int kmin = floor_div(t - c, LINE) + 1;
		int kmax = floor_div(s - c, LINE);
		if (kmax >= kmin) n += kmax - kmin + 1;
	}
	return n;
}

static bool stretch_cas(Mode m, int row)
{
	if (m == Mode::DispOff) return row == 1326 || row == 1336;
	if (m == Mode::SprOff) return row == 1324 || row == 1334;
	return false;
}

static int cas_to_ras(Mode m, int t)
{
	int row = mod_line(t);
	return t - 1 - (stretch_cas(m, row) ? 1 : 0);
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

static std::unordered_set<int> set_of(const std::vector<int>& v)
{
	return {v.begin(), v.end()};
}

static std::vector<int> make_wait(
	const std::vector<int>& slots, const SlotTable& tab, int need, DistKind dist)
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
			int d = s - t;
			if (dist == DistKind::Engine) {
				d -= 2 * n_stretch(t, s, tab.stretch0, tab.stretch1);
			}
			if (d >= need) {
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

static int z80_request(int phi0, int i)
{
	int burst = i / N_IO;
	int k = i % N_IO;
	if (k < 0) {
		--burst;
		k += N_IO;
	}
	return phi0 + burst * PERIOD + k * IO_VDP;
}

struct CpuSim {
	std::vector<int> pred;
	int overwrites = 0;
};

static CpuSim vdp_cpu(
	int phi0, const std::vector<int>& wait, int tmin, int tmax,
	int busy, OwPolicy pol)
{
	int i0 = 0;
	while (z80_request(phi0, i0) > tmin - 2 * PERIOD) --i0;
	int i1 = i0;
	while (z80_request(phi0, i1) <= tmax + 400) ++i1;

	CpuSim out;
	bool occupied = false;
	std::optional<int> sched;
	std::optional<int> last_s;
	int i = i0;
	while (i < i1 || occupied) {
		std::optional<int> t_req;
		if (i < i1) t_req = z80_request(phi0, i);
		if (!occupied && last_s && t_req && *t_req < *last_s + busy) {
			++out.overwrites;
			++i;
			continue;
		}
		if (occupied && sched && (!t_req || *sched <= *t_req)) {
			if (tmin <= *sched && *sched <= tmax) out.pred.push_back(*sched);
			last_s = sched;
			occupied = false;
			sched.reset();
			continue;
		}
		if (!t_req) break;
		if (!occupied) {
			occupied = true;
			sched = first_slot(wait, *t_req);
		} else {
			++out.overwrites;
			if (pol == OwPolicy::Reschedule) {
				sched = first_slot(wait, *t_req);
			} else if (pol == OwPolicy::DropNew) {
				// keep old request / slot
			}
			// KeepSlot: data overwritten, scheduled slot kept
		}
		++i;
	}
	return out;
}

struct Score {
	int hit = 0;
	int extra = 0;
	int miss = 0;
	int err() const { return extra + miss; }
};

static Score score(const std::vector<int>& pred, const std::vector<int>& obs)
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

struct CpuFit {
	int phi0 = 0;
	int ow = 0;
	Score sc;
	std::vector<int> pred;
};

static CpuFit fit_cpu(
	const std::vector<int>& obs, const std::vector<int>& wait,
	int busy, OwPolicy pol)
{
	int tmin = obs.front();
	int tmax = obs.back();
	CpuFit best;
	bool have = false;
	for (int phi0 = 0; phi0 < PERIOD; ++phi0) {
		auto sim = vdp_cpu(phi0, wait, tmin, tmax, busy, pol);
		auto sc = score(sim.pred, obs);
		int err = sc.err();
		int nerr = have ? best.sc.err() : 1 << 30;
		if (!have || sc.hit > best.sc.hit ||
		    (sc.hit == best.sc.hit && err < nerr) ||
		    (sc.hit == best.sc.hit && err == nerr && sim.overwrites < best.ow)) {
			best.phi0 = phi0;
			best.ow = sim.overwrites;
			best.sc = sc;
			best.pred = std::move(sim.pred);
			have = true;
		}
		if (sc.hit == int(obs.size()) && sc.extra == 0) {
			break;
		}
	}
	return best;
}

static int next_cmd_slot(
	int last, int delta, const SlotTable& tab,
	const std::unordered_set<int>& occupied)
{
	int base = last - mod_line(last);
	for (int wrap = 0; wrap < 4; ++wrap) {
		for (int i = 0; i < tab.n; ++i) {
			int s = base + wrap * LINE + tab.data[i];
			if (s <= last) continue;
			if (s - last - 2 * n_stretch(last, s, tab.stretch0, tab.stretch1) < delta) {
				continue;
			}
			if (occupied.count(s)) continue;
			return s;
		}
	}
	throw std::runtime_error("no command slot");
}

static std::vector<int> predict_hmmv(
	int first_ras, int first_addr, int n, Mode mode, int pitch,
	const std::unordered_set<int>& occupied)
{
	const SlotTable& tab = CMD_TABLE[mode_index(mode)];
	int spr = (mode == Mode::SprOn) ? 1 : 0;
	int p = HMMV_P + spr;
	int nl = HMMV_NL + spr;
	std::vector<int> pred;
	pred.reserve(n);
	pred.push_back(first_ras);
	int last = first_ras;
	int addr = first_addr;
	for (int k = 1; k < n; ++k) {
		int delta = (addr % pitch == pitch - 1) ? nl : p;
		// Packed-start +1 (FINDINGS5 §6). HMMV has no Δ=32 dest-read.
		if (mode == Mode::SprOff && is_packed_ras(last)) delta += 1;
		last = next_cmd_slot(last, delta, tab, occupied);
		pred.push_back(last);
		addr = (addr + 1) & 0x1FFFF;
	}
	return pred;
}

struct Parsed {
	std::string name;
	Mode mode = Mode::DispOff;
	bool nocmd = false;
	bool has_cpu = false;
	int pitch = 128;
	std::vector<int> cpu;
	std::vector<std::pair<int, int>> eng; // ras, addr
};

static bool is_cpu_file(std::string_view name)
{
	if (name.find("nocpu") != std::string_view::npos) return false;
	return name.find("cpuread") != std::string_view::npos ||
	       name.find("cpuwrite") != std::string_view::npos;
}

static Parsed parse_file(const fs::path& path)
{
	std::ifstream in(path);
	if (!in) throw std::runtime_error("cannot open " + path.string());
	Parsed p;
	p.name = path.filename().string();
	p.mode = mode_of(p.name);
	p.nocmd = p.name.find("nocmd") != std::string::npos;
	p.pitch = (p.name.find("screen8") != std::string::npos) ? 256 : 128;
	std::string line;
	while (std::getline(in, line)) {
		auto colon = line.find(':');
		if (colon == std::string_view::npos) continue;
		int row = int(std::strtol(line.c_str(), nullptr, 10));
		std::string_view rest(line.c_str() + colon + 1, line.size() - (colon + 1));
		int col = 0;
		while (rest.size() >= 13 || (rest.size() > 2 && rest.substr(2).find_first_not_of(' ') != std::string_view::npos)) {
			std::string_view cell = rest.size() >= 13 ? rest.substr(2, 11) : rest.substr(2);
			auto nonempty = cell.find_first_not_of(' ');
			if (nonempty != std::string_view::npos) {
				char rw = cell[0];
				char type = cell.size() >= 3 ? cell[2] : 0;
				int ras = cas_to_ras(p.mode, LINE * col + row);
				int addr = 0;
				auto hx = cell.find("0x");
				if (hx != std::string_view::npos) {
					addr = int(std::strtol(cell.data() + hx, nullptr, 16));
				}
				if ((rw == 'W' || rw == 'R') && type == 'c') {
					p.cpu.push_back(ras);
				} else if (rw == 'W' && type == 'e') {
					p.eng.push_back({ras, addr});
				}
			}
			if (rest.size() < 13) break;
			rest.remove_prefix(13);
			++col;
		}
	}
	std::sort(p.cpu.begin(), p.cpu.end());
	std::sort(p.eng.begin(), p.eng.end());
	p.has_cpu = !p.cpu.empty();
	return p;
}

struct Classify {
	int n = 0;
	int cpu_legal = 0;
	int packed = 0;
	int off_table = 0;
	int cmd_table = 0;
};

static int classify_ras(
	int ras, const std::unordered_set<int>& legal,
	const std::unordered_set<int>& packed,
	const std::unordered_set<int>& cmd)
{
	int row = mod_line(ras);
	if (legal.count(row)) return 0;
	if (packed.count(row)) return 1;
	if (cmd.count(row)) return 2;
	return 3;
}

int main(int argc, char** argv)
{
	fs::path dir = (argc > 1) ? fs::path(argv[1]) : fs::current_path();

	std::vector<int> cpu_slot[3];
	std::vector<int> packed_slot[3];
	std::unordered_set<int> legal_set[3];
	std::unordered_set<int> packed_set[3];
	std::unordered_set<int> cmd_set[3];
	for (int m = 0; m < 3; ++m) {
		cpu_slot[m] = cpu_slots_of(CMD_TABLE[m]);
		packed_slot[m] = packed_slots_of(CMD_TABLE[m]);
		legal_set[m] = set_of(cpu_slot[m]);
		packed_set[m] = set_of(packed_slot[m]);
		for (int i = 0; i < CMD_TABLE[m].n; ++i) cmd_set[m].insert(CMD_TABLE[m].data[i]);
	}

	std::vector<Parsed> files;
	for (const auto& ent : fs::directory_iterator(dir)) {
		if (!ent.is_regular_file()) continue;
		if (ent.path().extension() != ".txt") continue;
		auto name = ent.path().filename().string();
		if (!is_cpu_file(name)) continue;
		files.push_back(parse_file(ent.path()));
	}
	std::sort(files.begin(), files.end(), [](const Parsed& a, const Parsed& b) {
		return a.name < b.name;
	});

	std::cout << "=== A. occupancy (RAS, FINDINGS4 CAS→RAS) ===\n";
	std::cout << std::left << std::setw(46) << "file"
		  << "mode    cpu  legal packed off  eng  cmdTbl packed\n";
	Classify tot_cpu[3]{}, tot_eng[3]{};
	for (const auto& f : files) {
		int mi = mode_index(f.mode);
		Classify cc{}, ee{};
		cc.n = int(f.cpu.size());
		for (int ras : f.cpu) {
			int k = classify_ras(ras, legal_set[mi], packed_set[mi], cmd_set[mi]);
			if (k == 0) ++cc.cpu_legal;
			else if (k == 1) ++cc.packed;
			else ++cc.off_table;
		}
		ee.n = int(f.eng.size());
		for (auto [ras, _] : f.eng) {
			int k = classify_ras(ras, legal_set[mi], packed_set[mi], cmd_set[mi]);
			if (k == 0) ++ee.cpu_legal;
			else if (k == 1) ++ee.packed;
			else if (k == 2) ++ee.cmd_table;
			else ++ee.off_table;
		}
		tot_cpu[mi].n += cc.n;
		tot_cpu[mi].cpu_legal += cc.cpu_legal;
		tot_cpu[mi].packed += cc.packed;
		tot_cpu[mi].off_table += cc.off_table;
		tot_eng[mi].n += ee.n;
		tot_eng[mi].cpu_legal += ee.cpu_legal;
		tot_eng[mi].packed += ee.packed;
		tot_eng[mi].off_table += ee.off_table;
		std::cout << std::left << std::setw(46) << f.name
			  << std::setw(8) << mode_name(f.mode)
			  << std::right << std::setw(4) << cc.n << ' '
			  << std::setw(5) << cc.cpu_legal << ' '
			  << std::setw(6) << cc.packed << ' '
			  << std::setw(3) << cc.off_table << "  "
			  << std::setw(4) << ee.n << ' '
			  << std::setw(5) << (ee.cpu_legal + ee.packed) << ' '
			  << std::setw(6) << ee.packed
			  << (f.nocmd ? "  nocmd" : "  +HMMV")
			  << '\n';
	}
	std::cout << "CPU totals by mode (legal / packed / off):\n";
	for (int m = 0; m < 3; ++m) {
		std::cout << "  " << mode_name(Mode(m)) << ' '
			  << tot_cpu[m].cpu_legal << '/' << tot_cpu[m].packed << '/'
			  << tot_cpu[m].off_table << " of " << tot_cpu[m].n << '\n';
	}
	std::cout << "HMMV W.e on packed +6 / on CPU-legal / off-table:\n";
	for (int m = 0; m < 3; ++m) {
		if (!tot_eng[m].n) continue;
		std::cout << "  " << mode_name(Mode(m)) << " packed=" << tot_eng[m].packed
			  << " legal=" << tot_eng[m].cpu_legal
			  << " off=" << tot_eng[m].off_table
			  << " n=" << tot_eng[m].n << '\n';
	}

	std::vector<const Parsed*> nocmd;
	std::vector<const Parsed*> mixed;
	for (const auto& f : files) {
		if (!f.has_cpu) continue;
		if (f.nocmd) nocmd.push_back(&f);
		else mixed.push_back(&f);
	}

	std::cout << "\n=== B. CPU-only grid (nocmd files, own phi0) ===\n";
	std::cout << "slots: cpu-legal vs all-cmd; dist: engine vs wall; "
		     "NEED 1..32; BUSY 0..4; overwrite keep/resched/dropnew\n";

	struct Cfg {
		bool all_cmd = false;
		DistKind dist = DistKind::Engine;
		int need = 16;
		int busy = 2;
		OwPolicy pol = OwPolicy::KeepSlot;
		int n_perfect = 0;
		int tot_err = 0;
		int tot_ow = 0;
	};
	std::vector<Cfg> ranked;

	auto wait_for = [&](int mi, bool all_cmd, DistKind dist, int need) {
		const auto& slots = all_cmd
			? std::vector<int>(CMD_TABLE[mi].data, CMD_TABLE[mi].data + CMD_TABLE[mi].n)
			: cpu_slot[mi];
		return make_wait(slots, CMD_TABLE[mi], need, dist);
	};

	for (int all = 0; all < 2; ++all) {
		for (int di = 0; di < 2; ++di) {
			auto dist = di ? DistKind::Wall : DistKind::Engine;
			std::vector<int> waits[3][33];
			for (int need = 1; need <= 32; ++need) {
				for (int m = 0; m < 3; ++m) {
					waits[m][need] = wait_for(m, all, dist, need);
				}
			}
			for (int need = 1; need <= 32; ++need) {
				for (int busy = 0; busy <= 4; ++busy) {
					for (int pi = 0; pi < 3; ++pi) {
						auto pol = OwPolicy(pi);
						Cfg cfg;
						cfg.all_cmd = all;
						cfg.dist = dist;
						cfg.need = need;
						cfg.busy = busy;
						cfg.pol = pol;
						for (const Parsed* f : nocmd) {
							int mi = mode_index(f->mode);
							auto fit = fit_cpu(f->cpu, waits[mi][need], busy, pol);
							if (fit.sc.err() == 0) ++cfg.n_perfect;
							cfg.tot_err += fit.sc.err();
							cfg.tot_ow += fit.ow;
						}
						ranked.push_back(cfg);
					}
				}
			}
		}
	}

	std::sort(ranked.begin(), ranked.end(), [](const Cfg& a, const Cfg& b) {
		if (a.n_perfect != b.n_perfect) return a.n_perfect > b.n_perfect;
		if (a.tot_err != b.tot_err) return a.tot_err < b.tot_err;
		return a.tot_ow < b.tot_ow;
	});

	int n_nocmd = int(nocmd.size());
	std::cout << "nocmd files: " << n_nocmd << "\n";
	std::cout << "top configs (perfect files, tot extra+miss, overwrites):\n";
	int shown = 0;
	for (const auto& c : ranked) {
		if (shown >= 20 && c.n_perfect < n_nocmd) break;
		if (shown >= 40) break;
		std::cout << "  perfect " << c.n_perfect << '/' << n_nocmd
			  << "  err " << std::setw(5) << c.tot_err
			  << "  ow " << std::setw(5) << c.tot_ow
			  << "  " << (c.all_cmd ? "all-cmd" : "cpu-legal")
			  << "  " << (c.dist == DistKind::Engine ? "engine" : "wall  ")
			  << "  NEED=" << std::setw(2) << c.need
			  << "  BUSY=" << c.busy
			  << "  " << pol_name(c.pol)
			  << '\n';
		++shown;
		if (c.n_perfect == n_nocmd && shown >= 20) {
			// keep listing the full plateau a bit
		}
	}

	int plateau = 0;
	for (const auto& c : ranked) {
		if (c.n_perfect == n_nocmd && c.tot_err == ranked.front().tot_err) ++plateau;
		else break;
	}
	std::cout << "tied at best: " << plateau << " configs\n";

	// NEED plateaus for cpu-legal + engine + keep (the FINDINGS4-compatible family)
	std::cout << "\ncpu-legal + engine + keep: NEED × BUSY perfect counts\n";
	std::cout << "NEED\\BUSY";
	for (int b = 0; b <= 4; ++b) std::cout << std::setw(6) << b;
	std::cout << '\n';
	for (int need = 1; need <= 32; ++need) {
		bool any = false;
		int row[5];
		for (int b = 0; b <= 4; ++b) {
			row[b] = 0;
			for (const auto& c : ranked) {
				if (!c.all_cmd && c.dist == DistKind::Engine &&
				    c.pol == OwPolicy::KeepSlot && c.need == need && c.busy == b) {
					row[b] = c.n_perfect;
					if (row[b]) any = true;
				}
			}
		}
		if (!any) continue;
		std::cout << std::setw(7) << need;
		for (int b = 0; b <= 4; ++b) std::cout << std::setw(6) << row[b];
		std::cout << '\n';
	}

	Cfg best = ranked.front();
	std::cout << "\n=== B2. per-file CPU with best config ===\n";
	std::cout << "using: " << (best.all_cmd ? "all-cmd" : "cpu-legal")
		  << " " << (best.dist == DistKind::Engine ? "engine" : "wall")
		  << " NEED=" << best.need << " BUSY=" << best.busy
		  << " " << pol_name(best.pol) << "\n";

	auto print_cpu_table = [&](const std::vector<const Parsed*>& group, const Cfg& cfg, const char* title) {
		std::cout << title << '\n';
		std::vector<int> waits[3];
		for (int m = 0; m < 3; ++m) waits[m] = wait_for(m, cfg.all_cmd, cfg.dist, cfg.need);
		for (const Parsed* f : group) {
			int mi = mode_index(f->mode);
			auto fit = fit_cpu(f->cpu, waits[mi], cfg.busy, cfg.pol);
			bool ok = fit.sc.err() == 0;
			std::cout << "  " << std::left << std::setw(46) << f->name
				  << std::right << std::setw(3) << fit.sc.hit << '/'
				  << std::setw(3) << f->cpu.size()
				  << (ok ? " OK" : "   ")
				  << " extra " << fit.sc.extra
				  << " miss " << fit.sc.miss
				  << " ow " << fit.ow
				  << " phi0=" << fit.phi0
				  << '\n';
		}
	};
	print_cpu_table(nocmd, best, "nocmd:");
	print_cpu_table(mixed, best, "mixed (CPU only, ignore command):");

	// Reference: documented NEED=16 BUSY=2 keep engine cpu-legal
	Cfg ref;
	ref.all_cmd = false;
	ref.dist = DistKind::Engine;
	ref.need = 16;
	ref.busy = 2;
	ref.pol = OwPolicy::KeepSlot;
	if (best.need != 16 || best.busy != 2 || best.pol != OwPolicy::KeepSlot ||
	    best.all_cmd || best.dist != DistKind::Engine) {
		std::cout << "\nreference D16 BUSY=2 keep engine cpu-legal:\n";
		print_cpu_table(nocmd, ref, "nocmd:");
		print_cpu_table(mixed, ref, "mixed:");
	}

	std::cout << "\n=== C. mixed HMMV (FINDINGS4 waits; skip occupied CPU RAS) ===\n";
	auto eval_hmmv = [&](const Cfg& cfg, bool oracle) {
		std::vector<int> waits[3];
		for (int m = 0; m < 3; ++m) waits[m] = wait_for(m, cfg.all_cmd, cfg.dist, cfg.need);
		int n = 0, ok = 0;
		for (const Parsed* f : mixed) {
			if (f->eng.size() < 2) continue;
			++n;
			int mi = mode_index(f->mode);
			std::unordered_set<int> occ;
			if (oracle) {
				occ.insert(f->cpu.begin(), f->cpu.end());
			} else {
				auto fit = fit_cpu(f->cpu, waits[mi], cfg.busy, cfg.pol);
				occ.insert(fit.pred.begin(), fit.pred.end());
			}
			std::vector<int> obs_e;
			for (auto [ras, _] : f->eng) obs_e.push_back(ras);
			auto pred_e = predict_hmmv(
				f->eng.front().first, f->eng.front().second,
				int(f->eng.size()), f->mode, f->pitch, occ);
			auto es = score(pred_e, obs_e);
			bool good = es.err() == 0;
			ok += good;
			std::cout << "  " << std::left << std::setw(46) << f->name
				  << std::right << std::setw(3) << es.hit << '/'
				  << std::setw(3) << f->eng.size()
				  << (good ? " OK" : "   ")
				  << " extra " << es.extra << " miss " << es.miss
				  << '\n';
		}
		std::cout << "  HMMV perfect " << ok << '/' << n << "\n";
		return std::pair<int, int>{ok, n};
	};

	std::cout << "C1 oracle occupied = observed CPU RAS\n";
	eval_hmmv(best, true);
	std::cout << "C2 occupied = predicted CPU (best CPU config)\n";
	eval_hmmv(best, false);
	if (best.need != 16 || best.busy != 2) {
		std::cout << "C3 occupied = predicted CPU (D16 BUSY=2 keep)\n";
		eval_hmmv(ref, false);
	}

	// First leftover for oracle misses
	std::cout << "\nC4 first leftover (oracle occupied) if any\n";
	{
		for (const Parsed* f : mixed) {
			if (f->eng.size() < 2) continue;
			std::unordered_set<int> occ(f->cpu.begin(), f->cpu.end());
			std::vector<int> obs_e;
			for (auto [ras, _] : f->eng) obs_e.push_back(ras);
			auto pred_e = predict_hmmv(
				f->eng.front().first, f->eng.front().second,
				int(f->eng.size()), f->mode, f->pitch, occ);
			auto es = score(pred_e, obs_e);
			if (es.err() == 0) continue;
			int last = f->eng.front().first;
			int addr = f->eng.front().second;
			int spr = (f->mode == Mode::SprOn) ? 1 : 0;
			for (size_t k = 1; k < f->eng.size(); ++k) {
				int delta = (addr % f->pitch == f->pitch - 1) ? (HMMV_NL + spr) : (HMMV_P + spr);
				if (f->mode == Mode::SprOff && is_packed_ras(last)) delta += 1;
				int pred = next_cmd_slot(last, delta, CMD_TABLE[mode_index(f->mode)], occ);
				int obs = f->eng[k].first;
				if (pred != obs) {
					std::cout << "  " << f->name
						  << " k=" << k
						  << " last=" << last << " row " << mod_line(last)
						  << " Δ=" << delta
						  << " pred=" << pred << " row " << mod_line(pred)
						  << " obs=" << obs << " row " << mod_line(obs)
						  << " cpu@pred=" << occ.count(pred)
						  << " cpu@obs=" << occ.count(obs)
						  << '\n';
					break;
				}
				last = obs;
				addr = (addr + 1) & 0x1FFFF;
			}
		}
	}
	return 0;
}
