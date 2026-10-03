#include "aig.hpp"
#include "sdc.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <climits>

struct LutCut {
    int leaf[8] = {};
    int nLeaves = 0;
    std::uint64_t tt = 0;
    int delay = 0;
    float area_flow = 0.f;
};

bool aigGraph::cut_better(const LutCut& a, const LutCut& b) {
    if(a.delay != b.delay)
        return a.delay < b.delay;
    return a.area_flow < b.area_flow;
} //Custom comparator for sorting best cuts

bool aigGraph::cut_better_time(const LutCut& a, const LutCut& b, int req) {
    int a_meets = a.delay <= req;
    int b_meets = b.delay <= req;
    if(a_meets != b_meets) return a_meets;
    if(a_meets) return a.area_flow < b.area_flow;
    else return a.delay < b.delay;
} //takes required time into the decision (must meet), and may allow slower cuts if they are better area

bool aigGraph::same_cut(LutCut ca, LutCut cb) {
    if(ca.nLeaves != cb.nLeaves) return false;
    else {
        for(int i = 0; i < ca.nLeaves; ++i) {
            if(ca.leaf[i] != cb.leaf[i]) return false;
        }
    }
    return true;
}

// Need to pull the correct location
static int leaf_slot(const LutCut& nc, int id) {
    for (int p = 0; p < nc.nLeaves; ++p) {
        if (nc.leaf[p] == id)
            return p;
    }
    return -1;
}

//Using the correct location, determine current setting and if 1, set it
static int fanin_minterm(const LutCut& fanin, const LutCut& nc, int x) {
    int idx = 0;
    for (int t = 0; t < fanin.nLeaves; ++t) {
        int p = leaf_slot(nc, fanin.leaf[t]);
        if (x & (1 << p))
            idx |= (1 << t);
    }
    return idx;
}

//Output of this is a 64bit truth table that is an AND'd version of the two input LutCuts
//However, since the LutCuts from the fanins can have multiple leaves in multiple orders
//We need to make sure we choose the proper positioning in that fanin tt for the operation
static std::uint64_t compose_and_tt(const LutCut& ca, bool inv_a,
                                    const LutCut& cb, bool inv_b,
                                    const LutCut& nc) {
    std::uint64_t tt = 0;
    int n = 1 << nc.nLeaves;
    for (int x = 0; x < n; ++x) {
        int ia = fanin_minterm(ca, nc, x);
        int ib = fanin_minterm(cb, nc, x);
        int bita = (int)((ca.tt >> ia) & 1);
        int bitb = (int)((cb.tt >> ib) & 1);
        if (inv_a) bita ^= 1;
        if (inv_b) bitb ^= 1;
        if (bita & bitb) //If the specific bit of interest should be set to 1
            tt |= (std::uint64_t)1 << x;
    }
    return tt;
}

LutCut aigGraph::upper_cut(LutCut ca, LutCut cb, int k) {
    int i = 0, j = 0, index = 0;
    int out[8];
    LutCut new_cut;
    while(i < ca.nLeaves && j < cb.nLeaves) {
        if (index >= k) return new_cut;
        if(ca.leaf[i] == cb.leaf[j]) {
            out[index++] = ca.leaf[i];
            ++i; ++j;
        } else if(ca.leaf[i] < cb.leaf[j]) {
            out[index++] = ca.leaf[i++];
        } else {
            out[index++] = cb.leaf[j++];
        }
    }
    while(i < ca.nLeaves) {
        if (index >= k) return new_cut;
        out[index++] = ca.leaf[i++];
    }
    while(j < cb.nLeaves) {
        if (index >= k) return new_cut;
        out[index++] = cb.leaf[j++];
    }
    new_cut.nLeaves = index;
    i = 0;
    while(i < index) { new_cut.leaf[i] = out[i]; ++i; }
    return new_cut;
}

void aigGraph::map(int k, const Constraints& constraints) {
    _mapped_luts.clear();
    _has_mapping = false;
    rebuild_fanouts();
    rebuild_order();
    std::vector<std::vector<LutCut>> cuts_by_node((int)_nodes.size());
    std::vector<int> node_delay((int)_nodes.size(), 0);
    std::vector<float> node_area((int)_nodes.size(), 0.f);

    //Seed all the PIs/Const
    for(int i = 0; i < (int)_nodes.size(); ++i) {
        if(_nodes[i].is_pi_or_const()) {
            LutCut lc;
            lc.nLeaves = 1;
            lc.leaf[0] = i;
            if(_nodes[i].isPi) lc.tt = 0x2;
            else lc.tt = 0; //Constant 0
            lc.delay = 0;
            lc.area_flow = 0; 
            cuts_by_node[i].push_back(lc);

            node_delay[i] = 0;
            node_area[i] = 0.f;
        }
    }
    


    //Through topo-ANDs for identity cut + other cuts
    for(int i = 0; i < (int)_kahns.size(); ++i) {
        int nid = _kahns[i]; //Topo order, will only ever get live ANDs
        //First push identity LutCut
        LutCut lc;
        lc.nLeaves = 1;
        lc.leaf[0] = nid;
        lc.tt = 0x2;
        lc.delay = 0;
        lc.area_flow = 0;
        cuts_by_node[nid].push_back(lc);

        //Now start creating new cuts by combining fanin cuts
        int adex = _nodes[nid].input_a;
        int bdex = _nodes[nid].input_b;
        for(int j = 0; (std::size_t)j < cuts_by_node[adex].size(); ++j) {
            bool full = false;
            for(int l = 0; (std::size_t)l < cuts_by_node[bdex].size(); ++l) {
                LutCut ca = cuts_by_node[adex][j];
                LutCut cb = cuts_by_node[bdex][l];
                LutCut nc = upper_cut(ca, cb, k);
                if(nc.nLeaves == 0) continue;
                int max_delay = 0;
                float area = 0.f;
                for(int leaf = 0; leaf < (int)nc.nLeaves; ++leaf) {
                    if(node_delay[nc.leaf[leaf]] > max_delay) max_delay = node_delay[nc.leaf[leaf]];
                    int fo = (int)_nodes[nc.leaf[leaf]].fanouts.size();
                    if(fo < 1) fo = 1;
                    area += node_area[nc.leaf[leaf]] / (float)fo;
                }
                nc.delay = 1 + max_delay;
                nc.area_flow = 1 + area;
                nc.tt = compose_and_tt(ca, _nodes[nid].invert_a,
                                       cb, _nodes[nid].invert_b, nc); 
                
                bool same = false;
                for(int l = 0; l < (int)cuts_by_node[nid].size(); ++l) {
                    same = same_cut(nc, cuts_by_node[nid][l]);
                    if(same) break;
                }
                if(!same) cuts_by_node[nid].push_back(nc); 
                //Normally I'd only keep the best 8 here, but for small sets like these
                //its ok for now to just sort and trim after
            }
            if(full) break;
        }
        //Sort cuts_by_node[nid] by delay -> area, and then keep only the 8 best
        //make sure the sort alg keeps the identity at the front at all times (never sort pos0)
        auto& cuts = cuts_by_node[nid];
        if((int)cuts.size() > 2)
            std::sort(cuts.begin() + 1, cuts.end(),
                [this](const LutCut& a, const LutCut& b) { return cut_better(a, b); });
        if((int)cuts.size() > 9)
            cuts.resize(9);
        if((int)cuts.size() >= 2) {
            node_delay[nid] = cuts[1].delay;
            node_area[nid] = cuts[1].area_flow;
        }
    }

    //Now start covering
    std::vector<char> used((int)_nodes.size());
    map_cover(cuts_by_node, used);

    std::vector<int> required_time((int)_nodes.size(), INT_MAX);
    std::vector<int> required_worklist;
    if (constraints.has_timing()) {
        for(int p = 0; p < (int)_pos.size(); ++p) {
            int driver = _nodes[_pos[p]].input_a;
            int cap = constraints.po_cap(p);
            if(cap != INT_MAX) {
                required_time[driver] = std::min(required_time[driver], cap);
                required_worklist.push_back(driver);
            }
        }
        //Now required_time is populated, recurse over the netlist covers-style
        //to set the now required_time for all leaves
        while(!required_worklist.empty()) {
            int nid = required_worklist.back();
            required_worklist.pop_back();
            if(!_nodes[nid].is_and() || !used[nid] || cuts_by_node[nid].size() <= 1) continue;
            LutCut lc = cuts_by_node[nid][1];
            if(lc.nLeaves == 1 && lc.leaf[0] == nid) continue;
            for(int l = 0; l < lc.nLeaves; ++l) {
                int lnid = lc.leaf[l];
                if(!_nodes[lnid].is_and()) continue;
                int before_check = required_time[lnid];
                required_time[lnid] = std::min(required_time[lnid], required_time[nid]-1);
                if(required_time[lnid] < before_check) required_worklist.push_back(lnid);
            }
        }
    }

    int mapped_depth = 0;
    for(int i = 0; i < (int)_pos.size(); ++i) {
        if(_nodes[_pos[i]].input_a >= (int)_nodes.size()) continue;
        int depth = node_delay[_nodes[_pos[i]].input_a];
        if(depth > mapped_depth) {
            mapped_depth = depth;
        }
    }
    int worst_negative_slack = INT_MAX; 
    for(int po = 0; po < (int)_pos.size(); ++po) {
        int cap = constraints.po_cap(po);
        if(cap == INT_MAX) continue;
        int slack = cap - node_delay[_nodes[_pos[po]].input_a];
        worst_negative_slack = std::min(worst_negative_slack, slack);
    } 
    if(worst_negative_slack == INT_MAX) worst_negative_slack = 0;

    
    //Now calculate LUT count
    int count_of_lut = 0;
    for(int i = 0; i < (int)used.size(); ++i) {
        if(used[i]) count_of_lut++; 
    }

    std::cout << "luts = " << count_of_lut << " lev = " << mapped_depth;  
    if(constraints.has_timing()) std::cout << " wns = " << worst_negative_slack << std::endl;
    else std::cout << std::endl;

    if(constraints.has_timing()) { 
    //Now change LutCuts to care only about meeting requirement, and then area instead
    //of delay first -> area second
        for(int i = 0; i < (int)_kahns.size(); ++i) {
            int nid = _kahns[i];
            for(int j = 1; j < (int)cuts_by_node[nid].size(); ++j) {
                if((cuts_by_node[nid][j].nLeaves == 1) && (cuts_by_node[nid][j].leaf[0] == nid)) continue;
                LutCut lc = cuts_by_node[nid][j];
                int max_delay = 0;
                for(int leaf = 0; leaf < (int)lc.nLeaves; ++leaf)
                    if(node_delay[lc.leaf[leaf]] > max_delay) max_delay = node_delay[lc.leaf[leaf]];
                cuts_by_node[nid][j].delay = 1 + max_delay;
            } //Now all cuts have recomputed delay, sort this nodes cuts by required_time
            
            auto& cuts = cuts_by_node[nid];
            int req = required_time[nid];
            if(req == INT_MAX) continue;
            if((int)cuts.size() > 2)
                std::sort(cuts.begin() + 1, cuts.end(),
                    [this, req](const LutCut& a, const LutCut& b) { return cut_better_time(a, b, req); });
            if((int)cuts.size() > 9)
                cuts.resize(9);
            if((int)cuts.size() >= 2) {
                node_delay[nid] = cuts[1].delay;
            }
        }
        //Now re-calc and print
        std::fill(used.begin(), used.end(), 0);
        map_cover(cuts_by_node, used);

        mapped_depth = 0;
        for(int i = 0; i < (int)_pos.size(); ++i) {
            if(_nodes[_pos[i]].input_a >= (int)_nodes.size()) continue;
            int depth = node_delay[_nodes[_pos[i]].input_a];
            if(depth > mapped_depth) {
                mapped_depth = depth;
            }
        }
        int worst_negative_slack = INT_MAX; 
        for(int po = 0; po < (int)_pos.size(); ++po) {
            int cap = constraints.po_cap(po);
            if(cap == INT_MAX) continue;
            int slack = cap - node_delay[_nodes[_pos[po]].input_a];
            worst_negative_slack = std::min(worst_negative_slack, slack);
        } 
        if(worst_negative_slack == INT_MAX) worst_negative_slack = 0;
        
        //Now calculate LUT count
        count_of_lut = 0;
        for(int i = 0; i < (int)used.size(); ++i) {
            if(used[i]) count_of_lut++; 
        }

        std::cout << "luts = " << count_of_lut << " lev = " << mapped_depth;  
        if(constraints.has_timing()) std::cout << " wns = " << worst_negative_slack << std::endl;
        else std::cout << std::endl;
    
    }

    _mapped_luts.clear();
    _mapped_luts.reserve((std::size_t)count_of_lut);
    for (int i = 0; i < (int)used.size(); ++i) {
        if (!used[i]) continue;
        if ((int)cuts_by_node[i].size() < 2) continue;
        const LutCut& lc = cuts_by_node[i][1];
        MappedLut m;
        m.root = i;
        m.nLeaves = lc.nLeaves;
        m.tt = lc.tt;
        for (int j = 0; j < lc.nLeaves && j < 8; ++j)
            m.leaf[j] = lc.leaf[j];
        _mapped_luts.push_back(m);
    }
    _has_mapping = true;
}

void aigGraph::process_lut_root(const std::vector<std::vector<LutCut>>& cuts_by_node, std::vector<char>& used, int nid) {
    //We have a generic node id (nid), check if its used, mark it used, then go through 
    //the best cuts leaves on it and recurse
    if(nid < 0 || nid >= (int)_nodes.size() || nid >= (int)used.size() || 
            nid >= (int)cuts_by_node.size() || !_nodes[nid].is_and() || 
            used[nid] || (int)cuts_by_node[nid].size() <= 1) return;
    //If we never found any better cuts, cut[1] doesn't exist
    
    LutCut lc = cuts_by_node[nid][1];
    if(lc.nLeaves == 1 && lc.leaf[0] == nid) return; //Don't return identity

    used[nid] = true;
    for(int i = 0; i < lc.nLeaves; ++i) {
        process_lut_root(cuts_by_node, used, lc.leaf[i]);
    }
}

void aigGraph::map_cover(const std::vector<std::vector<LutCut>>& cuts_by_node, std::vector<char>& used) {
    //Iterate over all the POs and seed a worklist of some sort with all the input_as that feed them
    //Then go through those one at a time, those are LUT roots. used vector stores the LUT roots only
    //after marking used[id]=1, take the best cut (cuts[1] because we sorted), and iterate through the
    //Leaves. each of those leaves is a new LUT root, add those to the worklist and recurse
    //We stop recursing when we hit a PI/Const or when when used is already true for that ID
    //If used was already true, we've already read through that LUT and can be done

    //Order of traversal here doesn't matter
    for(int i = 0; i < (int)_nodes.size(); ++i) {
        if(_nodes[i].isPo)
            process_lut_root(cuts_by_node, used, _nodes[i].input_a);
    }
}

static bool blif_fail(const std::string& msg) {
    std::cerr << "blif: " << msg << '\n';
    return false;
}

static std::string blif_model_name(const char* path) {
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

// Onset cubes. Pin p of .names is leaf[p]; that pin is bit p of minterm x (LSB = leaf[0]).
static void write_onset(std::ostream& out, int nLeaves, std::uint64_t tt) {
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

bool aigGraph::write_blif(const char* path) const {
    if (!path || !*path)
        return blif_fail("empty write path");
    if (!_has_mapping)
        return blif_fail("map has not been run");

    std::ofstream out(path);
    if (!out)
        return blif_fail(std::string("cannot write ") + path);

    bool need_const0 = false;
    for (const MappedLut& m : _mapped_luts) {
        if (m.nLeaves < 0 || m.nLeaves > 6)
            return blif_fail("LUT has more than 6 inputs (tt is 64-bit)");
        for (int j = 0; j < m.nLeaves; ++j) {
            int id = m.leaf[j];
            if (id < 0 || id >= (int)_nodes.size())
                return blif_fail("LUT leaf id out of range");
            if (_nodes[id].isConst)
                need_const0 = true;
        }
    }
    for (int po : _pos) {
        if (po < 0 || po >= (int)_nodes.size() || !_nodes[po].isPo)
            return blif_fail("bad PO id");
        int d = _nodes[po].input_a;
        if (d < 0 || d >= (int)_nodes.size())
            return blif_fail("PO driver out of range");
        if (_nodes[d].isConst)
            need_const0 = true;
    }

    out << ".model " << blif_model_name(path) << '\n';
    out << ".inputs";
    for (int pi : _pis)
        out << " n" << pi;
    out << '\n';
    out << ".outputs";
    for (int po : _pos)
        out << " n" << po;
    out << '\n';

    if (need_const0) {
        out << ".names n0\n";
        out << " 0\n";
    }

    for (const MappedLut& m : _mapped_luts) {
        out << ".names";
        for (int j = 0; j < m.nLeaves; ++j)
            out << " n" << m.leaf[j];
        out << " n" << m.root << '\n';
        write_onset(out, m.nLeaves, m.tt);
    }

    // One buffer/inverter per PO so .outputs are dedicated nets and invert_a is honored.
    for (int po : _pos) {
        int d = _nodes[po].input_a;
        bool inv = _nodes[po].invert_a;
        out << ".names n" << d << " n" << po << '\n';
        out << (inv ? '0' : '1') << " 1\n";
    }

    out << ".end\n";
    if (!out)
        return blif_fail(std::string("write failed: ") + path);
    return true;
}

static bool bench_fail(const std::string& msg) {
    std::cerr << "bench: " << msg << '\n';
    return false;
}

bool aigGraph::write_bench(const char* path) const {
    if (!path || !*path)
        return bench_fail("empty write path");
    if (!_has_mapping)
        return bench_fail("map has not been run");

    bool need_const0 = false;
    for (const MappedLut& m : _mapped_luts) {
        if (m.nLeaves < 0 || m.nLeaves > 6)
            return bench_fail("LUT has more than 6 inputs");
        for (int j = 0; j < m.nLeaves; ++j) {
            int id = m.leaf[j];
            if (id < 0 || id >= (int)_nodes.size())
                return bench_fail("LUT leaf id out of range");
            if (_nodes[id].isConst)
                need_const0 = true;
        }
    }
    for (int po : _pos) {
        if (po < 0 || po >= (int)_nodes.size() || !_nodes[po].isPo)
            return bench_fail("bad PO id");
        int d = _nodes[po].input_a;
        if (d < 0 || d >= (int)_nodes.size())
            return bench_fail("PO driver out of range");
        if (_nodes[d].isConst)
            need_const0 = true;
    }

    std::vector<int> cells;
    std::vector<char> is_cell((int)_nodes.size(), 0);
    auto add_cell = [&](int id) -> bool {
        if (id < 0 || id >= (int)_nodes.size())
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
    for (const MappedLut& m : _mapped_luts) {
        if (!add_cell(m.root))
            return bench_fail("LUT root id out of range");
    }
    for (int po : _pos) {
        if (!add_cell(po))
            return bench_fail("bad PO id");
    }
    if (cells.empty())
        return bench_fail("no cells to place");

    std::vector<std::vector<int>> net_pins((int)_nodes.size());
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
    for (const MappedLut& m : _mapped_luts) {
        if (!add_pin(m.root, m.root))
            return bench_fail("LUT output net");
        for (int j = 0; j < m.nLeaves; ++j) {
            if (!add_pin(m.leaf[j], m.root))
                return bench_fail("LUT leaf is not a PI, const, or mapped LUT");
        }
    }
    for (int po : _pos) {
        int d = _nodes[po].input_a;
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
        out << "CELL n" << id << " " << cell_w << " " << cell_h << "\n";
    for (int net = 0; net < (int)net_pins.size(); ++net) {
        if ((int)net_pins[net].size() < 2)
            continue;
        out << "NET n" << net;
        for (int cell : net_pins[net])
            out << " n" << cell;
        out << "\n";
    }
    if (!out)
        return bench_fail(std::string("write failed: ") + path);
    return true;
}

//Write blif and write bench are mostly generated.
