#include "sdc.hpp"
#include "aig.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static bool fail_at(const char* path, int lineno, const std::string& msg) {
    std::cerr << "error: " << path << ":" << lineno << ": " << msg << '\n';
    return false;
}

static std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tok;
    std::string cur;
    auto flush = [&]() {
        if (!cur.empty()) {
            tok.push_back(cur);
            cur.clear();
        }
    };
    for (char c : line) {
        if (c == '#')
            break;
        if (c == ' ' || c == '\t' || c == '\r') {
            flush();
            continue;
        }
        if (c == '[' || c == ']' || c == '{' || c == '}') {
            flush();
            tok.push_back(std::string(1, c));
            continue;
        }
        cur.push_back(c);
    }
    flush();
    return tok;
}

static bool parse_nonneg(const std::string& s, int& v) {
    char* end = nullptr;
    long x = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end || x < 0)
        return false;
    v = (int)x;
    return true;
}

struct PortRef {
    bool all_outputs = false;
    std::string name;
};

static bool parse_port(const std::vector<std::string>& t, std::size_t& i,
                       PortRef& p, std::string& err) {
    if (i >= t.size()) {
        err = "missing port";
        return false;
    }
    if (t[i] == "[") {
        ++i;
        if (i >= t.size()) {
            err = "empty []";
            return false;
        }
        if (t[i] == "all_outputs") {
            p.all_outputs = true;
            ++i;
        } else if (t[i] == "get_ports") {
            ++i;
            if (i < t.size() && t[i] == "{") {
                ++i;
                if (i >= t.size()) {
                    err = "get_ports missing name";
                    return false;
                }
                p.name = t[i++];
                if (i >= t.size() || t[i] != "}") {
                    err = "missing }";
                    return false;
                }
                ++i;
            } else {
                if (i >= t.size()) {
                    err = "get_ports missing name";
                    return false;
                }
                p.name = t[i++];
            }
        } else {
            err = "expected get_ports or all_outputs";
            return false;
        }
        if (i >= t.size() || t[i] != "]") {
            err = "missing ]";
            return false;
        }
        ++i;
        if (!p.all_outputs && p.name == "*") {
            err = "wildcards not supported";
            return false;
        }
        return true;
    }
    if (t[i] == "{") {
        ++i;
        if (i >= t.size()) {
            err = "empty {}";
            return false;
        }
        p.name = t[i++];
        if (i >= t.size() || t[i] != "}") {
            err = "missing }";
            return false;
        }
        ++i;
        if (p.name == "*") {
            err = "wildcards not supported";
            return false;
        }
        return true;
    }
    if (t[i] == "*") {
        err = "wildcards not supported";
        return false;
    }
    p.name = t[i++];
    return true;
}

static bool apply_named_po(Constraints& sdc, const aigGraph& g, const PortRef& p,
                           bool max_delay, int delay) {
    if (p.all_outputs) {
        if (max_delay)
            return sdc.bind_max_delay_all_outputs(g, delay);
        return sdc.bind_false_path_all_outputs(g);
    }
    if (max_delay)
        return sdc.bind_max_delay(g, p.name, delay);
    return sdc.bind_false_path(g, p.name);
}

static bool apply_line(const std::vector<std::string>& t, const char* path, int lineno,
                       const aigGraph& g, Constraints& sdc) {
    const std::string& cmd = t[0];
    if (cmd == "create_clock") {
        int period = -1;
        bool saw_period = false;
        for (std::size_t i = 1; i < t.size(); ) {
            if (t[i] == "-period") {
                ++i;
                if (i >= t.size() || !parse_nonneg(t[i], period))
                    return fail_at(path, lineno, "create_clock -period needs an integer >= 0");
                saw_period = true;
                ++i;
            } else if (t[i] == "-name") {
                ++i;
                if (i >= t.size())
                    return fail_at(path, lineno, "create_clock -name needs a name");
                ++i;
            } else {
                return fail_at(path, lineno, "create_clock source pin not supported (virtual clock only)");
            }
        }
        if (!saw_period)
            return fail_at(path, lineno, "create_clock missing -period");
        sdc.set_period(period);
        return true;
    }

    if (cmd == "set_max_delay") {
        int delay = -1;
        bool saw_delay = false;
        bool saw_to = false;
        PortRef port;
        for (std::size_t i = 1; i < t.size(); ) {
            if (t[i] == "-to") {
                ++i;
                std::string err;
                if (!parse_port(t, i, port, err))
                    return fail_at(path, lineno, err);
                saw_to = true;
            } else if (t[i] == "-from" || t[i] == "-through") {
                return fail_at(path, lineno, "set_max_delay -from/-through not supported");
            } else if (!t[i].empty() && t[i][0] == '-') {
                return fail_at(path, lineno, "unsupported flag " + t[i]);
            } else {
                if (saw_delay)
                    return fail_at(path, lineno, "set_max_delay extra token '" + t[i] + "'");
                if (!parse_nonneg(t[i], delay))
                    return fail_at(path, lineno, "set_max_delay delay must be an integer >= 0");
                saw_delay = true;
                ++i;
            }
        }
        if (!saw_delay)
            return fail_at(path, lineno, "set_max_delay missing delay");
        if (!saw_to)
            return fail_at(path, lineno, "set_max_delay missing -to");
        if (!apply_named_po(sdc, g, port, true, delay))
            return false;
        return true;
    }

    if (cmd == "set_false_path") {
        bool saw_to = false;
        PortRef port;
        for (std::size_t i = 1; i < t.size(); ) {
            if (t[i] == "-to") {
                ++i;
                std::string err;
                if (!parse_port(t, i, port, err))
                    return fail_at(path, lineno, err);
                saw_to = true;
            } else if (t[i] == "-from" || t[i] == "-through") {
                return fail_at(path, lineno, "set_false_path -from/-through not supported");
            } else {
                return fail_at(path, lineno, "unexpected token '" + t[i] + "'");
            }
        }
        if (!saw_to)
            return fail_at(path, lineno, "set_false_path missing -to");
        if (!apply_named_po(sdc, g, port, false, 0))
            return false;
        return true;
    }

    if (cmd == "set_dont_touch") {
        if (t.size() < 2)
            return fail_at(path, lineno, "set_dont_touch missing name");
        std::size_t i = 1;
        PortRef port;
        std::string err;
        if (!parse_port(t, i, port, err))
            return fail_at(path, lineno, err);
        if (i != t.size())
            return fail_at(path, lineno, "set_dont_touch extra token");
        if (port.all_outputs)
            return fail_at(path, lineno, "set_dont_touch [all_outputs] not supported");
        if (!sdc.bind_dont_touch(g, port.name))
            return false;
        return true;
    }

    return fail_at(path, lineno, "unsupported command '" + cmd + "'");
}

bool read_sdc(const char* path, const aigGraph& g, Constraints& sdc) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "error: cannot read sdc '" << path << "'\n";
        return false;
    }
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        if (!line.empty() && line.back() == '\\')
            return fail_at(path, lineno, "line continuation not supported");
        const auto toks = tokenize(line);
        if (toks.empty())
            continue;
        if (!apply_line(toks, path, lineno, g, sdc))
            return false;
    }
    return true;
}
