// 2013 NMS 8250 CPU + HMMV slot model.
// This file was written with assistance from an AI coding agent.
//
//   g++ -O3 -std=c++20 -o fit_2013 fit_2013.cc
//   ./fit_2013 [dir]
//
// CPU: 40 posts at 72, then 252 (period 3060). D16 with engine-distance,
// overwrite-keep-slot, 2-cycle holdoff after RAS, no packed +6 slots.
// HMMV: P=46, newline 104 (sprites-on +1); skip CPU-occupied slots.
// First observed W.e is seeded.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
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
constexpr int NEED = 16;
constexpr int BUSY = 2;
constexpr int HMMV_P = 46;
constexpr int HMMV_NL = 104;

enum class Mode { DispOff, SprOff, SprOn };

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

// wait[t] = (first slot S with engine_dist(t,S) >= need) - t, t in 0..LINE-1
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
			if (s - t - 2 * n_stretch(t, s, tab.stretch0, tab.stretch1) >= need) {
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
	int phi0, const std::vector<int>& wait, int tmin, int tmax)
{
	int i0 = 0;
	while (z80_request(phi0, i0) > tmin - 2 * PERIOD) --i0;
	int i1 = i0;
	while (z80_request(phi0, i1) <= tmax + NEED + 400) ++i1;

	CpuSim out;
	bool occupied = false;
	std::optional<int> sched;
	std::optional<int> last_s;
	int i = i0;
	while (i < i1 || occupied) {
		std::optional<int> t_req;
		if (i < i1) t_req = z80_request(phi0, i);
		if (!occupied && last_s && t_req && *t_req < *last_s + BUSY) {
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
		}
		++i;
	}
	return out;
}

struct Score {
	int hit = 0;
	int extra = 0;
	int miss = 0;
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

static CpuFit fit_cpu(const std::vector<int>& obs, const std::vector<int>& wait)
{
	int tmin = obs.front();
	int tmax = obs.back();
	CpuFit best;
	bool have = false;
	for (int phi0 = 0; phi0 < PERIOD; ++phi0) {
		auto sim = vdp_cpu(phi0, wait, tmin, tmax);
		auto sc = score(sim.pred, obs);
		int err = sc.extra + sc.miss;
		int nerr = have ? best.sc.extra + best.sc.miss : 1 << 30;
		if (!have || sc.hit > best.sc.hit ||
		    (sc.hit == best.sc.hit && err < nerr) ||
		    (sc.hit == best.sc.hit && err == nerr && sim.overwrites < best.ow)) {
			best.phi0 = phi0;
			best.ow = sim.overwrites;
			best.sc = sc;
			best.pred = std::move(sim.pred);
			have = true;
		}
		if (sc.hit == int(obs.size()) && sc.extra == 0 && sim.overwrites == 0) {
			// cannot beat a perfect zero-overwrite fit
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
		last = next_cmd_slot(last, delta, tab, occupied);
		pred.push_back(last);
		addr = (addr + 1) & 0x1FFFF;
	}
	return pred;
}

struct Access {
	int ras = 0;
	int addr = 0;
	char rw = 0;
	char type = 0;
};

struct Parsed {
	std::vector<int> cpu;
	std::vector<std::pair<int, int>> eng; // ras, addr
};

static Parsed parse_file(const fs::path& path)
{
	std::ifstream in(path);
	if (!in) throw std::runtime_error("cannot open " + path.string());
	Parsed p;
	std::string line;
	while (std::getline(in, line)) {
		auto colon = line.find(':');
		if (colon == std::string::npos) continue;
		int row = int(std::strtol(line.c_str(), nullptr, 10));
		std::string_view rest(line.c_str() + colon + 1, line.size() - (colon + 1));
		int col = 0;
		while (rest.size() >= 13 || (rest.size() > 2 && rest.substr(2).find_first_not_of(' ') != std::string_view::npos)) {
			std::string_view cell = rest.size() >= 13 ? rest.substr(2, 11) : rest.substr(2);
			auto nonempty = cell.find_first_not_of(' ');
			if (nonempty != std::string_view::npos) {
				char rw = cell[0];
				char type = cell.size() >= 3 ? cell[2] : 0;
				int ras = LINE * col + row - 1;
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
	return p;
}

static bool is_cpu_file(std::string_view name)
{
	if (name.find("nocpu") != std::string_view::npos) return false;
	return name.find("cpuread") != std::string_view::npos ||
	       name.find("cpuwrite") != std::string_view::npos;
}

int main(int argc, char** argv)
{
	fs::path dir = (argc > 1) ? fs::path(argv[1]) : fs::current_path();

	std::vector<int> cpu_wait[3];
	std::vector<int> cpu_slot[3];
	for (int m = 0; m < 3; ++m) {
		cpu_slot[m] = cpu_slots_of(CMD_TABLE[m]);
		cpu_wait[m] = make_wait(cpu_slot[m], CMD_TABLE[m], NEED);
	}

	std::vector<fs::path> files;
	for (const auto& ent : fs::directory_iterator(dir)) {
		if (!ent.is_regular_file()) continue;
		auto name = ent.path().filename().string();
		if (ent.path().extension() != ".txt") continue;
		if (is_cpu_file(name)) files.push_back(ent.path());
	}
	std::sort(files.begin(), files.end());

	std::cout << "file                                       mode     CPU            HMMV           phi0\n";
	int n_cpu = 0, ok_cpu = 0, n_hmmv = 0, ok_hmmv = 0;
	for (const auto& path : files) {
		std::string name = path.filename().string();
		Mode mode = mode_of(name);
		auto parsed = parse_file(path);
		if (parsed.cpu.empty()) continue;
		++n_cpu;
		auto fit = fit_cpu(parsed.cpu, cpu_wait[mode_index(mode)]);
		bool cpu_ok = fit.sc.hit == int(parsed.cpu.size()) && fit.sc.extra == 0;
		ok_cpu += cpu_ok;

		bool hmmv_ok = true;
		int eh = 0, en = int(parsed.eng.size()), ex = 0, em = 0;
		if (!parsed.eng.empty()) {
			++n_hmmv;
			std::unordered_set<int> occ(fit.pred.begin(), fit.pred.end());
			int pitch = (name.find("screen8") != std::string::npos) ? 256 : 128;
			std::vector<int> obs_e;
			obs_e.reserve(parsed.eng.size());
			for (auto [ras, _] : parsed.eng) obs_e.push_back(ras);
			auto pred_e = predict_hmmv(
				parsed.eng.front().first, parsed.eng.front().second,
				int(parsed.eng.size()), mode, pitch, occ);
			auto es = score(pred_e, obs_e);
			eh = es.hit;
			ex = es.extra;
			em = es.miss;
			hmmv_ok = es.hit == en && es.extra == 0;
			ok_hmmv += hmmv_ok;
		}

		std::cout << std::left << std::setw(42) << name << ' '
			  << std::setw(8) << mode_name(mode)
			  << std::right << std::setw(3) << fit.sc.hit << '/'
			  << std::left << std::setw(3) << parsed.cpu.size()
			  << (cpu_ok ? "OK" : "  ") << "  ";
		if (parsed.eng.empty()) {
			std::cout << "   -/-   --  ";
		} else {
			std::cout << std::right << std::setw(3) << eh << '/'
				  << std::left << std::setw(3) << en
				  << (hmmv_ok ? "OK" : "  ") << "  ";
		}
		std::cout << "phi0=" << std::setw(4) << fit.phi0
			  << "  %" << LINE << '=' << std::setw(4) << (fit.phi0 % LINE)
			  << (cpu_ok && hmmv_ok ? "  OK" : "")
			  << '\n';
		(void)ex;
		(void)em;
		(void)fit.ow;
	}
	std::cout << "\nCPU   " << ok_cpu << '/' << n_cpu << " perfect"
		  << "\nHMMV  " << ok_hmmv << '/' << n_hmmv << " perfect\n";
	return (ok_cpu == n_cpu && ok_hmmv == n_hmmv) ? 0 : 1;
}
