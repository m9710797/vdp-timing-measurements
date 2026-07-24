#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <map>
#include <cassert>
#include <cstdlib>

using namespace std;

struct Access {
	int addr;
	char rw;
	char burst;
	char type;
};

map<int, map<int, Access>> parse(const string& filename, int& columns)
{
	ifstream in(filename);
	
	map<int, map<int, Access>> result;
	columns = 0;
	while (true) {
		string line;
		getline(in, line);
		if (!in.good()) break;

		const char* d = line.data();
		int t = strtol(d, nullptr, 0);
		assert(t >= 0);
		assert(t < 1368);

		int p = 7;
		int q = 0;
		while (p < line.size()) {
			if (d[p] != ' ') {
				Access a;
				a.rw    = d[p + 0];
				a.burst = d[p + 1];
				a.type  = d[p + 2];
				a.addr = strtol(d + p + 4, nullptr, 0);
				result[t][q] = a;
			}
			p += 13;
			q += 1;
		}
		columns = max(columns, q);
	}
	return result;
}

void print(const map<int, map<int, Access>>& tab, int columns, ostream& out)
{
	for (auto& p1 : tab) {
		int t = p1.first;
		auto& m = p1.second;
		out << dec << setfill(' ') << setw(4) << t << ":";
		for (int i = 0; i < columns; ++i) {
			auto it = m.find(i);
			if (it != m.end()) {
				const Access& a = it->second;
				out << "  "
				     << a.rw
				     << a.burst
				     << a.type
				     << " 0x" << hex << setfill('0') << setw(5) << a.addr;
			} else {
				out << "             ";
			}
		}
		out << "\n";
	}
}

void annotate(map<int, map<int, Access>>& tab)
{
	int refresh[8] = { 285, 413, 541, 669, 797, 925, 1053, 1181 };
	int* refresh_end = refresh + 8;

	int sprite[] = {
		// 32x y-coord
		183,  215,  247,  279,  311,  343,  375,  407,
		439,  471,  503,  535,  567,  599,  631,  663,
		695,  727,  759,  791,  823,  855,  887,  919,
		951,  983, 1015, 1047, 1079, 1111, 1143, 1175,
		// other sprite fetches
		1239, 1243, 1247, 1252, 1256, 1260, 1271, 1275, 1281, 1287, 1291, 1297,
		1303, 1307, 1311, 1316, 1320, 1324, 1339, 1343, 1349, 1355, 1359, 1365,
		   3,    7,   11,   16,   20,   24,   35,   39,   45,   51,   55,   61,
		  67,   71,   75,   80,   84,   88,   99,  103,  109,  115,  119,  125,
	};
	int* sprite_end = sprite + (sizeof(sprite) / sizeof(*sprite));

	int preamble[] = {
		// bitmap preamble
		195, 199, 203, 207,
	};
	int* preamble_end = preamble + (sizeof(preamble) / sizeof(*preamble));

	int postamble[] = {
		// sprite postamble
		1207
	};
	int* postamble_end = postamble + (sizeof(postamble) / sizeof(*postamble));

	for (auto& p : tab) {
		int t = p.first;
		for (auto& q : p.second) {
			int c = q.first;
			auto& a = q.second;

			if (find(refresh, refresh_end, t) != refresh_end) {
				assert(a.rw = 'R');
				assert(a.burst = '.');
				assert((a.type == 'r') || (a.type == '.'));
				a.type = 'r';
				continue;
			}

			/*if (find(sprite, sprite_end, t) != sprite_end) {
				assert(a.rw = 'R');
				//assert(a.burst = '.');
				assert((a.type == 's') || (a.type == '.'));
				a.type = 's';
			}*/

			if (find(preamble, preamble_end, t) != preamble_end) {
				assert(a.rw = 'R');
				//assert(a.burst = '.');
				assert((a.type == 'p') || (a.type == '.'));
				a.type = 'p';
			}

			/*if (find(postamble, postamble_end, t) != postamble_end) {
				assert(a.rw = 'R');
				assert(a.burst = '.');
				assert((a.type == 'p') || (a.type == '.'));
				a.type = 'p';
			}*/

			if ((a.rw == 'R') && (0x14000 <= a.addr) && (a.addr < 0x18000)) {
				assert(a.burst == '.');
				assert((a.type == 'c') || (a.type == '.'));
				a.type = 'c';
			}
			if ((a.rw == 'W') && (0x16000 <= a.addr) && (a.addr < 0x1A000)) {
				assert(a.burst == '.');
				assert((a.type == 'c') || (a.type == '.'));
				a.type = 'c';
			}
			/*if ((a.rw == 'R') && (0x18000 <= a.addr) && (a.addr < 0x1ffff)) {
				assert(a.burst == '.');
				assert((a.type == 'e') || (a.type == '.'));
				a.type = 'e';
			}
			if ((a.rw == 'W') && (0x1c000 <= a.addr) && (a.addr < 0x20000)) {
				assert(a.burst == '.');
				assert((a.type == 'e') || (a.type == '.'));
				a.type = 'e';
			}*/
		}
	}
}

int main(int argc, char** argv)
{
	int columns;
	auto tab = parse(argv[1], columns);
	annotate(tab);
	print(tab, columns, cout);
}
