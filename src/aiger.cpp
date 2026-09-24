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
    uint32_t var = (uint32_t)lit_id(aiger_lit);
    if (var >= var_lit.size() || !defined[var]) {
        return false;
    }
    out = var_lit[var] ^ (uint32_t)lit_inv(aiger_lit);
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
        uint32_t var = (uint32_t)lit_id(lhs);
        if (lit_inv(lhs) || var == 0 || var > M || defined[var]) {
            return fail("bad AND lhs");
        }
        uint32_t in0 = 0, in1 = 0;
        if (!import_lit(var_lit, defined, rhs0, in0) ||
            !import_lit(var_lit, defined, rhs1, in1)) {
            return fail("AND fanin is not defined");
        }
        var_lit[var] = create_and(lit_id(in0), lit_inv(in0),
                                  lit_id(in1), lit_inv(in1));
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
            uint32_t lhs = make_lit((int)(I + 1 + k), false);
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
            create_po(lit_id(our), lit_inv(our));
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
        if (lit != make_lit((int)i, false)) {
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
        create_po(lit_id(our), lit_inv(our));
    }
    return true;
}

static bool encode_u32(std::ostream& out, uint32_t x) {
    for (unsigned i = 0; i < 5; ++i) {
        uint32_t byte = x & 0x7f;
        x >>= 7;
        if (x) {
            out.put((char)(byte | 0x80));
        } else {
            out.put((char)byte);
            return (bool)out;
        }
    }
    return false;
}

static bool ends_with_aag(const char* path) {
    const std::string p(path);
    return p.size() >= 4 && p.compare(p.size() - 4, 4, ".aag") == 0;
}

bool aigGraph::write_aiger(const char* path) const {
    if (!path || !*path) {
        return fail("empty write path");
    }

    const bool binary = !ends_with_aag(path);

    std::vector<uint32_t> aig_var(_nodes.size(), 0);
    uint32_t next = 1;
    for (int pi : _pis) {
        if (pi <= 0 || (std::size_t)pi >= _nodes.size() || !_nodes[(std::size_t)pi].isPi) {
            return fail("bad PI id");
        }
        aig_var[(std::size_t)pi] = next++;
    }

    // Number ANDs fanins-first. After swing, a low-index AND can read a
    // newly appended node, so _nodes index order is not AIGER topo order.
    std::vector<int> ands;
    std::vector<char> seen(_nodes.size(), 0);
    auto assign_and = [&](auto&& self, int id) -> void {
        if (id < 0 || (std::size_t)id >= _nodes.size() || seen[(std::size_t)id])
            return;
        seen[(std::size_t)id] = 1;
        const aigNode& n = _nodes[(std::size_t)id];
        if (n.isPi || n.isPo || n.isConst || n.tombstone)
            return;
        self(self, n.input_a);
        self(self, n.input_b);
        aig_var[(std::size_t)id] = next++;
        ands.push_back(id);
    };
    for (std::size_t i = 0; i < _nodes.size(); ++i) {
        const aigNode& n = _nodes[i];
        if (n.isPi || n.isPo || n.isConst || n.tombstone)
            continue;
        assign_and(assign_and, (int)i);
    }

    const uint32_t I = (uint32_t)_pis.size();
    const uint32_t O = (uint32_t)_pos.size();
    const uint32_t A = (uint32_t)ands.size();
    const uint32_t M = I + A;
    if (next - 1 != M) {
        return fail("aiger variable numbering mismatch");
    }

    auto lit_of = [&](int id, bool inv, uint32_t& out) -> bool {
        if (id < 0 || (std::size_t)id >= aig_var.size()) {
            return false;
        }
        if (id != 0 && aig_var[(std::size_t)id] == 0) {
            return false;
        }
        out = make_lit((int)aig_var[(std::size_t)id], inv);
        return true;
    };

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return fail(std::string("cannot write ") + path);
    }

    out << (binary ? "aig" : "aag") << ' ' << M << ' ' << I << " 0 " << O << ' ' << A << '\n';

    if (!binary) {
        for (uint32_t i = 1; i <= I; ++i) {
            out << make_lit((int)i, false) << '\n';
        }
    }

    for (int po : _pos) {
        if (po < 0 || (std::size_t)po >= _nodes.size() || !_nodes[(std::size_t)po].isPo) {
            return fail("bad PO id");
        }
        const aigNode& n = _nodes[(std::size_t)po];
        uint32_t lit = 0;
        if (!lit_of(n.input_a, n.invert_a, lit)) {
            return fail("PO driver is not a live AIGER variable");
        }
        out << lit << '\n';
    }

    for (int id : ands) {
        const aigNode& n = _nodes[(std::size_t)id];
        const uint32_t lhs = make_lit((int)aig_var[(std::size_t)id], false);
        uint32_t rhs0 = 0, rhs1 = 0;
        if (!lit_of(n.input_a, n.invert_a, rhs0) || !lit_of(n.input_b, n.invert_b, rhs1)) {
            return fail("AND fanin is not a live AIGER variable");
        }
        if (rhs0 < rhs1) {
            const uint32_t t = rhs0;
            rhs0 = rhs1;
            rhs1 = t;
        }
        if (lhs <= rhs0) {
            return fail("AND is not in topological order");
        }
        if (binary) {
            if (!encode_u32(out, lhs - rhs0) || !encode_u32(out, rhs0 - rhs1)) {
                return fail("failed to write AND");
            }
        } else {
            out << lhs << ' ' << rhs0 << ' ' << rhs1 << '\n';
        }
    }

    if (!out) {
        return fail(std::string("failed to write ") + path);
    }
    return true;
}
