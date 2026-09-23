#include "aig.hpp"
#include "rwlib.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static bool write_npn_file(const char* path, const std::vector<std::uint16_t>& classes) {
    std::filesystem::path p(path);
    if (p.has_parent_path() && !p.parent_path().empty())
        std::filesystem::create_directories(p.parent_path());
    std::ofstream os(path);
    if(!os) return false;
    os << "# 4-input NPN classes (canonical 16-bit truth tables)\n";
    os << classes.size() << '\n';
    for(std::size_t i = 0; i < classes.size(); ++i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%04X", (unsigned)classes[i]);
        os << buf << '\n';
    }
    return (bool)os;
}

bool RwLib::generate_npn(const char* path) {
    _classes.clear();

    std::vector<char> made_canon(65536, 0); //could also use unordered_set for this, but this is clean and no hash

    //Very brute force, and very long runtime, but that's (more) ok because it's run once and then use forever
    int next_tt = 0;
    for(int i = 0; i < 65536; ++i) {
        NPN next = npn_canon((std::uint16_t)next_tt);
        std::uint16_t canon = next.canon;
        if(!made_canon[canon]) {
            _classes.push_back(canon);
            made_canon[canon] = 1;
        }
        next_tt++;
    }

    if(_classes.empty()) return false;
    return write_npn_file(path, _classes);
}

bool RwLib::load_npn(const char* path) {
    std::ifstream in(path);
    if(!in) return false;

    _classes.clear();
    std::string line;
    int expected = -1;
    while(std::getline(in, line)) {
        if(line.empty() || line[0] == '#') continue;
        char* end = nullptr;
        if(expected < 0) {
            expected = (int)std::strtol(line.c_str(), &end, 10);
            if(end == line.c_str()) return false;
            continue;
        }
        unsigned long v = std::strtoul(line.c_str(), &end, 16);
        if(end == line.c_str() || v > 0xFFFFul) return false;
        _classes.push_back((std::uint16_t)v);
    }
    if(expected >= 0 && (int)_classes.size() != expected) return false;
    return !_classes.empty();
}
