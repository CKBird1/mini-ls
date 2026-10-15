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

// Packed pair shares one physical site, always emit lower
int lut_site(const LutNode& nd) {
    if (nd.pack_mate < 0)
        return nd.id;
    return nd.id < nd.pack_mate ? nd.id : nd.pack_mate;
}

bool pack_mate_valid(const std::vector<LutNode>& nodes, int n, const LutNode& nd) {
    if (nd.kind != LutKind::Lut || nd.pack_mate < 0)
        return true;
    int m = nd.pack_mate;
    if (m < 0 || m >= n)
        return false;
    const LutNode& mate = nodes[(std::size_t)m];
    return mate.kind == LutKind::Lut && mate.pack_mate == nd.id;
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
    _luts.push_back(lut.id);
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

bool lutGraph::reaches(int a, int b) const {
    if(a < 0 || b < 0) return false;
    if(a >= num_nodes() || b >= num_nodes()) return false;
    if(a == b) return true;
    std::vector<int> worklist;
    std::vector<int> seen((int)_nodes.size(), 0);
    bool found_loop = false;
    worklist.push_back(a);
    seen[a] = 1;
    while(!worklist.empty()) {
        int nid = worklist.back();
        worklist.pop_back();
        if(!(node(nid).kind == LutKind::Lut)) continue;
        for(int afo = 0; afo < (int)node(nid).fanouts.size(); ++afo) {
            if(node(nid).fanouts[afo] == b) {
                found_loop = true;
                break;
            } 
            if(!seen[node(nid).fanouts[afo]]) {
                worklist.push_back(node(nid).fanouts[afo]);
                seen[node(nid).fanouts[afo]] = 1;
            }
        }
        if(found_loop) break;
    }
    return found_loop;
}

void lutGraph::lut_occupancy(int* occ, const std::vector<int>& lut_union, const LutNode& lut) {
    //take input union, and fill occupancy with the luts original positioning
    for(int pins = 0; pins < lut.nPins; ++pins) { //unsorted, 1, 3, 4, 2, x, x
        for(int un = 0; un < (int)lut_union.size(); ++un) { //the position we need to map
            if(lut.pin[pins] == lut_union[un])
                occ[un] = pins;       
        }
    }
}

std::uint64_t lutGraph::union_tt(const int* occ, int union_size, std::uint64_t orig_tt) {
    // Rewrite lut.tt so minterm bits follow lut_union order (via occ).
    std::uint64_t final_tt = 0;
    int num_terms = 1 << union_size;

    for(int minterm = 0; minterm < num_terms; ++minterm) {
        int old = 0;
        for(int un = 0; un < union_size; ++un) {
            if(occ[un] == -1) continue;
            if((minterm >> un) & 1)
                old |= (1 << occ[un]);
        }
        if((orig_tt >> old) & 1)
            final_tt |= (1ULL << minterm);
    }
    return final_tt;
}

void lutGraph::update_lut(int lid, std::vector<int>& lut_union, std::uint64_t tt, int pack_mate) {
    LutNode& lut = _nodes[lid];
    lut.pack_mate = pack_mate;
    lut.tt = tt;
    lut.nPins = (int)lut_union.size();
    for(int i = 0; i < lut.nPins; ++i) {
        lut.pin[i] = lut_union[i];

        //Update driver at the same time
        bool already_driving = false;
        for(int d_fo = 0; d_fo < (int)_nodes[lut.pin[i]].fanouts.size(); ++d_fo) {
            if(_nodes[lut.pin[i]].fanouts[d_fo] == lid) {
                already_driving = true;
                break;
            }
        }
        if(!already_driving) _nodes[lut.pin[i]].fanouts.push_back(lid);
    }
}  

int lutGraph::num_sites() const {
    int n = 0;
    for (int i = 0; i < num_luts(); ++i) {
        const LutNode& nd = node(lut(i));
        if (nd.pack_mate < 0 || nd.id < nd.pack_mate)
            ++n;
    }
    return n;
}

void lutGraph::repack() {
    for(int i = 0; i < num_luts(); ++i) {
        const LutNode& a = node(lut(i));
        if(a.pack_mate != -1) continue; //Already packed with another
        for(int j = i+1; j < num_luts(); ++j) {
            const LutNode& b = node(lut(j));
            if(b.pack_mate != -1) continue; //Already packed with another
            std::vector<int> lut_union;
            std::vector<int> a_pins(a.pin, a.pin+a.nPins);
            std::vector<int> b_pins(b.pin, b.pin+b.nPins);
            std::sort(a_pins.begin(), a_pins.end());
            std::sort(b_pins.begin(), b_pins.end());
            int ita = 0, itb = 0;
            while(ita != a.nPins && itb != b.nPins) {
                if(a_pins[ita] < b_pins[itb]) lut_union.push_back(a_pins[ita++]);
                else if(a_pins[ita] > b_pins[itb]) lut_union.push_back(b_pins[itb++]);
                else if(a_pins[ita] == b_pins[itb]) {
                    lut_union.push_back(a_pins[ita++]); 
                    itb++;
                }
            } //This fills everything until one is empty
            while(ita != a.nPins) lut_union.push_back(a_pins[ita++]);
            while(itb != b.nPins) lut_union.push_back(b_pins[itb++]);
            if((int)lut_union.size() > 6) continue;
            else {
                //first is that a can't be in the fanout of b (indirectly neither) and vise versa
                if(!reaches(a.id, b.id) && !reaches(b.id, a.id)) {
                    int lut_a_occ[6] = {-1, -1, -1, -1, -1, -1};
                    int lut_b_occ[6] = {-1, -1, -1, -1, -1, -1};
                    lut_occupancy(lut_a_occ, lut_union, a);
                    lut_occupancy(lut_b_occ, lut_union, b);

                    std::uint64_t a_tt = union_tt(lut_a_occ, (int)lut_union.size(), a.tt);
                    std::uint64_t b_tt = union_tt(lut_b_occ, (int)lut_union.size(), b.tt);

                    //a/b_tt have updated truth table for positioning in the new combined lut, now truly
                    //mutate the final lut
                    update_lut(a.id, lut_union, a_tt, b.id);
                    update_lut(b.id, lut_union, b_tt, a.id);
                    break;
                }
            }
        }
    }
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
            if (!pack_mate_valid(_nodes, n, nd))
                return blif_fail("LUT pack_mate is not a mutual LUT pair");
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
        // Dual-output pack is two functions on shared pins: two .names, same inputs.
        if (nd.pack_mate >= 0 && nd.id < nd.pack_mate)
            out << "# packed " << emit_name(name(nd.id), nd.id) << " "
                << emit_name(name(nd.pack_mate), nd.pack_mate) << '\n';
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
            if (!pack_mate_valid(_nodes, n, nd))
                return bench_fail("LUT pack_mate is not a mutual LUT pair");
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
        const LutNode& nd = _nodes[(std::size_t)i];
        if (nd.kind != LutKind::Lut)
            continue;
        if (!add_cell(lut_site(nd)))
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
        const int site = lut_site(nd);
        if (!add_pin(nd.id, site))
            return bench_fail("LUT output net");
        for (int j = 0; j < nd.nPins; ++j) {
            if (!add_pin(nd.pin[j], site))
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
