#include <iostream>
#include <fstream>
#include <map>
#include <string>

using namespace std;

int main(int argc, char** argv)
{
	map<int, map<int, string>> m;
	for (int i = 1; i < argc; ++i) {
		ifstream in(argv[i]);
		while (true) {
			string line;
			getline(in, line);
			if (!in.good()) break;
			auto pos = line.find(' ');
			int t = atoi(line.data());
			m[t][i] = line.substr(pos + 1);
		}
	}

	for (auto& p : m) {
		cout << p.first << "\t";
		auto& q = p.second;
		for (int i = 1; i < argc; ++i) {
			auto it = q.find(i);
			if (it != q.end()) {
				cout << it->second;
			}
			cout << "\t";
		}
		cout << "\n";
	}
}
