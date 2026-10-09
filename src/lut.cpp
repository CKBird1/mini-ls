#include "lut.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

//Code base not large enough to justify it's own writer class
//Keeping helpers here for now, they don't belong in lutGraph class
namespace {

bool blif_fail(const std::string& msg) {
    std::cerr << "blif: " << msg << '\n';
    return false;
}

bool bench_fail(const std::string& msg) {
    std::cerr << "bench: " << msg << '\n';
    return false;
}

std::string blif_model_name(const char* path) {
    std::string p = path ? path : "mapped";
    auto slash = p.find_last_of("/\\");
    if (slash != std::string::npos)
        p = p.substr(slash + 1);
    auto dot = p.find_last_of('.');
    if (dot != std::string::npos)
        p = p.substr(0, dot);
    if (p.empty())
        p = "mapped";
    for (char& c : p) {
        if (!std::isalnum((unsigned char)c) && c != '_')
            c = '_';
    }
    return p;
}

// BLIF / mini-pd identifiers: [A-Za-z_][A-Za-z0-9_]*. Keep the table's original
// string for lookup; emit a legal form (V321(2) -> V321_2_).
std::string emit_name(const std::string& raw, int id) {
    std::string p = raw.empty() ? ("n" + std::to_string(id)) : raw;
    for (char& c : p) {
        if (!std::isalnum((unsigned char)c) && c != '_')
            c = '_';
    }
    if (p.empty() || !(std::isalpha((unsigned char)p[0]) || p[0] == '_'))
        p.insert(p.begin(), '_');
    return p;
}

// Onset cubes. Pin p of .names is leaf[p]; that pin is bit p of minterm x (LSB = leaf[0]).
void write_onset(std::ostream& out, int nLeaves, std::uint64_t tt) {
    if (nLeaves <= 0) {
        if (tt & 1ull)
            out << "1\n";
        else
            out << " 0\n";
        return;
    }
    const int n = 1 << nLeaves;
    for (int x = 0; x < n; ++x) {
        if (((tt >> x) & 1ull) == 0)
            continue;
        for (int p = 0; p < nLeaves; ++p)
            out << ((x & (1 << p)) ? '1' : '0');
        out << " 1\n";
    }
}

} // namespace

lutGraph::lutGraph() {
    LutNode const0(true);
    const0.id = _nodes.size();
    _nodes.push_back(const0);
    _names.set_name(const0.id);
}

int lutGraph::create_lut(const int* pinI, int np, std::uint64_t table){
    LutNode lut(pinI, np, table);
    lut.id = _nodes.size();
    _nodes.push_back(lut);
    for(int i = 0; i < lut.nPins; ++i) {
        _nodes[lut.pin[i]].fanouts.push_back(lut.id);
    }
    _names.set_name(lut.id);
    return lut.id;
}

int lutGraph::create_pi() {
    LutNode pi;
    pi.id = _nodes.size();
    _nodes.push_back(pi);
    _names.set_name(pi.id);
    _pis.push_back(pi.id);
    return pi.id;
}

int lutGraph::create_po(int d, bool invert) {
    LutNode po(d, invert);
    po.id = _nodes.size();
    _nodes.push_back(po);
    _nodes[d].fanouts.push_back(po.id);
    _names.set_name(po.id);
    _pos.push_back(po.id);
    return po.id;
}

bool lutGraph::write_blif(const char* path) const {
    if (!path || !*path)
        return blif_fail("empty write path");

    const int n = (int)_nodes.size();

    std::ofstream out(path);
    if (!out)
        return blif_fail(std::string("cannot write ") + path);

    bool need_const0 = false;
    for (int i = 0; i < n; ++i) {
        const LutNode& nd = _nodes[(std::size_t)i];
        if (nd.kind == LutKind::Lut) {
            if (nd.nPins < 0 || nd.nPins > 6)
                return blif_fail("LUT has more than 6 inputs (tt is 64-bit)");
            for (int j = 0; j < nd.nPins; ++j) {
                int id = nd.pin[j];
                if (id < 0 || id >= n)
                    return blif_fail("LUT leaf id out of range");
                if (_nodes[(std::size_t)id].kind == LutKind::Const)
                    need_const0 = true;
            }
        } else if (nd.kind == LutKind::Po) {
            int d = nd.driver;
            if (d < 0 || d >= n)
                return blif_fail("PO driver out of range");
            if (_nodes[(std::size_t)d].kind == LutKind::Const)
                need_const0 = true;
        }
    }

    out << ".model " << blif_model_name(path) << '\n';
    out << ".inputs";
    for (int pi : _pis)
        out << " " << emit_name(name(pi), pi);
    out << '\n';
    out << ".outputs";
    for (int po : _pos)
        out << " " << emit_name(name(po), po);
    out << '\n';

    if (need_const0) {
        out << ".names " << emit_name(name(0), 0) << '\n';
        out << " 0\n";
    }

    for (int i = 0; i < n; ++i) {
        const LutNode& nd = _nodes[(std::size_t)i];
        if (nd.kind != LutKind::Lut)
            continue;
        out << ".names";
        for (int j = 0; j < nd.nPins; ++j)
            out << " " << emit_name(name(nd.pin[j]), nd.pin[j]);
        out << " " << emit_name(name(nd.id), nd.id) << '\n';
        write_onset(out, nd.nPins, nd.tt);
    }

    // One buffer/inverter per PO so .outputs are dedicated nets and invert is honored.
    for (int po : _pos) {
        const LutNode& nd = _nodes[(std::size_t)po];
        out << ".names " << emit_name(name(nd.driver), nd.driver)
            << " " << emit_name(name(po), po) << '\n';
        out << (nd.invert ? '0' : '1') << " 1\n";
    }

    out << ".end\n";
    if (!out)
        return blif_fail(std::string("write failed: ") + path);
    return true;
}

bool lutGraph::write_bench(const char* path) const {
    if (!path || !*path)
        return bench_fail("empty write path");

    const int n = (int)_nodes.size();

    bool need_const0 = false;
    for (int i = 0; i < n; ++i) {
        const LutNode& nd = _nodes[(std::size_t)i];
        if (nd.kind == LutKind::Lut) {
            if (nd.nPins < 0 || nd.nPins > 6)
                return bench_fail("LUT has more than 6 inputs");
            for (int j = 0; j < nd.nPins; ++j) {
                int id = nd.pin[j];
                if (id < 0 || id >= n)
                    return bench_fail("LUT leaf id out of range");
                if (_nodes[(std::size_t)id].kind == LutKind::Const)
                    need_const0 = true;
            }
        } else if (nd.kind == LutKind::Po) {
            int d = nd.driver;
            if (d < 0 || d >= n)
                return bench_fail("PO driver out of range");
            if (_nodes[(std::size_t)d].kind == LutKind::Const)
                need_const0 = true;
        }
    }

    std::vector<int> cells;
    std::vector<char> is_cell(n, 0);
    auto add_cell = [&](int id) -> bool {
        if (id < 0 || id >= n)
            return false;
        if (!is_cell[id]) {
            is_cell[id] = 1;
            cells.push_back(id);
        }
        return true;
    };
    if (need_const0 && !add_cell(0))
        return bench_fail("const0 id out of range");
    for (int pi : _pis) {
        if (!add_cell(pi))
            return bench_fail("bad PI id");
    }
    for (int i = 0; i < n; ++i) {
        if (_nodes[(std::size_t)i].kind == LutKind::Lut && !add_cell(i))
            return bench_fail("LUT root id out of range");
    }
    for (int po : _pos) {
        if (!add_cell(po))
            return bench_fail("bad PO id");
    }
    if (cells.empty())
        return bench_fail("no cells to place");

    std::vector<std::vector<int>> net_pins(n);
    auto add_pin = [&](int net, int cell) -> bool {
        if (net < 0 || net >= (int)net_pins.size() || !is_cell[cell])
            return false;
        auto& pins = net_pins[net];
        if (std::find(pins.begin(), pins.end(), cell) == pins.end())
            pins.push_back(cell);
        return true;
    };
    if (need_const0 && !add_pin(0, 0))
        return bench_fail("const0 net");
    for (int pi : _pis) {
        if (!add_pin(pi, pi))
            return bench_fail("PI net");
    }
    for (int i = 0; i < n; ++i) {
        const LutNode& nd = _nodes[(std::size_t)i];
        if (nd.kind != LutKind::Lut)
            continue;
        if (!add_pin(nd.id, nd.id))
            return bench_fail("LUT output net");
        for (int j = 0; j < nd.nPins; ++j) {
            if (!add_pin(nd.pin[j], nd.id))
                return bench_fail("LUT leaf is not a PI, const, or mapped LUT");
        }
    }
    for (int po : _pos) {
        int d = _nodes[(std::size_t)po].driver;
        if (!add_pin(d, po))
            return bench_fail("PO driver is not a placed cell");
    }

    const int cell_w = 4;
    const int cell_h = 2;
    const int row_h = 10;
    const int n_cells = (int)cells.size();
    const double area = (double)n_cells * cell_w * cell_h / 0.5;
    const int side = std::max(cell_w, (int)std::ceil(std::sqrt(area)));
    const int n_rows = std::max(1, (int)std::lround((double)side / row_h));
    const int cells_per_row = (n_cells + n_rows - 1) / n_rows;
    const int die_w = cells_per_row * cell_w;
    const int die_h = n_rows * row_h;

    std::ofstream out(path);
    if (!out)
        return bench_fail(std::string("cannot write ") + path);

    out << "# mapped LUT netlist; unit cells " << cell_w << "x" << cell_h
        << ", row " << row_h << ", ~50% util\n";
    out << "DIE 0 0 " << die_w << " " << die_h << "\n";
    out << "ROWS " << n_rows << " " << row_h << "\n";
    for (int id : cells)
        out << "CELL " << emit_name(name(id), id) << " " << cell_w << " " << cell_h << "\n";
    for (int net = 0; net < (int)net_pins.size(); ++net) {
        if ((int)net_pins[net].size() < 2)
            continue;
        out << "NET " << emit_name(name(net), net);
        for (int cell : net_pins[net])
            out << " " << emit_name(name(cell), cell);
        out << "\n";
    }
    if (!out)
        return bench_fail(std::string("write failed: ") + path);
    return true;
}
