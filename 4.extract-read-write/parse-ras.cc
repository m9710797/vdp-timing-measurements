#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <set>
#include <map>
#include <string>
#include <cassert>
#include <cstdlib>

using namespace std;

int main(int argc, char** argv)
{
	ifstream in(argv[1]);

	int offset = 0;
	vector<int> times;
	map<int, string> log;
	while (true) {
		string line;
		getline(in, line);
		if (!in.good()) break;

		if (line[0] == '%') {
			offset = atoi(line.c_str() + 1);
			offset *= 6370;
			offset /= 1368;
			continue;
		}
		if (line[0] == '#') {
			times.push_back(atoi(line.c_str() + 1));
			continue;
		}
		int t = atoi(line.c_str()) + offset;
		auto p = line.find(':');
		log[t] = line.substr(p + 1);
	}

	times.insert(times.begin(), times[0] - 6370);
	times.push_back(times.back() + 6370);
	for (int i = 0; i < (times.size() - 1); ++i) {
		int d = times[i + 1] - times[i];
		assert((d == 6370) || (d == 6368));
	}

	map<int, map<int, string>> tab;
	int p = 0;
	for (auto& q : log) {
		int t = q.first;
		string s = q.second;
		if (s.empty()) s = "  *";
		s.resize(13, ' ');

		while (t >= times[p + 1]) {
			++p;
		}
		int t2 = ((t - times[p]) * 1368 + (6370 / 2)) / 6370;
		tab[t2][p] = s;
	}

	for (auto& p1 : tab) {
		int t = p1.first;
		auto& m = p1.second;
		cout << dec << setfill(' ') << setw(4) << t << ":";
		for (int i = 0; i <= p; ++i) {
			auto it = m.find(i);
			if (it != m.end()) {
				cout << it->second;
			} else {
				cout << "             ";
			}
		}
		cout << "\n";
	}
}
