#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <map>
#include <string>
#include <vector>
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

map<int, Access> linearize(map<int, map<int, Access>>& tab)
{
	map<int, Access> result;
	for (auto& p : tab) {
		int t = p.first;
		for (auto& q : p.second) {
			int c = q.first;
			auto& a = q.second;
			result[1368 * c + t] = a;
		}
	}
	return result;
}

map<int, map<int, string>> toTable(const map<int, string>& in, int columns)
{
	map<int, map<int, string>> result;
	int c;
	for (auto& p : in) {
		int t = p.first;
		int t1 = t % 1368;
		c = t / 1368;
		result[t1][c] = p.second;
	}
	columns = c + 1;
	return result;
}

void print(map<int, map<int, string>>& tab, int columns)
{
	auto it = tab.end();
	int prev = -1;
	if (it != tab.begin()) {
		--it;
		prev = it->first - 1368;
	}
	for (auto& p1 : tab) {
		int t = p1.first;
		int d = t - prev;
		prev = t;
		auto& m = p1.second;
		cout << dec << setfill(' ') << setw(4) << t
		     << "(" << setw(2) << d << "):";
		for (int c = 0; c < columns; ++c) {
			auto it = m.find(c);
			if (it != m.end()) {
				cout << it->second;
			} else {
				cout << "            ";
			}
		}
		cout << "\n";
	}
}

int t_screenoff[154 + 1] = {
	   1,    9,   17,   25,   33,   41,   49,   57,   65,   73,
	  81,   89,   97,  105,  113,  121,  165,  173,  181,  189,
	 197,  205,  213,  221,  229,  237,  245,  253,  261,  269,
	 277,  293,  301,  309,  317,  325,  333,  341,  349,  357,
	 365,  373,  381,  389,  397,  405,  421,  429,  437,  445,
	 453,  461,  469,  477,  485,  493,  501,  509,  517,  525,
	 533,  549,  557,  565,  573,  581,  589,  597,  605,  613,
	 621,  629,  637,  645,  653,  661,  677,  685,  693,  701,
	 709,  717,  725,  733,  741,  749,  757,  765,  773,  781,
	 789,  805,  813,  821,  829,  837,  845,  853,  861,  869,
	 877,  885,  893,  901,  909,  917,  933,  941,  949,  957,
	 965,  973,  981,  989,  997, 1005, 1013, 1021, 1029, 1037,
	1045, 1061, 1069, 1077, 1085, 1093, 1101, 1109, 1117, 1125,
	1133, 1141, 1149, 1157, 1165, 1173, 1189, 1197, 1205, 1213,
	1221, 1229, 1269, 1277, 1285, 1293, 1301, 1309, 1317, 1325,
	1335, 1345, 1353, 1361,
	1 + 1368,
};
int t_nosprites[88 + 1] = {
	   7,   15,   23,   31,   39,   47,   55,   63,   71,   79,
	  87,   95,  103,  111,  119,  163,  171,  183,  189,  215,
	 221,  247,  253,  279,  311,  317,  343,  349,  375,  381,
	 407,  439,  445,  471,  477,  503,  509,  535,  567,  573,
	 599,  605,  631,  637,  663,  695,  701,  727,  733,  759,
	 765,  791,  823,  829,  855,  861,  887,  893,  919,  951,
	 957,  983,  989, 1015, 1021, 1047, 1079, 1085, 1111, 1117,
	1143, 1149, 1175, 1207, 1213, 1267, 1275, 1283, 1291, 1299,
	1307, 1315, 1323, 1333, 1343, 1351, 1359, 1367,
	7 + 1368
};
int t_sprites[31 + 1] = {
	  29,   93,  163,  171,  189,  221,  253,  317,  349,  381,
	 445,  477,  509,  573,  605,  637,  701,  733,  765,  829,
	 861,  893,  957,  989, 1021, 1085, 1117, 1149, 1213, 1265,
	1331,
	29 + 1368
};

int main(int argc, char** argv)
{
	int adjust1 = 0;
	int adjust2 = 0;
	if (argc >= 4) adjust1 = atoi(argv[3]);
	if (argc >= 5) adjust2 = atoi(argv[4]);


	int columns;
	auto tab = parse(argv[1], columns);
	auto lin = linearize(tab);

	int w = atoi(argv[2]);
	int* slots;
	int num;
	if (w == 0) {
		slots = t_screenoff;
		num = sizeof(t_screenoff) / sizeof(*t_screenoff) - 1;
	} else if (w == 1) {
		slots = t_nosprites;
		num = sizeof(t_nosprites) / sizeof(*t_nosprites) - 1;
	} else {
		assert(w == 2);
		slots = t_sprites;
		num = sizeof(t_sprites) / sizeof(*t_sprites) - 1;
	}
	vector<int> slots2;
	for (int i = 0; i < 5; ++i) {
		for (int j = 0; j < num; ++j) {
			slots2.push_back(1368 * i + slots[j]);
		}
	}

	auto it = lin.begin();
	auto p = lower_bound(slots2.begin(), slots2.end(), it->first);
	assert(it->first <= *p);

	map<int, string> result;
	int prevC = -1;
	int prevE = -1;
	int sC = 0;
	int sE = 0;
	int nC = 0;
	int nE = 0;
	int eC = 0;
	int error = adjust2;
	bool ok = true;
	for (/**/; it != lin.end(); ++it) {
		if ((it->second.type != 'c') && (it->second.type != 'e')) {
			continue;
		}
		int t = it->first;
		while (*p < t) {
			result[*p] = "  --        ";
			++p;
		}

		auto& a = it->second;
		bool isC = it->second.type == 'c';
		int& prev = isC ? prevC : prevE;
		int& sum  = isC ? sC    : sE;
		int& num  = isC ? nC    : nE;
		int delta = (prev == -1) ? -1 : t - prev;
		int expected = 0;
		if (delta != -1) {
			sum += delta;
			num += 1;
			if (isC) {
				if (a.rw == 'R') {
					expected = ((a.addr - 0x14001 + adjust1) % 40) ? 72 : 252;
				} else {
					expected = ((a.addr - 0x16000 + adjust1) % 40) ? 72 : 252;
				}
				eC += expected;
				error += delta - expected;
			}
		}
		prev = t;

		ostringstream os;
		os << "  "
		   << a.rw << a.type
		   << " " << dec << setw(3) << delta
		   << ((isC && (expected == 252)) ? "!" : " ");
		if (isC) {
			bool missed = error >= 72;
			if (missed) {
				error -= 72;
				adjust1 += 1;
			}
			os << setw(2) << error;
			os << (missed ? "*" : " ");
		} else {
			int d2 = t - prevC;
			if ((error + d2) > 72) {
				os << "#" << setw(2) << error + d2;
			} else {
				os << "   ";
			}
		}
		result[t] = os.str();
		if (error < 0) ok = false;
		if (error >= 72) ok = false;

		assert(*p == t);
		++p;
	}

	columns;
	auto tab2 = toTable(result, columns);
	print(tab2, columns);

	cout << endl;
	cout << "average  delta-E = " << double(sE) / nE << endl;
	cout << "average  delta-C = " << double(sC) / nC << endl;
	cout << "expected delta-C = " << double(eC) / nC << endl;
	if (ok) {
		cout << "OK" << endl;
	} else {
		cout << "NOT OK" << endl;
	}
}

