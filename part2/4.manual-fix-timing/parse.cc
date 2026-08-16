#include <cassert>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <string_view>

void parse(const std::string& filename, std::map<int/*slot*/, int/*count*/>& histogram)
{
	std::ifstream in(filename);
	if (!in) assert(false);

	std::string line;
	while (std::getline(in, line)) {
		assert(line[4] == ':');
		auto t = strtol(line.c_str(), nullptr, 10);
		assert(0 <= t);
		assert(0 < 1368);
		//std::cout << t << ":";

		std::string_view columns = std::string_view(line).substr(5);
		while (!columns.empty()) {
			auto cell = columns.substr(2, 11);
			auto type = cell.substr(0, 3);
			auto aStr = cell.substr(6, 5);
			//std::cout << "  " << type << ' ' << aStr;
			if (type[0] != ' ') {
				++histogram[t];
			}
			columns = columns.substr(13);
		}
		//std::cout << '\n';
	}

}

int main(int argc, char** argv)
{
	std::map<int/*slot*/, int/*count*/> histogram;
	for (int i = 1; i < argc; ++i) {
		parse(argv[i], histogram);
	}
	for (const auto& [slot, count] : histogram) {
		std::cout << std::setfill(' ') << std::setw(4) << slot << ": " << count << '\n';
	}
}
