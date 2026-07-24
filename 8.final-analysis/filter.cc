#include <iostream>
#include <iomanip>
#include <fstream>
#include <map>
#include <set>
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

void filter(const map<int, map<int, Access>>& tab, set<int>& result)
{
	for (auto& p : tab) {
		int t = p.first;
		for (auto& q : p.second) {
			auto& a = q.second;
			if ((a.type == 'c') || (a.type == 'e')) {
				result.insert(t);
			}
		}
	}
}

int main(int argc, char** argv)
{
	set<int> all;
	for (int i = 1; i < argc; ++i) {
		int columns;
		auto tab = parse(argv[i], columns);
		filter(tab, all);
	}
	int prev = -1;
	for (auto t : all) { 
		cout << t << " " << (t % 8);
		if (prev != -1) {
			cout << " " << (t - prev);
		}
		cout << endl;
		prev = t;
	}
}


/*{
}*/
