// Fit VDP command wait parameters (P / Pr / Pw / L) from part2/5.slots traces.
// This file was written with assistance from an AI coding agent.
// Usage:
//   g++ -O3 -std=c++20 -o fit_cmd_slots fit_cmd_slots.cc
//   ./fit_cmd_slots hmmm
//   ./fit_cmd_slots hmmv
//
// Slot tables: openMSX slotsScreenOff, and hardware-shifted 1324/1334 -> 1325/1335.

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

constexpr int LINE = 1368;

// Canonical openMSX slotsScreenOff (no +1 offset yet).
constexpr int SLOTS_OPENMSX[] = {
	0, 8, 16, 24, 32, 40, 48, 56, 64, 72,
	80, 88, 96, 104, 112, 120, 164, 172, 180, 188,
	196, 204, 212, 220, 228, 236, 244, 252, 260, 268,
	276, 292, 300, 308, 316, 324, 332, 340, 348, 356,
	364, 372, 380, 388, 396, 404, 420, 428, 436, 444,
	452, 460, 468, 476, 484, 492, 500, 508, 516, 524,
	532, 548, 556, 564, 572, 580, 588, 596, 604, 612,
	620, 628, 636, 644, 652, 660, 676, 684, 692, 700,
	708, 716, 724, 732, 740, 748, 756, 764, 772, 780,
	788, 804, 812, 820, 828, 836, 844, 852, 860, 868,
	876, 884, 892, 900, 908, 916, 932, 940, 948, 956,
	964, 972, 980, 988, 996, 1004, 1012, 1020, 1028, 1036,
	1044, 1060, 1068, 1076, 1084, 1092, 1100, 1108, 1116, 1124,
	1132, 1140, 1148, 1156, 1164, 1172, 1188, 1196, 1204, 1212,
	1220, 1228, 1268, 1276, 1284, 1292, 1300, 1308, 1316, 1324,
	1334, 1344, 1352, 1360,
};
constexpr int NUM_SLOTS = sizeof(SLOTS_OPENMSX) / sizeof(SLOTS_OPENMSX[0]);

enum class SlotKind { OpenMSX, Hardware };

struct Access {
	int time;   // absolute: 1368 * col + row_time (measurement timebase)
	int addr;
	char type;  // 'R' = R.s, 'W' = W.d, 'D' = R.d (future)
};

enum class TransKind {
	RsToWd,      // after R.s, wait Pw, then W.d
	WdToRsMid,   // after W.d, wait Pr, then R.s (same line)
	WdToRsBreak, // after W.d, wait Pr+L, then R.s (new line)
	WdToWdMid,   // HMMV mid-line: wait P
	WdToWdBreak, // HMMV line break: wait P+L
};

struct Pair {
	int t0 = 0;
	int t1 = 0;
	TransKind kind{};
};

struct SlotTable {
	std::array<int, LINE> slotWait{};
	std::array<bool, LINE> isSlot{};
	int onSlot = 0;
	int offSlot = 0;
};

SlotTable makeSlotTable(SlotKind kind)
{
	std::array<int, NUM_SLOTS> slots{};
	for (int i = 0; i < NUM_SLOTS; ++i) {
		int s = SLOTS_OPENMSX[i];
		if (kind == SlotKind::Hardware) {
			if (s == 1324) s = 1325;
			if (s == 1334) s = 1335;
		}
		// Measurement files use +1 offset vs canonical table.
		slots[i] = (s + 1) % LINE;
	}
	std::sort(slots.begin(), slots.end());

	SlotTable tab;
	for (int s : slots) {
		tab.isSlot[s] = true;
	}
	std::vector<int> ext;
	ext.reserve(NUM_SLOTS * 2);
	for (int s : slots) {
		ext.push_back(s);
	}
	for (int s : slots) {
		ext.push_back(s + LINE);
	}
	for (int pos = 0; pos < LINE; ++pos) {
		for (int s : ext) {
			if (s >= pos) {
				tab.slotWait[pos] = s - pos;
				break;
			}
		}
	}
	return tab;
}

int nextAfter(int t, int wait, const SlotTable& tab)
{
	const int ready = t + wait;
	return ready + tab.slotWait[ready % LINE];
}

enum class Variant { Wide, B64, C4 };

Variant parseVariant(std::string_view name)
{
	// ...-noCpu-<digit>[b|c].txt
	const auto pos = name.rfind("noCpu-");
	assert(pos != std::string_view::npos);
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

bool isLineBreak(int a0, int a1, Variant var)
{
	const int d = a1 - a0;
	switch (var) {
	case Variant::Wide:
		return (a0 / 0x80) != (a1 / 0x80);
	case Variant::B64:
		return d == 97 || ((a0 / 0x80) != (a1 / 0x80) && std::abs(d) != 1);
	case Variant::C4:
		return d == 127 || (std::abs(d) != 1 && (a0 / 0x80) != (a1 / 0x80));
	}
	return false;
}

// wantRead: 'R' = R.s, 'D' = R.d; wantW always W.d when non-zero.
std::vector<Access> parseFile(const fs::path& path, char wantRead, char wantW)
{
	std::ifstream in(path);
	assert(in);

	struct Row {
		int t;
		std::vector<std::pair<bool, Access>> cells; // present + data (time filled later)
	};
	std::vector<Row> rows;
	std::string line;
	while (std::getline(in, line)) {
		if (line.size() < 5 || line[4] != ':') continue;
		Row row;
		row.t = int(std::strtol(line.c_str(), nullptr, 10));
		std::string_view cols = std::string_view(line).substr(5);
		while (!cols.empty()) {
			std::string_view cell = cols.substr(0, std::min<size_t>(13, cols.size()));
			Access a{};
			bool present = false;
			if (cell.size() >= 13 && cell[2] != ' ') {
				const std::string_view typ = cell.substr(2, 3);
				const int addr = int(std::strtol(std::string(cell.substr(8, 5)).c_str(), nullptr, 16));
				if (typ == "R.s" && wantRead == 'R') {
					a = Access{0, addr, 'R'};
					present = true;
				} else if (typ == "R.d" && wantRead == 'D') {
					a = Access{0, addr, 'D'};
					present = true;
				} else if (typ == "W.d" && wantW == 'W') {
					a = Access{0, addr, 'W'};
					present = true;
				}
			}
			row.cells.push_back({present, a});
			if (cols.size() <= 13) break;
			cols.remove_prefix(13);
		}
		rows.push_back(std::move(row));
	}

	size_t nCols = 0;
	for (const auto& r : rows) {
		nCols = std::max(nCols, r.cells.size());
	}

	std::vector<Access> items;
	for (size_t col = 0; col < nCols; ++col) {
		for (const auto& r : rows) {
			if (col < r.cells.size() && r.cells[col].first) {
				Access a = r.cells[col].second;
				a.time = int(LINE * col + r.t);
				items.push_back(a);
			}
		}
	}
	std::sort(items.begin(), items.end(),
	          [](const Access& a, const Access& b) { return a.time < b.time; });
	return items;
}

// --- HMMV -----------------------------------------------------------------

std::vector<Pair> collectHmmvPairs(const fs::path& dir)
{
	std::vector<Pair> pairs;
	for (const auto& ent : fs::directory_iterator(dir)) {
		const auto name = ent.path().filename().string();
		if (name.find("scr5-dispOff-hmmv-noCpu-") != 0) continue;
		const Variant var = parseVariant(name);
		auto items = parseFile(ent.path(), 0, 'W');
		// keep only W
		items.erase(std::remove_if(items.begin(), items.end(),
		                           [](const Access& a) { return a.type != 'W'; }),
		            items.end());
		for (size_t i = 0; i + 1 < items.size(); ++i) {
			Pair p;
			p.t0 = items[i].time;
			p.t1 = items[i + 1].time;
			p.kind = isLineBreak(items[i].addr, items[i + 1].addr, var)
			         ? TransKind::WdToWdBreak
			         : TransKind::WdToWdMid;
			pairs.push_back(p);
		}
	}
	return pairs;
}

// --- Read/Write commands: HMMM (R.s), YMMM (R.s), LMMV (R.d) -------------

struct RwStats {
	int files = 0;
	int accesses = 0;
	int nR = 0;
	int nW = 0;
	int nonAlt = 0;
	int startR = 0;
	int startW = 0;
};

bool isReadType(char t) { return t == 'R' || t == 'D'; }

std::pair<std::vector<Pair>, RwStats> collectRwPairs(
	const fs::path& dir, const std::string& cmdPrefix, char wantRead)
{
	std::vector<Pair> pairs;
	RwStats st;
	const std::string needle = "scr5-dispOff-" + cmdPrefix + "-noCpu-";
	for (const auto& ent : fs::directory_iterator(dir)) {
		const auto name = ent.path().filename().string();
		if (name.find(needle) != 0) continue;
		++st.files;
		const Variant var = parseVariant(name);
		auto items = parseFile(ent.path(), wantRead, 'W');
		items.erase(std::remove_if(items.begin(), items.end(),
		                           [](const Access& a) {
			                           return !isReadType(a.type) && a.type != 'W';
		                           }),
		            items.end());
		st.accesses += int(items.size());
		if (!items.empty()) {
			if (isReadType(items.front().type)) ++st.startR;
			else ++st.startW;
		}
		for (const auto& a : items) {
			if (isReadType(a.type)) ++st.nR;
			else ++st.nW;
		}
		for (size_t i = 0; i + 1 < items.size(); ++i) {
			const auto& a = items[i];
			const auto& b = items[i + 1];
			const bool aR = isReadType(a.type);
			const bool bR = isReadType(b.type);
			if (!((aR && b.type == 'W') || (a.type == 'W' && bR))) {
				++st.nonAlt;
				continue;
			}
			Pair p;
			p.t0 = a.time;
			p.t1 = b.time;
			if (aR && b.type == 'W') {
				p.kind = TransKind::RsToWd;
			} else {
				int prevRAddr = -1;
				for (int j = int(i) - 1; j >= 0; --j) {
					if (isReadType(items[j].type)) {
						prevRAddr = items[j].addr;
						break;
					}
				}
				const bool br = (prevRAddr >= 0) &&
				                isLineBreak(prevRAddr, b.addr, var);
				p.kind = br ? TransKind::WdToRsBreak : TransKind::WdToRsMid;
			}
			pairs.push_back(p);
		}
	}
	return {pairs, st};
}

int waitFor(const Pair& p, int /*P*/, int Pr, int Pw, int L)
{
	switch (p.kind) {
	case TransKind::RsToWd:      return Pw;
	case TransKind::WdToRsMid:   return Pr;
	case TransKind::WdToRsBreak: return Pr + L;
	case TransKind::WdToWdMid:   return Pr; // reuse Pr as P for HMMV
	case TransKind::WdToWdBreak: return Pr + L;
	}
	return 0;
}

struct Score {
	int exact = 0;
	int total = 0;
	long long errSum = 0;
	int rs2wd = 0, rs2wdN = 0;
	int wd2rs = 0, wd2rsN = 0;
	int br = 0, brN = 0;
};

Score eval(const std::vector<Pair>& pairs, const SlotTable& tab,
           int P, int Pr, int Pw, int L, bool rwCmd)
{
	Score s;
	s.total = int(pairs.size());
	for (const auto& p : pairs) {
		const int wait = rwCmd ? waitFor(p, 0, Pr, Pw, L)
		                       : (p.kind == TransKind::WdToWdBreak ? P + L : P);
		const int pred = nextAfter(p.t0, wait, tab);
		const int e = std::abs(pred - p.t1);
		s.errSum += e;
		const bool ok = (e == 0);
		if (ok) ++s.exact;
		if (rwCmd) {
			if (p.kind == TransKind::RsToWd) {
				++s.rs2wdN;
				if (ok) ++s.rs2wd;
			} else if (p.kind == TransKind::WdToRsMid) {
				++s.wd2rsN;
				if (ok) ++s.wd2rs;
			} else if (p.kind == TransKind::WdToRsBreak) {
				++s.brN;
				if (ok) ++s.br;
			}
		} else {
			if (p.kind == TransKind::WdToWdMid) {
				++s.wd2rsN;
				if (ok) ++s.wd2rs;
			} else {
				++s.brN;
				if (ok) ++s.br;
			}
		}
	}
	return s;
}

void printMidFailPatterns(const std::vector<Pair>& pairs, const SlotTable& tab,
                          int Pr, int Pw, int L)
{
	std::map<std::tuple<int, int, int, int>, int> midFails; // pred-obs, m0, m1, delta
	for (const auto& p : pairs) {
		if (p.kind != TransKind::WdToRsMid) continue;
		const int pred = nextAfter(p.t0, Pr, tab);
		if (pred == p.t1) continue;
		const int m0 = p.t0 % LINE;
		const int m1 = p.t1 % LINE;
		++midFails[{pred - p.t1, m0, m1, p.t1 - p.t0}];
	}
	if (midFails.empty()) {
		std::cout << "  mid residuals: none\n";
		return;
	}
	std::cout << "  mid residuals at Pr=" << Pr << " Pw=" << Pw << " L=" << L << ":\n";
	std::vector<std::pair<std::tuple<int, int, int, int>, int>> v(midFails.begin(), midFails.end());
	std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
	for (size_t i = 0; i < std::min<size_t>(6, v.size()); ++i) {
		auto [e, m0, m1, d] = v[i].first;
		std::cout << "    err=" << e << " " << m0 << "->" << m1
		          << " delta=" << d << " count=" << v[i].second << '\n';
	}
}

void analyzeRw(const fs::path& dir, const std::string& cmd, char wantRead,
               std::vector<std::array<int, 3>> refs)
{
	auto [pairs, st] = collectRwPairs(dir, cmd, wantRead);
	const char* rName = (wantRead == 'D') ? "R.d" : "R.s";
	std::cout << cmd << " files=" << st.files
	          << " accesses=" << st.accesses
	          << " " << rName << "=" << st.nR << " W.d=" << st.nW
	          << " nonAlternatingPairs=" << st.nonAlt
	          << " startR=" << st.startR << " startW=" << st.startW << '\n';
	std::cout << cmd << " transition pairs=" << pairs.size() << '\n';

	int nRs2Wd = 0, nWd2Rs = 0, nBr = 0;
	for (const auto& p : pairs) {
		if (p.kind == TransKind::RsToWd) ++nRs2Wd;
		else if (p.kind == TransKind::WdToRsMid) ++nWd2Rs;
		else if (p.kind == TransKind::WdToRsBreak) ++nBr;
	}
	std::cout << "  " << rName << "->W.d=" << nRs2Wd
	          << " W.d->" << rName << " mid=" << nWd2Rs
	          << " W.d->" << rName << " break=" << nBr << '\n';

	std::vector<Access> allAcc;
	const std::string needle = "scr5-dispOff-" + cmd + "-noCpu-";
	for (const auto& ent : fs::directory_iterator(dir)) {
		const auto name = ent.path().filename().string();
		if (name.find(needle) != 0) continue;
		auto items = parseFile(ent.path(), wantRead, 'W');
		allAcc.insert(allAcc.end(), items.begin(), items.end());
	}

	for (SlotKind kind : {SlotKind::OpenMSX, SlotKind::Hardware}) {
		auto tab = makeSlotTable(kind);
		int on = 0, off = 0;
		std::vector<int> offMods;
		std::array<bool, LINE> seenOff{};
		for (const auto& a : allAcc) {
			const int m = a.time % LINE;
			if (tab.isSlot[m]) ++on;
			else {
				++off;
				if (!seenOff[m]) {
					seenOff[m] = true;
					offMods.push_back(m);
				}
			}
		}
		std::sort(offMods.begin(), offMods.end());

		std::cout << "\n=== " << cmd << " slots: "
		          << (kind == SlotKind::OpenMSX ? "openMSX (1324/1334)" : "hardware (1325/1335)")
		          << " ===\n";
		std::cout << "Accesses on-slot " << on << '/' << (on + off);
		if (!offMods.empty()) {
			std::cout << " off-mods:";
			for (int m : offMods) std::cout << ' ' << m;
		}
		std::cout << '\n';

		int bestExact = -1;
		long long bestErr = 0;
		std::vector<std::array<int, 4>> plateau; // Pr,Pw,L,Pr+L
		Score best{};

		for (int Pr = 16; Pr <= 100; ++Pr) {
			for (int Pw = 16; Pw <= 100; ++Pw) {
				for (int L = 0; L <= 100; ++L) {
					Score s = eval(pairs, tab, 0, Pr, Pw, L, true);
					if (s.exact > bestExact ||
					    (s.exact == bestExact && s.errSum < bestErr)) {
						bestExact = s.exact;
						bestErr = s.errSum;
						best = s;
						plateau.clear();
						plateau.push_back({Pr, Pw, L, Pr + L});
					} else if (s.exact == bestExact && s.errSum == bestErr) {
						plateau.push_back({Pr, Pw, L, Pr + L});
					}
				}
			}
		}

		std::cout << "Best exact " << bestExact << '/' << pairs.size()
		          << " errSum=" << bestErr << '\n';
		std::cout << "  " << rName << "->W.d " << best.rs2wd << '/' << best.rs2wdN
		          << "  W->R mid " << best.wd2rs << '/' << best.wd2rsN
		          << "  break " << best.br << '/' << best.brN << '\n';

		int prMin = 999, prMax = 0, pwMin = 999, pwMax = 0;
		int lMin = 999, lMax = 0, plMin = 999, plMax = 0;
		for (auto [Pr, Pw, L, PL] : plateau) {
			prMin = std::min(prMin, Pr); prMax = std::max(prMax, Pr);
			pwMin = std::min(pwMin, Pw); pwMax = std::max(pwMax, Pw);
			lMin = std::min(lMin, L); lMax = std::max(lMax, L);
			plMin = std::min(plMin, PL); plMax = std::max(plMax, PL);
		}
		std::cout << "Plateau n=" << plateau.size()
		          << " Pr=[" << prMin << ',' << prMax << ']'
		          << " Pw=[" << pwMin << ',' << pwMax << ']'
		          << " L=[" << lMin << ',' << lMax << ']'
		          << " Pr+L=[" << plMin << ',' << plMax << "]\n";

		std::vector<int> pws, prs, ls;
		for (auto [Pr, Pw, L, PL] : plateau) {
			(void)PL;
			pws.push_back(Pw); prs.push_back(Pr); ls.push_back(L);
		}
		std::sort(pws.begin(), pws.end()); pws.erase(std::unique(pws.begin(), pws.end()), pws.end());
		std::sort(prs.begin(), prs.end()); prs.erase(std::unique(prs.begin(), prs.end()), prs.end());
		std::sort(ls.begin(), ls.end()); ls.erase(std::unique(ls.begin(), ls.end()), ls.end());
		std::cout << "  unique Pw:"; for (int x : pws) std::cout << ' ' << x; std::cout << '\n';
		std::cout << "  unique Pr:"; for (int x : prs) std::cout << ' ' << x; std::cout << '\n';
		std::cout << "  unique L :"; for (int x : ls) std::cout << ' ' << x; std::cout << '\n';

		for (auto [Pr, Pw, L] : refs) {
			Score s = eval(pairs, tab, 0, Pr, Pw, L, true);
			std::cout << "  Pr=" << Pr << " Pw=" << Pw << " L=" << L
			          << " exact=" << s.exact << '/' << s.total
			          << " errSum=" << s.errSum
			          << " [R->W " << s.rs2wd << '/' << s.rs2wdN
			          << " mid " << s.wd2rs << '/' << s.wd2rsN
			          << " br " << s.br << '/' << s.brN << "]\n";
		}
		const int bPr = plateau[0][0], bPw = plateau[0][1], bL = plateau[0][2];
		printMidFailPatterns(pairs, tab, bPr, bPw, bL);
		if (!(bPr == refs[0][0] && bPw == refs[0][1] && bL == refs[0][2])) {
			printMidFailPatterns(pairs, tab, refs[0][0], refs[0][1], refs[0][2]);
		}

		// Marginal Pw / Pr slices at best companion values
		std::cout << "  Pw slice (Pr=" << bPr << " L=" << bL << "):";
		for (int Pw = std::max(16, bPw - 8); Pw <= bPw + 8; ++Pw) {
			Score s = eval(pairs, tab, 0, bPr, Pw, bL, true);
			if (s.rs2wd == s.rs2wdN || std::abs(Pw - bPw) <= 4)
				std::cout << " Pw" << Pw << "=" << s.rs2wd << '/' << s.rs2wdN;
		}
		std::cout << '\n';
		std::cout << "  Pr slice (Pw=" << bPw << ", best L for each):";
		for (int Pr = std::max(16, bPr - 8); Pr <= bPr + 8; ++Pr) {
			int bestL = bL, bestM = -1;
			for (int L = 0; L <= 100; ++L) {
				Score s = eval(pairs, tab, 0, Pr, bPw, L, true);
				if (s.wd2rs + s.br > bestM) {
					bestM = s.wd2rs + s.br;
					bestL = L;
				}
			}
			Score s = eval(pairs, tab, 0, Pr, bPw, bestL, true);
			std::cout << " Pr" << Pr << "=" << s.wd2rs << '/' << s.wd2rsN
			          << "+br" << s.br << '/' << s.brN;
		}
		std::cout << '\n';
	}
}

void analyzeHmmv(const fs::path& dir)
{
	auto pairs = collectHmmvPairs(dir);
	std::cout << "HMMV pairs: " << pairs.size() << '\n';

	for (SlotKind kind : {SlotKind::OpenMSX, SlotKind::Hardware}) {
		auto tab = makeSlotTable(kind);
		std::cout << "\n=== HMMV slots: "
		          << (kind == SlotKind::OpenMSX ? "openMSX (1324/1334)" : "hardware (1325/1335)")
		          << " ===\n";

		int bestExact = -1;
		std::vector<std::array<int, 3>> plateau; // P,L,P+L
		Score best{};
		for (int P = 20; P <= 100; ++P) {
			for (int L = 0; L <= 100; ++L) {
				Score s = eval(pairs, tab, P, 0, 0, L, false);
				if (s.exact > bestExact) {
					bestExact = s.exact;
					best = s;
					plateau.clear();
					plateau.push_back({P, L, P + L});
				} else if (s.exact == bestExact) {
					plateau.push_back({P, L, P + L});
				}
			}
		}
		std::cout << "Best exact " << bestExact << '/' << pairs.size()
		          << " errSum=" << best.errSum
		          << " mid=" << best.wd2rs << '/' << best.wd2rsN
		          << " break=" << best.br << '/' << best.brN << '\n';
		int pMin = 999, pMax = 0, lMin = 999, lMax = 0, plMin = 999, plMax = 0;
		for (auto [P, L, PL] : plateau) {
			pMin = std::min(pMin, P); pMax = std::max(pMax, P);
			lMin = std::min(lMin, L); lMax = std::max(lMax, L);
			plMin = std::min(plMin, PL); plMax = std::max(plMax, PL);
		}
		std::cout << "Plateau n=" << plateau.size()
		          << " P=[" << pMin << ',' << pMax << ']'
		          << " L=[" << lMin << ',' << lMax << ']'
		          << " P+L=[" << plMin << ',' << plMax << "]\n";
		for (int P : {45, 48}) {
			for (int L : {53, 56}) {
				Score s = eval(pairs, tab, P, 0, 0, L, false);
				std::cout << "  P=" << P << " L=" << L << " exact=" << s.exact
				          << '/' << s.total << " errSum=" << s.errSum << '\n';
			}
		}
	}
}

int main(int argc, char** argv)
{
	fs::path dir = ".";
	std::string cmd = "hmmm";
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		if (a == "hmmv" || a == "hmmm" || a == "ymmm" || a == "lmmv") cmd = a;
		else dir = a;
	}

	if (cmd == "hmmv") {
		analyzeHmmv(dir);
	} else if (cmd == "hmmm") {
		analyzeRw(dir, "hmmm", 'R', {
			{64, 24, 64}, {60, 24, 68}, {64, 24, 56},
		});
	} else if (cmd == "ymmm") {
		analyzeRw(dir, "ymmm", 'R', {
			{36, 24, 68}, {40, 24, 68}, {36, 24, 64}, {32, 24, 68},
		});
	} else if (cmd == "lmmv") {
		analyzeRw(dir, "lmmv", 'D', {
			{72, 24, 64}, {64, 24, 64}, {72, 24, 56}, {68, 24, 64},
		});
	}
	return 0;
}
