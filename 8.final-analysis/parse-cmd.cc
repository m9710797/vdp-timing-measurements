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


int main(int argc, char** argv)
{
	int columns;
	auto tab = parse(argv[1], columns);
	auto m = linearize(tab);

	int prev = m.begin()->first;
	for (auto& [t, a] : m) {
		if (a.type != 'e') continue;
		auto d = t - prev;
		prev = t;
		cout << "t=" << (t%1368);
		cout << ' ' << a.rw;
		cout << ' ' << d;
		cout << '\n';
	}
}
