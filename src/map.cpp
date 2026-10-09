#include "aig.hpp"
#include "sdc.hpp"
#include <algorithm>
#include <iostream>
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
        
        //Dont touch constraint means we can never use anything other than the identity cut
        //We must still populate all cuts properly, but we can never 'absorb' this into a lut
        //It must stay a lut root always. Kahns + pre-lead identity guarantees this always works
        int adex_max = !constraints.is_dont_touch(adex) ? (int)cuts_by_node[adex].size() : 1;
        int bdex_max = !constraints.is_dont_touch(bdex) ? (int)cuts_by_node[bdex].size() : 1;
        
        for(int j = 0; j < adex_max; ++j) {
            bool full = false;
            for(int l = 0; l < bdex_max; ++l) {
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

    LutGraph = lutGraph(); //Set it to empty
    aig_to_lut.clear();
    aig_to_lut.resize((std::size_t)_nodes.size(), -1);

    if(!populate_pis(LutGraph)) { std::cout << "error" << std::endl; }

    // Rewrite can make a cut leaf a larger AIG id than the root, so create
    // LUT cells only once every pin is already in aig_to_lut.
    bool progress = true;
    while (progress) {
        progress = false;
        for (int i = 0; i < (int)used.size(); ++i) {
            if (!used[i] || aig_to_lut[i] >= 0) continue;
            if ((int)cuts_by_node[i].size() < 2) continue;
            const LutCut& lc = cuts_by_node[i][1];
            if (add_lut_to_graph(LutGraph, i, lc.leaf, lc.nLeaves, lc.tt))
                progress = true;
        }
    }
    for (int i = 0; i < (int)used.size(); ++i) {
        if (used[i] && aig_to_lut[i] < 0)
            std::cout << "Error" << std::endl;
    }
    _has_mapping = true;

    if(!populate_pos(LutGraph)) { std::cout << "Error" << std::endl; }
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

bool aigGraph::populate_pis(lutGraph& l) {
    aig_to_lut[0] = 0;
    for(int i = 0; i < num_pis(); ++i) {
        int nid = _pis[i];
        int pi = l.create_pi();
        l.set_name(pi, name(nid));
        aig_to_lut[nid] = pi; 
    }
    return true;
}

bool aigGraph::populate_pos(lutGraph& l) {
    for(int i = 0; i < num_pos(); ++i) {
        int nid = _pos[i];
        int driver = _nodes[nid].input_a;
        bool invert = _nodes[nid].invert_a;
        if(aig_to_lut[driver] == -1) return false;
        int po = l.create_po(aig_to_lut[driver], invert);
        l.set_name(po, name(nid));
        aig_to_lut[nid] = po;
    }
    return true;
}

bool aigGraph::add_lut_to_graph(lutGraph& l, int root, const int* leaves, int nLeaves,
                               std::uint64_t tt) {
    int pins[8] = {};
    for (int i = 0; i < nLeaves; ++i) {
        int mapped = aig_to_lut[leaves[i]];
        if (mapped < 0) return false;
        pins[i] = mapped;
    }
    int lut = l.create_lut(pins, nLeaves, tt);
    l.set_name(lut, name(root));
    aig_to_lut[root] = lut;
    return true;
}

// File-local emit helper. Not an aigGraph method.
static bool emit_fail(const char* kind, const std::string& msg) {
    std::cerr << kind << ": " << msg << '\n';
    return false;
}

bool aigGraph::write_blif(const char* path) const {
    if (!_has_mapping)
        return emit_fail("blif", "map has not been run");
    return LutGraph.write_blif(path);
}

bool aigGraph::write_bench(const char* path) const {
    if (!_has_mapping)
        return emit_fail("bench", "map has not been run");
    return LutGraph.write_bench(path);
}
