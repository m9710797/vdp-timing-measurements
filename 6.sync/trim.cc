#include <iostream>
#include <string>

using namespace std;

int main()
{
	while (true) {
		string line;
		getline(cin, line);
		if (!cin.good()) break;
		auto pos = line.find_last_not_of(' ');
		line.resize(pos + 1);
		cout << line << endl;
	}
}
