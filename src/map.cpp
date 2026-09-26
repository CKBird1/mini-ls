#include "aig.hpp"
#include <algorithm>

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
    while(i < ca.nLeaves && j < cb.nLeaves) {
        if(ca.leaf[i] == cb.leaf[j]) {
            out[index++] = ca.leaf[i];
            ++i; ++j;
        } else if(ca.leaf[i] < cb.leaf[j]) {
            out[index++] = ca.leaf[i++];
        } else {
            out[index++] = cb.leaf[j++];
        }
    }
    while(i < ca.nLeaves) out[index++] = ca.leaf[i++];
    while(j < cb.nLeaves) out[index++] = cb.leaf[j++];
    LutCut new_cut;
    if(index > k) {
        new_cut.nLeaves = 0;
        return new_cut;
    }
    new_cut.nLeaves = index;
    i = 0;
    while(i < index) { new_cut.leaf[i] = out[i]; ++i; }
    return new_cut;
}

void aigGraph::map(int k) {
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
}
