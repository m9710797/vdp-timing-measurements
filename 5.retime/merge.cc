#include <iostream>
#include <iomanip>
#include <fstream>
#include <map>
#include <string>
#include <vector>
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

void print(map<int, map<int, Access>>& tab, const vector<pair<string, int>>& files)
{
	// heading
	cout << "       ";
	for (auto& p : files) {
		string f = p.first;
		int c = 13 * p.second;
		f.resize(c, ' ');
		cout << f << "       ";
	}
	cout << "\n";
	// body
	for (auto& p1 : tab) {
		int t = p1.first;
		auto& m = p1.second;
		int c = 0;
		for (auto& p : files) {
			cout << dec << setfill(' ') << setw(4) << t << ":";
			for (int i = 0; i < p.second; ++i) {
				auto it = m.find(i + c);
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
			c += p.second;
			cout << "  ";
		}
		cout << "\n";
	}
}

void merge(map<int, map<int, Access>>& total, int& totalColumns,
           const map<int, map<int, Access>>& sub, int subColumns)
{
	for (auto& p : sub) {
		int time = p.first;
		auto& row = p.second;
		for (int i = 0; i < subColumns; ++i) {
			auto it = row.find(i);
			if (it != row.end()) {
				total[time][totalColumns + i] = it->second;
			}
		}
	}
	totalColumns += subColumns;
}

int main(int argc, char** argv)
{
	map<int, map<int, Access>> total;
	vector<pair<string, int>> files;
	int totalColumns = 0;
	for (int i = 1; i < argc; ++i) {
		int columns;
		auto tab = parse(argv[i], columns);
		files.push_back(make_pair(argv[i], columns));
		merge(total, totalColumns, tab, columns);
	}
	print(total, files);
}
