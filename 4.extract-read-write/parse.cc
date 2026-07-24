#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <set>
#include <map>
#include <cassert>
#include <cstdlib>

using namespace std;

struct Access {
	int time;
	int addr;
	bool rw;
	bool burst;
	bool vds;
};

vector<Access> parse(const string& filename, vector<int>& times)
{
	ifstream in(filename);
	vector<Access> result;
	while (true) {
		string line;
		getline(in, line);
		if (!in.good()) break;
		//cout << line << endl;
		const char* d = line.data();
		if (d[0] == '#') {
			int t = atoi(d + 1);
			times.push_back(t);
			continue;
		}
		Access a;
		a.time = atoi(d + 5);
		if (a.time == 0) continue;
		a.addr = strtol(d + 18, nullptr, 0);
		a.rw = d[11] == 'r';
		a.burst = (line.size() > 26) && (d[26] == 'b');
		a.vds   = (line.size() > 32) && (d[32] == 'v');
		result.push_back(a);
	}
	return result;
}

int main(int argc, char** argv)
{
	vector<int> times;
	auto log = parse(argv[1], times);
	/*for (int t : times) {
		cout << t << endl;
	}*/
	/*for (auto& a : log) {
		cout << "t=" << a.time
		     << " a=" << a.addr
		     << (a.rw ? " read " : " write")
		     << (a.burst ? " burst" : "      ")
		     << (a.vds ? " vds" : "    ")
		     << "\n";
	}*/
	/*set<int> s1;
	for (auto& a : log) {
		if (a.rw && a.addr == 0x1ffff && !a.vds) {
			cout << a.time << endl;
			s1.insert(a.time);
		}
	}
	cout << "--" << endl;
	vector<int> v2;
	for (int t : s1) {
		if ((s1.find(t + 6370) != s1.end()) ||
		    (s1.find(t + 6368) != s1.end())) {
			cout << t << endl;
			v2.push_back(t);
		}
	}
	cout << "--" << endl;
	int p = 0;
	int b = 9999;
	for (int i = 0; i < (v2.size() - 1); ++i) {
		int d = v2[i + 1] - v2[i];
		cout << v2[i] << " " << d << endl;
		if (d < (b - 2)) {
			b = d;
			p = i;
		}
	}
	cout << "--" << endl;
	while (p < v2.size()) {
		cout << v2[p] << endl;
		int c = v2[p];
		do {
			++p;
		} while ((p != v2.size()) &&
		         ((v2[p] - c) != 6370) &&
		         ((v2[p] - c) != 6368));
	}*/

	times.insert(times.begin(), times[0] - 6370);
	times.push_back(times.back() + 6370);
	for (int i = 0; i < (times.size() - 1); ++i) {
		int d = times[i + 1] - times[i];
		assert((d == 6370) || (d == 6368));
	}
	map<int, map<int, Access>> tab;
	int p = 0;
	for (auto& a : log) {
		int t = a.time;
		while (t >= times[p + 1]) {
			++p;
			//cout << "---\n";
		}
		int t2 = ((a.time - times[p]) * 1368 + (6370 / 2)) / 6370;
		if ((t2 & 1) == 0) {
			cout << "!! " << t2 << " !!\n";
		}
		/*cout << dec << setfill(' ') << setw(4) << t2 << ": "
		     << (a.rw ? "R" : "W")
		     << (a.burst ? "b" : ".")
		     << (a.vds ? "v" : ".")
		     << " 0x" << hex << setfill('0') << setw(5) << a.addr
		     << "\n";*/
		tab[t2][p] = a;
	}

	for (auto& p1 : tab) {
		int t = p1.first;
		auto& m = p1.second;
		cout << dec << setfill(' ') << setw(4) << t << ":";
		for (int i = 0; i <= p; ++i) {
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
