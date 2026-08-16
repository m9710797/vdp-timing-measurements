#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

constexpr std::array<int, 31> EXPECTED_KEYS = {
    29, 93, 163, 171, 189, 221, 253, 317, 349, 381,
    445, 477, 509, 573, 605, 637, 701, 733, 765, 829,
    861, 893, 957, 989, 1021, 1085, 1117, 1149, 1213, 1265, 1331
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <filename>\n";
        return 1;
    }

    std::ifstream file(argv[1]);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << argv[1] << "\n";
        return 1;
    }

    size_t idx = 0;
    std::string line;

    while (std::getline(file, line)) {
        auto colon_pos = line.find(':');
        if (colon_pos == std::string::npos) {
            std::cerr << "Error: Missing ':' in line: " << line << "\n";
            return 1;
        }

        int key = std::stoi(line.substr(0, colon_pos));

        // Output missing lines up until the parsed key
        while (idx < EXPECTED_KEYS.size() && EXPECTED_KEYS[idx] < key) {
            std::cout << std::setw(4) << EXPECTED_KEYS[idx++] << ":\n";
        }

        // Check if key matches current expected index
        if (idx >= EXPECTED_KEYS.size() || EXPECTED_KEYS[idx] != key) {
            std::cerr << "Error: Invalid or out-of-order key '" << key << "'\n";
            return 1;
        }

        std::cout << line << "\n";
        idx++;
    }

    // Emit remaining key placeholders
    while (idx < EXPECTED_KEYS.size()) {
        std::cout << std::setw(4) << EXPECTED_KEYS[idx++] << ":\n";
    }
}
