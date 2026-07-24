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
	bool rw;
	bool burst;
	bool vds;
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
				a.rw    = d[p + 0] == 'R';
				a.burst = d[p + 1] == 'b';
				a.vds   = d[p + 2] == 'v';
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

void print(const map<int, map<int, Access>>& tab, int columns)
{
	for (auto& p1 : tab) {
		int t = p1.first;
		auto& m = p1.second;
		cout << dec << setfill(' ') << setw(4) << t << ":";
		for (int i = 0; i < columns; ++i) {
			auto it = m.find(i);
			if (it != m.end()) {
				const Access& a = it->second;
				cout << "  "
				     << (a.rw ? "R" : "W")
				     << (a.burst ? "b" : ".")
				     << (a.vds ? "v" : ".")
				     << " 0x" << hex << setfill('0') << setw(5) << a.addr;
			} else {
				cout << "             ";
			}
		}
		cout << "\n";
	}
}

map<int, map<int, Access>> rotate(const map<int, map<int, Access>>& tab,
                                  int offset, int& newColumns)
{
	map<int, map<int, Access>> result;
	int minC = 999;
	int maxC = 0;
	for (auto& p : tab) {
		int time = p.first;
		for (auto& q : p.second) {
			int c = q.first;
			auto& a = q.second;

			int nt = time + offset;
			while (nt < 0) {
				nt += 1368;
				c -= 1;
			}
			while (nt >= 1368) {
				nt -= 1368;
				c += 1;
			}
			minC = min(minC, c);
			maxC = max(maxC, c);
			result[nt][c] = a;
		}
	}
	if (minC != 0) {
		map<int, map<int, Access>> tmp;
		swap(tmp, result);
		for (auto& p : tmp) {
			int time = p.first;
			for (auto& q : p.second) {
				int c = q.first;
				auto& a = q.second;
				result[time][c - minC] = a;
			}
		}
		maxC -= minC;
	}
	newColumns = maxC + 1;
	return result;
}

int main(int argc, char** argv)
{
	int columns;
	auto tab = parse(argv[1], columns);
	int offset = atoi(argv[2]);
	tab = rotate(tab, offset, columns);
	print(tab, columns);
	cout << columns << endl;

	/*
	ifstream in(argv[1]);
	while (true) {
		string line;
		getline(in, line);
		if (!in.good()) break;

		auto pos = line.find(' ');
		string f = line.substr(0, pos);
		int o = atoi(line.substr(pos + 1).c_str());
		//cout << f << "\t" << o << endl;
	}
	*/

}
