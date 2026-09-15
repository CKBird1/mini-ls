#include "aig.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static bool fail(const std::string& msg) {
    std::cerr << "aiger: " << msg << '\n';
    return false;
}

static bool decode_u32(std::istream& in, uint32_t& out) {
    uint32_t x = 0;
    for (unsigned i = 0; i < 5; ++i) {
        int c = in.get();
        if (c == EOF) {
            return false;
        }
        x |= (uint32_t)(c & 0x7f) << (7 * i);
        if ((c & 0x80) == 0) {
            out = x;
            return true;
        }
    }
    return false;
}

static bool import_lit(const std::vector<uint32_t>& var_lit,
                       const std::vector<char>& defined,
                       uint32_t aiger_lit,
                       uint32_t& out) {
    uint32_t var = aiger_lit >> 1;
    if (var >= var_lit.size() || !defined[var]) {
        return false;
    }
    out = var_lit[var] ^ (aiger_lit & 1u);
    return true;
}

bool aigGraph::read_aiger(const char* path) {
    if (_nodes.size() != 1 || !_pis.empty() || !_pos.empty()) {
        return fail("graph is not empty");
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return fail(std::string("cannot open ") + path);
    }

    std::string line;
    if (!std::getline(in, line)) {
        return fail("empty file");
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    std::istringstream hs(line);
    std::string magic;
    uint32_t M = 0, I = 0, L = 0, O = 0, A = 0;
    if (!(hs >> magic >> M >> I >> L >> O >> A)) {
        return fail("bad header");
    }
    uint32_t extra = 0;
    while (hs >> extra) {
        if (extra != 0) {
            return fail("unsupported aiger extension");
        }
    }

    const bool binary = (magic == "aig");
    if (!binary && magic != "aag") {
        return fail("expected aig or aag");
    }
    if (L != 0) {
        return fail("sequential aiger (latches) not supported");
    }
    if (binary && M != I + A) {
        return fail("binary aiger requires M = I + A");
    }
    if (M < I + A) {
        return fail("M smaller than I + A");
    }

    std::vector<uint32_t> var_lit(M + 1, 0);
    std::vector<char> defined(M + 1, 0);
    var_lit[0] = 0;
    defined[0] = 1;

    auto add_and = [&](uint32_t lhs, uint32_t rhs0, uint32_t rhs1) -> bool {
        uint32_t var = lhs >> 1;
        if ((lhs & 1u) || var == 0 || var > M || defined[var]) {
            return fail("bad AND lhs");
        }
        uint32_t in0 = 0, in1 = 0;
        if (!import_lit(var_lit, defined, rhs0, in0) ||
            !import_lit(var_lit, defined, rhs1, in1)) {
            return fail("AND fanin is not defined");
        }
        var_lit[var] = create_and((int)(in0 >> 1), (bool)(in0 & 1u),
                                  (int)(in1 >> 1), (bool)(in1 & 1u));
        defined[var] = 1;
        return true;
    };

    if (binary) {
        for (uint32_t i = 1; i <= I; ++i) {
            var_lit[i] = create_pi();
            defined[i] = 1;
        }

        // ABC / common .aig: ASCII output literals, then binary ANDs.
        // (Biere's spec encodes those literals too; i10.aig is the ABC form.)
        std::vector<uint32_t> po_lits(O);
        for (uint32_t i = 0; i < O; ++i) {
            if (!std::getline(in, line)) {
                return fail("truncated outputs");
            }
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            std::istringstream ls(line);
            if (!(ls >> po_lits[i])) {
                return fail("bad output literal");
            }
        }

        for (uint32_t k = 0; k < A; ++k) {
            uint32_t lhs = 2u * (I + 1 + k);
            uint32_t d0 = 0, d1 = 0;
            if (!decode_u32(in, d0) || !decode_u32(in, d1)) {
                return fail("truncated AND section");
            }
            if (d0 > lhs) {
                return fail("AND delta overflow");
            }
            uint32_t rhs0 = lhs - d0;
            if (d1 > rhs0) {
                return fail("AND delta overflow");
            }
            uint32_t rhs1 = rhs0 - d1;
            if (!add_and(lhs, rhs0, rhs1)) {
                return false;
            }
        }

        for (uint32_t i = 0; i < O; ++i) {
            uint32_t our = 0;
            if (!import_lit(var_lit, defined, po_lits[i], our)) {
                return fail("output refers to undefined variable");
            }
            create_po((int)(our >> 1), (bool)(our & 1u));
        }
        return true;
    }

    // ASCII .aag
    auto read_uint = [&](uint32_t& v) -> bool {
        if (!std::getline(in, line)) {
            return false;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream ls(line);
        return (bool)(ls >> v);
    };

    for (uint32_t i = 1; i <= I; ++i) {
        uint32_t lit = 0;
        if (!read_uint(lit)) {
            return fail("truncated inputs");
        }
        if (lit != 2u * i) {
            return fail("ASCII input literals must be 2, 4, ..., 2I");
        }
        var_lit[i] = create_pi();
        defined[i] = 1;
    }

    std::vector<uint32_t> po_lits(O);
    for (uint32_t i = 0; i < O; ++i) {
        if (!read_uint(po_lits[i])) {
            return fail("truncated outputs");
        }
    }

    for (uint32_t k = 0; k < A; ++k) {
        if (!std::getline(in, line)) {
            return fail("truncated AND section");
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream ls(line);
        uint32_t lhs = 0, rhs0 = 0, rhs1 = 0;
        if (!(ls >> lhs >> rhs0 >> rhs1)) {
            return fail("bad AND line");
        }
        if (!add_and(lhs, rhs0, rhs1)) {
            return false;
        }
    }

    for (uint32_t i = 0; i < O; ++i) {
        uint32_t our = 0;
        if (!import_lit(var_lit, defined, po_lits[i], our)) {
            return fail("output refers to undefined variable");
        }
        create_po((int)(our >> 1), (bool)(our & 1u));
    }
    return true;
}
