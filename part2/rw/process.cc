#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

struct Line {
    int time;
    std::string type;
    unsigned addr;
    int refresh = -9;
    int time2 = -9;
};

static std::vector<int> candidate_filter1(const std::vector<Line>& lines) {
    std::vector<int> candidates;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const Line& line = lines[i];
        if (line.type != "R..") {
            continue;
        }
	if ((line.addr & 0x3f) != 0x3f) {
	    continue;
	}
        candidates.push_back(static_cast<int>(i));
    }
    return candidates;
}

static std::vector<int> candidate_filter2b(const std::vector<Line>& lines, const std::vector<int>& candidates, size_t start) {
	std::vector<int> result;
	auto prev = lines[candidates[start]].addr;
	result.push_back(int(candidates[start]));
	for (auto i = start + 1; i < candidates.size(); ++i) {
		auto idx = candidates[i];
		auto next = lines[idx].addr;
        if ((next & 0xf0000) != ((prev & 0xf0000) ^ 0x10000)) continue;
		if ((next & 0x3f) == (prev & 0x3f)) {
			if ((((prev >> 8) + 1) & 0xff) != ((next >> 8) & 0xff)) continue;
		} else {
			if ((((prev >> 8) + 1) & 0xf0) != ((next >> 8) & 0xf0)) continue;
		}
		result.push_back(int(idx));
		prev = next;
	}
	return result;
}

static std::vector<int> candidate_filter2(const std::vector<Line>& lines, const std::vector<int>& candidates) {
    std::vector<int> best;
    for (size_t start = 0; start < candidates.size(); ++start) {
	auto c = candidate_filter2b(lines, candidates, start);
	if (c.size() > best.size()) {
	    best = std::move(c);
	}
    }
    return best;
}

std::vector<int> find_refresh_starts(const std::vector<Line>& lines, const std::vector<int>& candidates)
{
#if 0
    std::vector<int> delta;
    for (size_t i = 1; i < candidates.size(); ++i) {
        delta.push_back(lines[candidates[i]].time - lines[candidates[i - 1]].time);
        std::cout << lines[candidates[i]].time << ' ' << delta.back() << '\n';
    }
#endif

    std::vector<int> starts;
    if (candidates.size() < 8) {
        return starts;
    }

    for (std::size_t i = 1; i + 7 < candidates.size(); ++i) {
        const int previous_gap = lines[candidates[i]].time - lines[candidates[i - 1]].time;

        std::vector<int> small_gaps;
        small_gaps.reserve(7);
        for (std::size_t j = 0; j < 7; ++j) {
            const int start_time = lines[candidates[i + j]].time;
            const int end_time = lines[candidates[i + j + 1]].time;
            small_gaps.push_back(end_time - start_time);
        }

        std::vector<int> sorted_gaps = small_gaps;
        std::sort(sorted_gaps.begin(), sorted_gaps.end());
        const int median_small_gap = sorted_gaps[3];
        const int min_small_gap = sorted_gaps.front();
        const int max_small_gap = sorted_gaps.back();

        //const bool similar_small_gaps = (max_small_gap - min_small_gap) <= std::max(2000, median_small_gap / 10);
        const bool similar_small_gaps = (max_small_gap - min_small_gap) <= std::max(2000, 3 * median_small_gap / 5); // timing anomaly?
        const bool large_gap_before = previous_gap >= std::max(3 * median_small_gap, max_small_gap + 10000);

        if (similar_small_gaps && large_gap_before) {
            starts.push_back(candidates[i]);
        }
    }

    return starts;
}

void fill_refresh(std::vector<Line>& lines, const std::vector<int>& refresh, const std::vector<int>& refresh_starts)
{
    auto time = [](int r) {
        int m = r / 8;
        int o = r % 8;
        if (o < 0) {
            o += 8;
            m -= 1;
        }
        static constexpr std::array<int, 8> t{285, 413, 541, 669, 797, 925, 1053, 1181};
        return 1368 * m + t[o];
    };

    assert(!refresh_starts.empty());
    auto r_it = refresh_starts.begin();
    auto first = *r_it;
    auto it = std::ranges::find(refresh, first);
    assert(it != refresh.end());

    int r0 = (it == refresh.begin()) ? 0 : 8;
    int r = r0;
    for (auto it0 = it; it0 != refresh.begin(); ) {
        --it0;
        --r;
        lines[*it0].refresh = time(r);
    }

    r = r0;
    for (/**/; it != refresh.end(); ++it) {
        lines[*it].refresh = time(r);
        if (r % 8 == 0) {
            if (r_it != refresh_starts.end()) {
                assert(*it == *r_it);
                ++r_it;
            }
        }
        ++r;
    }
}

void interpolate_time(std::vector<Line>& lines, const std::vector<int>& refresh)
{
    assert(refresh.size() >= 2);
    if (refresh[0] != 0) {
        // extrapolate start
        auto idx0 = refresh[0];
        auto idx1 = refresh[1];
        auto t0 = lines[idx0].time;
        auto t1 = lines[idx1].time;
        auto r0 = lines[idx0].refresh; assert(r0 != -9);
        auto r1 = lines[idx1].refresh; assert(r1 != -9);
        assert(t1 > t0);
        assert(r1 > r0);
        auto factor = float(r1 - r0) / float(t1 - t0);
        for (int i = 0; i < idx0; ++i) {
            auto t = lines[i].time;
            assert(t < t0);
            auto t2 = r0 - (t0 - t) * factor;
            lines[i].time2 = int(std::round(t2));
        }
    }
    unsigned r = 0;
    auto idx0 = refresh[r];
    auto t0 = lines[idx0].time;
    auto r0 = lines[idx0].refresh; assert(r0 != -9);
    float factor = 0.0f; // remember last factor for extrapolation
    for (++r; r < refresh.size(); ++r) {
        auto idx1 = refresh[r];
        auto t1 = lines[idx1].time;
        auto r1 = lines[idx1].refresh; assert(r0 != -9);
        assert(t1 > t0);
        assert(r1 > r0);
        factor = float(r1 - r0) / float(t1 - t0);
        for (int i = idx0; i <= idx1; ++i) {
            auto t = lines[i].time;
            assert(t0 <= t); assert(t <= t1);
            auto t2 = (t - t0) * factor + r0;
            assert(r0 <= t2); assert(t2 <= r1);
            lines[i].time2 = int(std::round(t2));
        }
        idx0 = idx1;
        t0 = t1;
        r0 = r1;
    }

    // extrapolate end
    for (size_t i = idx0; i < lines.size(); ++i) {
        auto t = lines[i].time;
        assert(t0 <= t);
        auto t2 = (t - t0) * factor + r0;
        assert(r0 <= t2);
        lines[i].time2 = int(std::round(t2));
    }
}

void annotate_type(std::vector<Line>& lines, const std::vector<int>& refresh)
{
    for (unsigned i = 0; i < lines.size(); ++i) {
        auto& line = lines[i];
        auto time2 = line.time2;
        if (time2 <= 0) continue; // ignore
        time2 %= 1368;
        if (time2 ==  195 || time2 ==  199 || time2 ==  203 || time2 ==  207) continue; // dummy slots
        if (time2 == 1237 || time2 == 1245 || time2 == 1253 || time2 == 1261) continue; // dummy slots
        if (time2 == 1243 || time2 == 1251 || time2 == 1259                 ) continue;

        auto addr = line.addr;
        auto& type = line.type;

        if (std::ranges::find(refresh, i) != refresh.end()) {
            assert((addr & 0x3f) == 0x3f);
            assert(type == "R..");
            type[2] = 'R'; // refresh
        } else if (addr == 0x1ffff) {
            // skip
        } else if (addr < 0x6a00) {
            assert((type == "R.v") || (type == "Rbv"));
            // nametable fetch (already annotated with 'v')
        } else if (0xd400 <= addr && addr < 0xd680) {
            assert((type == "R..") || (type == "Rb."));
            type[2] = 'a'; // sprite attribute
        } else if (0xd800 <= addr && addr < 0xe000) {
            assert((type == "R..") || (type == "Rb."));
            type[2] = 'p'; // sprite pattern
        } else if (0x14000 <= addr && addr < 0x16000) {
            assert(type == "R..");
            type[2] = 'r'; // CPU read
        } else if (0x16000 <= addr && addr < 0x18000) {
            if (type == "R..") {
                //assert(addr < 0x17000); // overflow designated region
                type[2] = 'r'; // CPU read
            } else {
                assert(type == "W..");
                type[2] = 'w'; // CPU write
            }
        } else if (0x18000 <= addr && addr < 0x1c000) {
            if (type == "W..") {
                //assert(addr < 0x19800); // overflow designated region
                type[2] = 'w'; // CPU write
            } else {
                assert(type == "R..");
                type[2] = 's'; // command source
            }
        } else if (0x1c000 <= addr && addr < 0x20000) {
            assert((type == "R..") || (type == "W.."));
            type[2] = 'd'; // command destination
        }
    }
}

static bool parse_line(const std::string& line, Line& out) {
    std::string_view input(line);

    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front()))) {
        input.remove_prefix(1);
    }

    const auto colon = input.find(':');
    if (colon == std::string_view::npos) {
        return false;
    }

    const std::string_view time_text = input.substr(0, colon);
    if (time_text.empty()) {
        return false;
    }

    int time = 0;
    for (char ch : time_text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
        time = time * 10 + (ch - '0');
    }

    input.remove_prefix(colon + 1);
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front()))) {
        input.remove_prefix(1);
    }

    if (input.size() < 3) {
        return false;
    }

    const std::string_view type_text = input.substr(0, 3);
    if (type_text.size() != 3) {
        return false;
    }

    input.remove_prefix(3);
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front()))) {
        input.remove_prefix(1);
    }

    if (input.rfind("0x", 0) != 0) {
        return false;
    }

    input.remove_prefix(2);
    if (input.empty()) {
        return false;
    }

    unsigned addr = 0;
    for (char ch : input) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
        addr = addr * 16 + (std::isdigit(static_cast<unsigned char>(ch)) ? (ch - '0') : (std::tolower(static_cast<unsigned char>(ch)) - 'a' + 10));
    }

    out.time = time;
    out.type = std::string(type_text);
    out.addr = addr;
    return true;
}

[[maybe_unused]] static void print_filtered(const std::vector<Line>& lines, const std::vector<int>& indices)
{
    for (int idx : indices) {
	const Line& entry = lines[idx];
	std::cout << idx << ": " << std::dec << entry.time << ":  " << entry.type << ' ';
	std::cout << "0x" << std::setw(5) << std::setfill('0') << std::hex << std::nouppercase << entry.addr;
	std::cout << '\n';
	std::cout << std::dec;
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <input-file>\n";
        return 1;
    }

    std::ifstream input(argv[1]);
    if (!input) {
        std::cerr << "unable to open input file: " << argv[1] << "\n";
        return 1;
    }

    std::vector<Line> lines;
    std::string line;
    while (std::getline(input, line)) {
        Line parsed;
        if (parse_line(line, parsed)) {
            lines.push_back(parsed);
        }
    }
    assert(lines.size() > 2);
    lines.erase(lines.begin(), lines.begin() + 2);

    auto candidates = candidate_filter1(lines);
    //print_filtered(lines, candidates); return 0;
    auto refresh = candidate_filter2(lines, candidates);
    //print_filtered(lines, refresh); return 0;

    const std::vector<int> refresh_starts = find_refresh_starts(lines, refresh);
    if (refresh_starts.empty()) {
        std::cerr << "failed to find a valid interval marker sequence\n";
        return 2;
    }
#if 0
    for (int idx : refresh) {
        const Line& entry = lines[idx];
        std::cout << idx << ": " << std::dec << entry.time << ":  " << entry.type << ' ';
        std::cout << "0x" << std::setw(5) << std::setfill('0') << std::hex << std::nouppercase << entry.addr;
        if (std::find(refresh_starts.begin(), refresh_starts.end(), idx) != refresh_starts.end()) {
            std::cout << " *";
        }
        std::cout << '\n';
        std::cout << std::dec;
    }
#endif

    fill_refresh(lines, refresh, refresh_starts);
#if 0
    for (const auto& entry : lines) {
        if (entry.time == 0) continue;
        std::cout << std::dec << entry.time << ":  " << entry.type << ' ';
        std::cout << "0x" << std::setw(5) << std::setfill('0') << std::hex << std::nouppercase << entry.addr;
        std::cout << "  " << std::dec << entry.refresh;
        std::cout << "  " << std::dec << entry.time2;
        std::cout << '\n';
        std::cout << std::dec;
    }
#endif

    interpolate_time(lines, refresh);
#if 0
    for (const auto& entry : lines) {
        std::cout << std::dec << entry.time << ":  " << entry.type << ' ';
        std::cout << "0x" << std::setw(5) << std::setfill('0') << std::hex << std::nouppercase << entry.addr;
        std::cout << "  " << std::dec << entry.refresh;
        std::cout << "  " << std::dec << entry.time2;
        std::cout << '\n';
        std::cout << std::dec;
    }
#endif

    annotate_type(lines, refresh);

#if 0
    std::set<int> slots;
    for (const auto& entry : lines) {
        slots.insert(entry.time2 % 1368);
    }
    for (auto s : slots) std::cout << s << ' ';
    std::cout << '\n';
#endif

    std::map<int /*row*/, std::map<int /*column*/, unsigned /*idx*/>> table;
    for (unsigned i = 0; i < lines.size(); ++i) {
        auto t = lines[i].time2;
        auto row = t % 1368;
        auto col = t / 1368;
        table[row][col] = i;
    }
    for (const auto& [row, e] : table) {
        std::cout << std::dec << std::setfill(' ') << std::setw(4) << row << ':';
        assert(!e.empty());
        auto cl = std::prev(e.end())->first;
        for (int c = 0; c <= cl; ++c) {
            if (auto it = e.find(c); it != e.end()) {
                const auto& line = lines[it->second];
                std::cout << "  " << line.type
                          << " 0x" << std::hex << std::setfill('0') << std::setw(5) << line.addr;
            } else {
                std::cout << "             ";
            }
        }
        std::cout << '\n';
    }

    return 0;
}
