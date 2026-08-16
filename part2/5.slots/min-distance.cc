#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct Item {
	int time;
	std::string type;
	std::string aStr;
};

std::vector<Item> parse(const std::string& filename)
{
	std::vector<Item> items;

	std::ifstream in(filename);
	if (!in) assert(false);

	std::string line;
	while (std::getline(in, line)) {
		assert(line[4] == ':');
		auto t = strtol(line.c_str(), nullptr, 10);
		assert(0 <= t);
		assert(0 < 1368);

		std::string_view columns = std::string_view(line).substr(5);
		int col = 0;
		while (!columns.empty()) {
			auto cell = columns.substr(2, 11);
			auto type = cell.substr(0, 3);
			auto aStr = cell.substr(6, 5);
			if (type[0] != ' ') {
				int time = 1368 * col + t;
				auto s = std::string(type);
				auto a = std::string(aStr);
				items.push_back(Item{time, s, a});
			}
			columns = columns.substr(13);
			++col;
		}
	}

	std::ranges::sort(items, {}, &Item::time);
	return items;
}

void detectNewLine(std::span<Item> items, std::string_view file)
{
	bool prevN = false;
	for (auto& i : items) {
		if (i.type != "R.s") continue;
		if (!prevN && (i.aStr.ends_with("00") || i.aStr.ends_with("80"))) {
			i.type += 'N';
			prevN = true;
			std::cout << file << ' ' << (i.time / 1368) << ',' << (i.time % 1368) << ' ' << i.type << '\n';
		} else {
			prevN = false;
		}
	}
}

using Key = std::pair<std::string /*from*/, std::string /*to*/>;
using Deltas = std::map<Key, std::set<int /*delta*/>>;

void delta(std::span<const Item> items, Deltas& deltas)
{
	assert(!items.empty());
	auto it = items.begin();
	auto et = items.end();

	auto prev = *it;
	for (++it; it != et; ++it) {
		auto& curr = *it;
		auto delta = curr.time - prev.time;
		deltas[Key{prev.type, curr.type}].insert(delta);
		prev = curr;
	}
}

int main(int argc, char** argv)
{
	Deltas deltas;

	for (int i = 1; i < argc; ++i) {
		auto items = parse(argv[i]);
		detectNewLine(items, argv[i]);
		/*for (const auto& i : items) {
			std::cout << i.time << ' ' << i.type << '\n';
		}*/
		delta(items, deltas);
	}

	for (const auto& [key, dd] : deltas) {
		std::cout << key.first << " -> " << key.second << " : ";
		for (const auto& d : dd) {
			std::cout << ' ' << d;
		}
		std::cout << '\n';
	}
}
