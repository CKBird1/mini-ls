#include "aig.hpp"
#include "rwlib.hpp"
#include <iostream>
#include <vector>
#include <queue>

void aigGraph::rebuild_order() {
    _kahns.clear();
    std::vector<int> indegrees(_nodes.size());
    std::queue<int> to_add;
    for(int i = 0; i < (int)_nodes.size(); ++i) {
        if(!_nodes[i].is_and()) continue;
        int indegree = 0;
        int fanin = _nodes[i].input_a;
        if(_nodes[fanin].is_and()) indegree++;
        fanin = _nodes[i].input_b;
        if(_nodes[fanin].is_and()) indegree++;
        indegrees[i] = indegree;
        if(indegree == 0) to_add.push(i);
    }

    //to_add now has all nodes only driven by PIs, start adding them to _kahns one at a time
    //Decrement the indegree of any fanouts that this drives, and if any hit 0 add to to_add

    while(!to_add.empty()) {
        int nid = to_add.front();
        to_add.pop();
        _kahns.push_back(nid);
        for(const auto& fo : _nodes[nid].fanouts) {
            if(!_nodes[fo].is_and()) continue;
            indegrees[fo]--;
            if(indegrees[fo] == 0) to_add.push(fo);
        }
    }
    //_kahns should now be properly sorted by topo order
}

Cut aigGraph::upper_cut(Cut ca, Cut cb) {
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
    Cut new_cut;
    if(index > 4) {
        new_cut.nLeaves = 0;
        return new_cut;
    }
    new_cut.nLeaves = index;
    i = 0;
    while(i < index) { new_cut.leaf[i] = out[i]; ++i; }
    return new_cut;
}

bool aigGraph::same_cut(Cut ca, Cut cb) {
    if(ca.nLeaves != cb.nLeaves) return false;
    else {
        for(int i = 0; i < ca.nLeaves; ++i) {
            if(ca.leaf[i] != cb.leaf[i]) return false;
        }
    }
    return true;
}

void aigGraph::load_PIs(std::vector<std::vector<Cut>>& cuts_by_node) {
    //load all the PIs into the cuts_by_node vector so that the topo-sort order calls 
    //to enumerate cuts has something to jump off of
    for(int i = 0; i < (int)_nodes.size(); ++i) {
        if(_nodes[i].isPi) {
            Cut c;
            c.nLeaves = 1;
            c.leaf[0] = i;
            cuts_by_node[i].push_back(c);
        }
    }
}

void aigGraph::enumerate_cuts(std::vector<std::vector<Cut>>& cuts_by_node, int nid) {  
    Cut cut;
    cut.nLeaves = 1; 
    cut.leaf[0] = nid;
    cuts_by_node[nid].push_back(cut);
    int adex = _nodes[nid].input_a;
    int bdex = _nodes[nid].input_b;
    for(int j = 0; (std::size_t)j < cuts_by_node[adex].size(); ++j) {
        bool full = false;
        for(int k = 0; (std::size_t)k < cuts_by_node[bdex].size(); ++k) {
            //work with cuts_by_node[adex][j] and cuts_by_node[bdex][k] to make a new union each loop
            Cut ca = cuts_by_node[adex][j];
            Cut cb = cuts_by_node[bdex][k];
            Cut nc = upper_cut(ca, cb);
            if(nc.nLeaves == 0) continue;
            
            //Now loop over cuts_by_node[nid] and check against new cut nc
            bool same = false;
            for(int l = 0; l < (int)cuts_by_node[nid].size(); ++l) {
                same = same_cut(nc, cuts_by_node[nid][l]);
                if(same) break;
            }
            if(!same) cuts_by_node[nid].push_back(nc); 
            if(cuts_by_node[nid].size() >= 9) { //Allow unit cut to stay, 8 useful cuts
                full = true;
                break;                   
            }
        }
        if(full) break;
    }
}

std::uint16_t aigGraph::cut_tt(Cut c, int id, bool& ok) {
    std::vector<int> starters = {0xAAAA, 0xCCCC, 0xF0F0, 0xFF00}; //Initialize 4-bit truth-table worthy inputs to each leaf
    std::vector<int> tts((int)_nodes.size(), -1);
    ok = true;
    for(int i = 0; i < c.nLeaves; ++i) {
        int leaf = c.leaf[i];
        if (leaf < 0 || (std::size_t)leaf >= tts.size()) {
            ok = false;
            return 0;
        }
        tts[leaf] = starters[i];
    }

    std::uint16_t curr_tt = eval_tt(tts, id, ok); //Recursive call, it travels 'up' the fanins until it hits the leaves, then back down to build this tt
    return curr_tt;
}


std::uint16_t aigGraph::eval_tt(std::vector<int> &tts, int id, bool& ok) {
    if (!ok) return 0;
    if (id < 0 || (std::size_t)id >= tts.size() || (std::size_t)id >= _nodes.size()) {
        ok = false;
        return 0;
    }
    if (tts[id] != -1) return tts[id];
    if (!_nodes[id].is_and()) {
        ok = false;
        return 0;
    }

    int fanina = eval_tt(tts, _nodes[id].input_a, ok);
    if(_nodes[id].invert_a) fanina = (~fanina) & 0xFFFF; //Invert and then mask the upper 16
    int faninb = eval_tt(tts, _nodes[id].input_b, ok);
    if(_nodes[id].invert_b) faninb = (~faninb) & 0xFFFF;
    int result = fanina & faninb;
    tts[id] = result;
    return result;
}

NPN npn_canon(std::uint16_t orig_tt) {
    std::uint16_t best = 0xFFFF;
    int best_perm[4], best_mask[4], best_neg = 0;

    //Now loop over all 3 of those nested, and inside each rebuild the 16 bits, compare to best, and then decide to keep or not
    for(int m = 0; m < 16; ++m) { //The 16 masks for bit inversion
        for(int n = 0; n <= 1; ++n) { //Negate or not
            for(int a = 0; a < 4; ++a) { //nested loops for abcd perms (which permutation of 0 1 2 3)
                for(int b = 0; b < 4; ++b) {
                    if(a == b) continue;
                    for(int c = 0; c < 4; ++c) {
                        if(a == c || b == c) continue;
                        for(int d = 0; d < 4; ++d) {
                            if(a == d || b == d || c == d) continue;        
                            int neg_in[4]; //Decide which bits are inverted
                            for(int v = 0; v < 4; ++v) {
                                neg_in[v] = (m >> v) & 1;
                            }
                        
                            int perm[4] = {a, b, c, d};
                            std::uint16_t curr = 0;
                            for(int j = 0; j < 16; ++j) { //Update each bit based on inversion mask + perm abcd + negate
                                int y[4];
                                int x[4];
                                for(int k = 0; k < 4; ++k) { //k at minterm j, write to perm[k], then xor invert
                                    y[k] = (j >> k) & 1;
                                    x[perm[k]] = y[k] ^ neg_in[perm[k]];
                                }
                                int i = x[0] | (x[1] << 1) | (x[2] << 2) | (x[3] << 3); //create 4 bit value in i
                                int bit = (orig_tt >> i) & 1; //get the original bit in this position
                                if(n) bit ^= 1; //Invert if needed
                                curr |= (bit << j); //set current[correctBit] 
                            }

                            if(curr < best) {
                                for(int i = 0; i < 4; ++i) {
                                    best_perm[i] = perm[i];
                                    best_mask[i] = neg_in[i];
                                }
                                best_neg = n;
                                best = curr;
                            }
                        }
                    }
                }
            }
        }
    }
    NPN npn;
    for(int i = 0; i < 4; ++i) {
        npn.best_perm[i] = best_perm[i];
        npn.best_mask[i] = best_mask[i];
    }
    npn.best_neg = best_neg;
    npn.canon = best;
    return npn;
}

bool aigGraph::mffc_process_node(int nid, std::vector<int>& nof, Cut c) {
    //return true if we should process this node next, otherwise add/decrement from fanouts remaining
    if(_nodes[nid].is_pi_or_const() || _nodes[nid].tombstone) return false;
    for(int i = 0; i < (int)c.nLeaves; ++i) if(nid == c.leaf[i]) return false;
    if(nof[nid] == -1) nof[nid] = (int)_nodes[nid].fanouts.size() - 1;
    else nof[nid] = nof[nid] - 1;
    
    if(nof[nid] == 0) return true;
    
    return false;
}

int aigGraph::mffc_size(int nid, Cut c) {
    //Nodes that die if nid is replaced
    //walk toward PI along fanins -> if each fanin has fanout size of 1, we're guaranteed it's within the cone
    //Stop walking a given path when fanout > 1 because it's driving something outside this cone, it's now a leaf
    //doesn't really matter how many leaves we find, just need to know how many nodes are no longer used

    int num_nodes_removed = 0;
    std::vector<int> number_of_fanouts((int)_nodes.size(), -1); //If a node is processed and eventually has 0 fanouts, put it into nodes to process as a non-leaf. 
    std::vector<int> nodes_to_process;
    nodes_to_process.push_back(nid); //Seed the worklist

    while(nodes_to_process.size() != 0) {
        int cNode = nodes_to_process.back();
        nodes_to_process.pop_back();
        num_nodes_removed++;

        int fanin = _nodes[cNode].input_a;
        if(mffc_process_node(fanin, number_of_fanouts, c)) nodes_to_process.push_back(fanin);
         

        fanin = _nodes[cNode].input_b;
        if(mffc_process_node(fanin, number_of_fanouts, c)) nodes_to_process.push_back(fanin);
    }    

    return num_nodes_removed;
}

void aigGraph::map_npn_leaves(const Cut& cut, const NPN& cut_npn, std::uint32_t leaf_lit[4]) {
    // Can now assume canon because the generator outputs canon
    for (int i = 0; i < 4; ++i) {
        int p = cut_npn.best_perm[i];
        leaf_lit[i] = (p < cut.nLeaves) ? make_lit(cut.leaf[p], cut_npn.best_mask[p]) : 0;
    }

    /*
     for load_hand only, this was a workaround for the way I chose to write
     load_hand rather than the 'correct' solution which I waited for graph
     generation to make (above)

     int inv_r[4];
     for (int k = 0; k < 4; ++k)
         inv_r[g.npn.best_perm[k]] = k;
     for (int j = 0; j < 4; ++j) {
         int k = inv_r[j];
         int p = cut_npn.best_perm[k];
         leaf_lit[j] = (p < cut.nLeaves)
             ? make_lit(cut.leaf[p], cut_npn.best_mask[p] ^ g.npn.best_mask[j])
             : 0;
     }
     */
}

std::uint32_t aigGraph::build_rwgraph(const RwGraph& g, const std::uint32_t leaf_lit[4]) {
    std::uint32_t node_lit[12];
    for (int i = 0; i < 4; ++i)
        node_lit[i] = leaf_lit[i];

    auto rec_to_net = [&](std::uint32_t rec) {
        return node_lit[lit_id(rec)] ^ (std::uint32_t)lit_inv(rec);
    };

    for (int i = 0; i < g.nAnds; ++i) {
        std::uint32_t a = rec_to_net(g.fanin0[i]);
        std::uint32_t b = rec_to_net(g.fanin1[i]);
        node_lit[4 + i] = create_and(lit_id(a), lit_inv(a), lit_id(b), lit_inv(b));
    }
    return rec_to_net(g.root);
}

void aigGraph::swing(int nid, std::uint32_t new_root) {
    //int nid is the OLD node that is going to be disconnected, all of its fanouts need to be transferred over to the new_root
    //also, all the fanins of the items on that fanout list need to be rewritted to new_root assuming they used to be nid (don't change non-nid fanins)
    //new_root needs to be converted out of literal: id -> lit_id(new_root), inv -> lit_inv(new_root) XOR old_invert

    int new_id = lit_id(new_root);
    bool new_inv = lit_inv(new_root);

    std::vector<int> old_fanouts = _nodes[nid].fanouts;
    _nodes[nid].fanouts.clear();
    _nodes[new_id].fanouts.insert(_nodes[new_id].fanouts.end(), old_fanouts.begin(), old_fanouts.end());

    for(int i = 0; i < (int)old_fanouts.size(); ++i) {
        int user = old_fanouts[i];
        int in_a = _nodes[user].input_a;
        int in_b = _nodes[user].input_b;

        if(!_nodes[user].isPo) {
            _hashedNodes.erase(and_key(user));
        }
        if((in_a != -1) && (in_a == nid)) {
            _nodes[user].input_a = new_id;
            _nodes[user].invert_a ^= new_inv;
        }
        if((in_b != -1) && (in_b == nid)) {
            _nodes[user].input_b = new_id;
            _nodes[user].invert_b ^= new_inv;
        }
         if(!_nodes[user].isPo) {
            _hashedNodes[and_key(user)] = user;
        }
    }
}

static bool in_tfo(const std::vector<aigNode>& nodes, int src, int dst) {
    if (src == dst) return true;
    if (src < 0 || dst < 0) return false;
    std::vector<char> seen(nodes.size(), 0);
    std::vector<int> w;
    w.push_back(src);
    seen[(std::size_t)src] = 1;
    for (std::size_t i = 0; i < w.size(); ++i) {
        int id = w[i];
        if ((std::size_t)id >= nodes.size()) continue;
        for (int u : nodes[(std::size_t)id].fanouts) {
            if (u < 0 || (std::size_t)u >= nodes.size() || seen[(std::size_t)u])
                continue;
            if (u == dst) return true;
            seen[(std::size_t)u] = 1;
            w.push_back(u);
        }
    }
    return false;
}

void aigGraph::rollback_rwgraph(int mark) {
    // Remove all trial nodes from _nodes, _hashedNodes, and fanouts
    while ((int)_nodes.size() > mark) {
        int id = (int)_nodes.size() - 1;
        _hashedNodes.erase(and_key(id));
        int a = _nodes[id].input_a;
        int b = _nodes[id].input_b;
        if (a >= 0) {
            std::vector<int>& fo = _nodes[a].fanouts;
            for (int i = (int)fo.size() - 1; i >= 0; --i)
                if (fo[i] == id) fo.erase(fo.begin() + i);
        }
        if (b >= 0) {
            std::vector<int>& fo = _nodes[b].fanouts;
            for (int i = (int)fo.size() - 1; i >= 0; --i)
                if (fo[i] == id) fo.erase(fo.begin() + i);
        }
        _nodes.pop_back();
    }
}

void aigGraph::check_delete(int nid, std::vector<int>& node_is_dead) {    
    bool kill = true;
    int fanin = _nodes[nid].input_a;
    if(_nodes[fanin].is_and()) {
        for(int i = 0; i < (int)_nodes[fanin].fanouts.size(); ++i) {
            if(!_nodes[_nodes[fanin].fanouts[i]].tombstone) {
                kill = false;
                break; //This is not dead, cannot delete
            }
        }
        if(kill) {
            node_is_dead.push_back(fanin);
            _nodes[fanin].tombstone = true;
            _hashedNodes.erase(and_key(fanin));
        }
    }
    kill = true;
    fanin = _nodes[nid].input_b;
    if(_nodes[fanin].is_and()) {   
        for(int i = 0; i < (int)_nodes[fanin].fanouts.size(); ++i) {
            if(!_nodes[_nodes[fanin].fanouts[i]].tombstone) {
                kill = false;
                break; //This is not dead, cannot delete
            }
        }
        if(kill) {
            node_is_dead.push_back(fanin);
            _nodes[fanin].tombstone = true;
            _hashedNodes.erase(and_key(fanin));
        }
    }
}

void aigGraph::clean_mffc(int nid) {
    //Old cone still needs to be fully removed so we don't accidentally use it later
    std::vector<int> node_is_dead;
    node_is_dead.push_back(nid);
    _nodes[nid].tombstone = true;
    _hashedNodes.erase(and_key(nid));

    while(!node_is_dead.empty()) {
        int cid = node_is_dead.back();
        node_is_dead.pop_back();
        check_delete(cid, node_is_dead);
    }
}

void aigGraph::rewrite() {
    rebuild_fanouts();
    RwLib& lib = RwLib::instance();

    std::vector<std::vector<Cut>> cuts_by_node(_nodes.size());
    load_PIs(cuts_by_node);
    rebuild_order();

    int hits = 0;
    for(int kid = 0; (std::size_t)kid < _kahns.size(); ++kid) {
        int nid = _kahns[kid];
        if(_nodes[nid].tombstone) continue;
        //Other checks no longer needed since _kahns never have po/pi/const/tombstone
        enumerate_cuts(cuts_by_node, nid);

        int deleted; //Will always be initialized later before use
        int best_gain = 0;
        int best_n = 0;
        const RwGraph* best_rwgraph = nullptr;
        std::uint32_t best_leaf_lit[4];

        for(int cid = 0; (std::size_t)cid < cuts_by_node[nid].size(); ++cid) {
            Cut& curr_cut = cuts_by_node[nid][cid];
            if(curr_cut.nLeaves == 1 && curr_cut.leaf[0] == nid) continue;
            
            //calc deleted for this specific cut
            deleted = mffc_size(nid, curr_cut);
            
            bool ok = true;
            std::uint16_t new_tt = cut_tt(curr_cut, nid, ok);
            if (!ok) continue;
            curr_cut.tt = new_tt;
            NPN npn = npn_canon(curr_cut.tt);

            const std::vector<RwGraph>* graphs = lib.find(npn.canon);
            if (!graphs) continue;
            for (const RwGraph& g : *graphs) {
                std::uint32_t leaf_lit[4];
                map_npn_leaves(curr_cut, npn, leaf_lit);

                int before = (int)_nodes.size();
                std::uint32_t new_root = build_rwgraph(g, leaf_lit);
                new_root ^= (std::uint32_t)npn.best_neg;
                int added = (int)_nodes.size() - before;
                int gain = deleted - added;

                // Check if what we're trying to swing in will create a cycle or no-op
                if (in_tfo(_nodes, nid, lit_id(new_root))) {
                    rollback_rwgraph(before);
                    continue;
                }

                if (gain > best_gain) {
                    best_gain = gain;
                    best_n = (std::uint32_t)npn.best_neg;
                    best_rwgraph = &g;
                    for(int p = 0; p < 4; p++) best_leaf_lit[p] = leaf_lit[p];
                }
                ++hits;
                rollback_rwgraph(before);
            }
        }
        if (best_gain > 0) {
            int mark = _nodes.size();
            std::uint32_t final_root = build_rwgraph(*best_rwgraph, best_leaf_lit);
            final_root ^= best_n;
            swing(nid, final_root);
            clean_mffc(nid);
            cuts_by_node.resize(_nodes.size());
            for(int newid = mark; newid < (int)_nodes.size(); ++newid) {
                if(!_nodes[newid].is_and()) continue;
                enumerate_cuts(cuts_by_node, newid);
            }
        }
    }
    clean_dangling();
    std::cout << "npn classes = " << lib.num_classes()
              << "  rwlib classes = " << lib.size()
              << "  matched cuts = " << hits << '\n';
}