#include <cassert>
#include <iostream>
#include <fstream>
#include <string>
#include <set>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <file1> [file2 ...]\n";
        return 1;
    }

    std::set<int> unique_values;

    for (int i = 1; i < argc; ++i) {
        std::ifstream file(argv[i]);
        if (!file.is_open()) {
            std::cerr << "Warning: Could not open file " << argv[i] << "\n";
            continue;
        }

        std::string line;
        while (std::getline(file, line)) {
            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                try {
                    // std::stoi automatically skips leading whitespace
                    int value = std::stoi(line.substr(0, colon_pos));
                    unique_values.insert(value);
                } catch (...) {
                    std::cerr << "Error in: " << argv[i] << "\n  " << line << '\n';
                    assert(false);
                }
            }
        }
    }

    // Output the sorted union
    for (int val : unique_values) {
        std::cout << val << ' ';
    }
    std::cout << '\n';

    return 0;
}
