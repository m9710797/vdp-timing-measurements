#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cassert>
#include <cstdlib>

using namespace std;

struct TokenizerEnd {};

class Tokenizer
{
public:
	Tokenizer(const string& filename);
	string getLine();
	string getWord();
private:
	ifstream in;
	vector<string> words;
};

struct VCDVar
{
	string name;
	int size;
};

class VCD
{
public:
	VCD(const string& filename);

	void setDate(const string& date_) { date = date_; }
	void setVersion(const string& version_) { version = version_; }
	void setTimescale(const string& timescale_) { timescale = timescale_; }
	void setScope(const string& scope_) { scope = scope_; }
	void addVar(const string& name, char id, int size);
	void addChange(int time, char id, int val);

	int getVarSize(char id) const;
	char getFreeId() const;
	char getId(const string& name) const;
	map<pair<int, char>, int> getFilteredEvents(vector<char> ids) const;
	const map<pair<int, char>, int>& getEvents() const { return events; }

	void removeVar(char id);

	void dump();

private:
	string date;
	string version;
	string timescale;
	string scope;
	map<char, VCDVar> vars;
	map<pair<int, char>, int> events;
};

class VCDParser
{
public:
	VCDParser(const string& filename, VCD& vcd);
private:
	void parseVar();
	string getTillEnd();

	Tokenizer tokenizer;
	VCD& vcd;
};



vector<string> split(const string& line)
{
	static const char* const DELIM = " \t";
	vector<string> result;
	auto pos1 = line.find_first_not_of(DELIM);
	while (pos1 != string::npos) {
		auto pos2 = line.find_first_of(DELIM, pos1);
		if (pos2 == string::npos) {
			result.push_back(line.substr(pos1));
			return result;
		}
		result.push_back(line.substr(pos1, pos2 - pos1));
		pos1 = line.find_first_not_of(DELIM, pos2);
	}
	return result;
}

string tobin(unsigned val)
{
	if (val == 0) return "0";

	char buf[32];
	char* p = buf + 32;
	while (val) {
		--p;
		*p = '0' + (val & 1);
		val >>= 1;
	}
	return string(p, buf + 32);
}


Tokenizer::Tokenizer(const string& filename)
	: in(filename)
{
}

string Tokenizer::getLine()
{
	string line;
	getline(in, line);
	if (!in.good()) throw TokenizerEnd();
	return line;
}

string Tokenizer::getWord()
{
	while (words.empty()) {
		words = split(getLine());
	}
	string result = words.front();
	words.erase(words.begin());
	return result;
}



VCDParser::VCDParser(const string& filename, VCD& vcd_)
	: tokenizer(filename)
	, vcd(vcd_)
{
	try {
		while (true) {
			string word = tokenizer.getWord();
			if (word == "$date") {
				vcd.setDate(getTillEnd());
			} else if (word == "$version") {
				vcd.setVersion(getTillEnd());
			} else if (word == "$timescale") {
				vcd.setTimescale(getTillEnd());
			} else if (word == "$scope") {
				vcd.setScope(getTillEnd());
			} else if (word == "$var") {
				parseVar();
			} else if (word == "$upscope") {
				getTillEnd(); // ignore
			} else if (word == "$comment") {
				getTillEnd(); // ignore
			} else if (word == "$enddefinitions") {
				getTillEnd();
				break;
			} else {
				cout << "ERROR " << word << endl;
				assert(false);
			}
		}
		int time = 0;
		while (true) {
			string line = tokenizer.getLine();
			if (line == "$dumpvars") {
				// ignore
			} else if (line == "$end") {
				// ignore
			} else if (line[0] == 'x') {
				// ignore
			} else if (line[0] == '#') {
				int time2 = atoi(line.c_str() + 1);
				assert(time2 >= time);
				time = time2;
			} else if (line[0] == 'b') {
				int val = 0;
				const char* p = line.data() + 1;
				while (*p != ' ') {
					int d = *p++ - '0';
					assert((d == 0) || (d == 1));
					val = 2 * val + d;
				}
				char id = p[1];
				vcd.addChange(time, id, val);
			} else {
				int val = line[0] - '0';
				assert((val == 0) || (val == 1));
				char id = line[1];
				vcd.addChange(time, id, val);
			}

		}
	} catch (TokenizerEnd&) {
		// nothing
	}
}

void VCDParser::parseVar()
{
	string type = tokenizer.getWord();
	int    size = atoi(tokenizer.getWord().c_str());
	string id   = tokenizer.getWord();
	string name = tokenizer.getWord();
	tokenizer.getWord();

	assert(type == "wire");
	assert(id.size() == 1);

	vcd.addVar(name, id[0], size);
}

string VCDParser::getTillEnd()
{
	string result;
	while (true) {
		string word = tokenizer.getWord();
		if (word == "$end") {
			return result;
		}
		if (!result.empty()) result += ' ';
		result += word;
	}
}



VCD::VCD(const string& filename)
{
	VCDParser parser(filename, *this);
}

void VCD::addVar(const string& name, char id, int size)
{
	VCDVar var;
	var.name = name;
	var.size = size;
	vars[id] = var;
}

void VCD::addChange(int time, char id, int val)
{
	events[make_pair(time, id)] = val;
}

int VCD::getVarSize(char id) const
{
	auto it = vars.find(id);
	assert(it != vars.end());
	return it->second.size;
}

char VCD::getFreeId() const
{
	char result = 33;
	for (auto it = vars.begin(); it != vars.end(); ++it, ++result) {
		assert(it->first >= result);
		if (it->first != result) return result;
	}
	return result;
}

char VCD::getId(const string& name) const
{
	for (auto& p : vars) {
		if (p.second.name == name) return p.first;
	}
	return 0;
}

map<pair<int, char>, int> VCD::getFilteredEvents(vector<char> ids) const
{
	map<pair<int, char>, int> result;
	for (auto& p : events) {
		char id = p.first.second;
		if (find(ids.begin(), ids.end(), id) != ids.end()) {
			result.insert(p);
		}
	}
	return result;
}

void VCD::removeVar(char id)
{
	// Remove events involving 'id'.
	// Note that we're iterating over the map while changing it!
	auto it = events.begin();
	while (it != events.end()) {
		if (it->first.second == id) {
			events.erase(it++);
		} else {
			++it;
		}
	}
	vars.erase(id);
}

void VCD::dump()
{
	if (!date.empty()) {
		cout << "$date " << date << " $end\n";
	}
	if (!version.empty()) {
		cout << "$version " << version << " $end\n";
	}
	if (!timescale.empty()) {
		cout << "$timescale " << timescale << " $end\n";
	}
	if (!scope.empty()) {
		cout << "$scope " << scope << " $end\n";
	}

	for (auto& p : vars) {
		char id = p.first;
		auto& var = p.second;
		cout << "$var wire "
		     << var.size << " "
		     << id << " "
		     << var.name << " $end\n";
	}

	cout << "$upscope $end\n"
	        "$enddefinitions $end\n";

	int prev = -1;
	for (auto& p : events) {
		int time = p.first.first;
		char id = p.first.second;
		int val = p.second;
		if (time != prev) {
			assert(prev < time);
			cout << '#' << time << '\n';
			prev = time;
		}
		int size = getVarSize(id);
		if (size == 1) {
			cout << val << id << '\n';
		} else {
			cout << 'b' << tobin(val) << ' ' << id << '\n';
		}
	}
}

void filter(VCD& vcd)
{
	char idA = vcd.getFreeId();
	vcd.addVar("A", idA, 8);

	vector<char> ids;
	map<char, int> revId;
	char name[3] = { 'A', '*', 0 };
	for (int i = 0; i < 8; ++i) {
		name[1] = '0' + i;
		char id = vcd.getId(name);
		assert(id);
		ids.push_back(id);
		revId[id] = i;
	}

	int time0 = 0;
	int val0 = 0;
	for (auto& e : vcd.getFilteredEvents(ids)) {
		int time = e.first.first;
		int id = e.first.second;
		int val = e.second;
		if (time0 != time) {
			vcd.addChange(time0, idA, val0);
			time0 = time;
		}
		int pos = revId[id];
		int mask = ~(1 << pos);
		val0 = (val0 & mask) | (val << pos);
	}
	vcd.addChange(time0, idA, val0);

	for (char id : ids) {
		vcd.removeVar(id);
	}

	char idNC = vcd.getId("NC");
	if (idNC) {
		vcd.removeVar(idNC);
	}
}

void print(int time, int rw, int addr, int VDS, bool highUsed)
{
	int addr2 = addr;
	//int addr2 = ((addr & 0xffff) << 1) | (addr >> 16);
	cout << "time=" << dec << setfill(' ') << setw(5) << time
	     << (rw ? " r" : " w")
	     << " addr=0x" << hex << setfill('0') << setw(5) << addr2;
	if (highUsed) {
		if (VDS) {
			cout << " burst" << endl;
		} else {
			cout << " burst vds" << endl;
		}
	} else {
		if (VDS) {
			cout << endl;
		} else {
			cout << "       vds" << endl;
		}
	}
	//     << (highUsed ? " burst" : "      ")
	//     << (VDS ? "    " : " vds")
	//     << endl;
}


int main(int argc, char** argv)
{
	VCD vcd(argv[1]);
	//filter(vcd);
	//vcd.dump();
	
	char idA    = vcd.getId("A"   ); assert(idA);
	char idRAS  = vcd.getId("RAS" ); assert(idRAS);
	char idCAS0 = vcd.getId("CAS0"); assert(idCAS0);
	char idCAS1 = vcd.getId("CAS1"); assert(idCAS1);
	char idRW   = vcd.getId("R/W");  assert(idRW);
	char idVDS  = vcd.getId("VDS" ); assert(idVDS);

	/*
	int A = 0;
	int VDS = 0;
	int high = 0;
	bool highUsed = false;
	int rw = 0;
	bool doRAS = false;
	bool doCAS0 = false;
	bool doCAS1 = false;
	int prev = -1;
	for (auto& e : vcd.getEvents()) {
		int time = e.first.first;
		if (time != prev) {
			assert(time > prev);
			if (doRAS) {
				doRAS = false;
				high = A << 8;
				highUsed = false;
			}
			if (doCAS0) {
				doCAS0 = false;
				print(prev, rw, high + A, VDS, highUsed);
				highUsed = true;
			}
			if (doCAS1) {
				doCAS1 = false;
				print(prev, rw, high + A + 0x10000, VDS, highUsed);
				highUsed = true;
			}
			prev = time;
		}


		char id = e.first.second;
		int val = e.second;
		if (id == idA) {
			A = val;
		} else if (id == idRW) {
			rw = val;
		} else if (id == idVDS) {
			VDS = val;
		} else if (id == idRAS) {
			if (val == 0) {
				doRAS = true;
			}
		} else if (id == idCAS0) {
			if (val == 0) {
				doCAS0 = true;
			}
		} else if (id == idCAS1) {
			if (val == 0) {
				doCAS1 = true;
			}
		}
	}*/

	int A = 0;
	int newA = A;
	int VDS = 0;
	int newVDS = VDS;
	int RW = 0;
	int newRW = RW;

	int high = 0;
	bool highUsed = false;
	int prev = -1;

	for (auto& e : vcd.getEvents()) {
		int time = e.first.first;
		if (time != prev) {
			assert(time > prev);
			prev = time;
			A = newA;
			VDS = newVDS;
			RW = newRW;
		}

		char id = e.first.second;
		int val = e.second;
		if (id == idA) {
			newA = val;
		} else if (id == idRW) {
			newRW = val;
		} else if (id == idVDS) {
			newVDS = val;
		} else if (id == idRAS) {
			if (val == 0) {
				high = A << 8;
				highUsed = false;
			}
		} else if (id == idCAS0) {
			if (val == 0) {
				print(prev, RW, high + A, VDS, highUsed);
				highUsed = true;
			}
		} else if (id == idCAS1) {
			if (val == 0) {
				print(prev, RW, high + A + 0x10000, VDS, highUsed);
				highUsed = true;
			}
		}
	}
}
