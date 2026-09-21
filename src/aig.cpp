#include "aig.hpp"
#include <utility>
#include <iostream>
#include <queue>
#include <vector>

aigGraph::aigGraph() {
    aigNode const0(true);
    const0.id = _nodes.size();
    _nodes.push_back(const0);
}

uint32_t aigGraph::create_and(int a, bool ainv, int b, bool binv) {
    //Before creating the AND, lets normalize inputs so AND(a,b) is the same as AND(b,a)
    int na = a, nb = b;
    bool nainv = ainv, nbinv = binv;

    std::uint32_t lit_a = (uint32_t)na << 1 | (nainv ? 1u : 0u);
    std::uint32_t lit_b = (uint32_t)nb << 1 | (nbinv ? 1u : 0u);

    if(lit_b <= lit_a) {
        std::swap(na, nb);
        std::swap(nainv, nbinv);
        std::swap(lit_b, lit_a);
    }

    //We now have normalized input
    //Handle const-fold
    if(lit_a == 0) return 0; //x & constant 0, constant 0 always on left after canonicalize
    if(lit_a == 1) return lit_b; //x & 1, constant 1 always on left after canonicalize
    if(lit_a == lit_b) return lit_a; //x & x, return x
    if((lit_a ^ 1) == lit_b) return 0; //x & ~x, return 0

    //If we've made it here, check to see if this and already exists in the current nodes before duplicating it
    std::uint64_t lookup = (std::uint64_t)lit_a << 32 | lit_b;
    auto it = _hashedNodes.find(lookup);
    if(it != _hashedNodes.end()) {
        return (uint32_t)it->second << 1;
    }

    aigNode tmp(na, nainv, nb, nbinv);
    tmp.id = _nodes.size();
    tmp.level = std::max(_nodes[a].level, _nodes[b].level) + 1;
    _nodes.push_back(tmp);

    _nodes[a].fanouts.push_back(tmp.id);
    _nodes[b].fanouts.push_back(tmp.id);
    _hashedNodes[lookup] = tmp.id;

    return (uint32_t)tmp.id << 1;
}

uint32_t aigGraph::create_pi() {
    aigNode tmp{};
    tmp.id = _nodes.size();
    _nodes.push_back(tmp);
    _pis.push_back(tmp.id);
    return (uint32_t)tmp.id << 1;
}

uint32_t aigGraph::create_po(int a, bool ainv) {
    aigNode tmp(a, ainv);
    tmp.id = _nodes.size();
    tmp.level = _nodes[a].level; //Don't increment, just absorb from above
    _nodes.push_back(tmp);
    _pos.push_back(tmp.id);

    _nodes[a].fanouts.push_back(tmp.id);

    return (uint32_t)tmp.id << 1;
}

void aigGraph::balance() {
    //Step 1, iterate over all nodes, if not PO, add their personal literal to a new vector size of nodes
    std::vector<std::uint32_t> repl(_nodes.size());
    std::vector<int> old_fanout(_nodes.size());
    for(int i = 0; (std::size_t)i < _nodes.size(); ++i) {
        if(!_nodes[i].isPo) {
            repl[i] = (std::uint32_t) i << 1;
            old_fanout[i] = _nodes[i].fanouts.size();
        }
    }

    int n = (int)_nodes.size(); //Need to iterate over only old nodes, not new ones being added
    for(int i = 0; i < n; ++i) {
        if(_nodes[i].isPo || _nodes[i].isPi || _nodes[i].isConst || _nodes[i].tombstone) continue;

        auto is_leaf = [&](int id, bool inverted) {
            return (inverted || (_nodes[id].isPo || _nodes[id].isPi || _nodes[id].isConst) || old_fanout[id] != 1);
        };

        std::vector<std::uint32_t> leaves;
        std::vector<std::pair<int,bool>> to_check;

        //Seed
        to_check.push_back(std::make_pair(_nodes[i].input_a, _nodes[i].invert_a));
        to_check.push_back(std::make_pair(_nodes[i].input_b, _nodes[i].invert_b));

        //To_check is seeded, leaves is available for pushing, now go through the stack

        while(!to_check.empty()) {
            int next = to_check.back().first;
            bool inve = to_check.back().second;
            to_check.pop_back();
            if(is_leaf(next, inve)) {
                leaves.push_back(repl[next] ^ inve);
            } else {
                to_check.push_back(std::make_pair(_nodes[next].input_a, _nodes[next].invert_a));
                to_check.push_back(std::make_pair(_nodes[next].input_b, _nodes[next].invert_b));
            }
        }

        //Now use huffman to create_and pairs of ands to retain previous function
        //Use priority_queue as a min-heap, make sure I use level and literal
        //if there is only one leaf, skip using heap and just set repl[i] = leaves[0]
        //if more, populate min-heap by level and then start combining the shallowest pairs until done

        if(leaves.size() == 1) { repl[i] = leaves[0]; continue; }

        std::priority_queue<std::pair<int, std::uint32_t>,
                            std::vector<std::pair<int, std::uint32_t>>,
                            std::greater<std::pair<int, std::uint32_t>>> huffman;

        for(const auto& p : leaves) huffman.push(std::make_pair(_nodes[p >> 1].level, p));

        while(huffman.size() > 1) {
            std::uint32_t lita = huffman.top().second;
            huffman.pop();
            std::uint32_t litb = huffman.top().second;
            huffman.pop();

            std::uint32_t created = create_and(lita >> 1, lita & 1, litb >> 1, litb & 1);
            huffman.push(std::make_pair(_nodes[created>> 1].level, created));
        }

        //Now down to one item, simply submit
        repl[i] = huffman.top().second;
    }
    for(int j = 0; j < (int)_pos.size(); ++j) {
        int old_driver = _nodes[_pos[j]].input_a;
        bool old_invert = _nodes[_pos[j]].invert_a;
        std::uint32_t literal = repl[old_driver] ^ old_invert;
        _nodes[_pos[j]].input_a = literal >> 1;
        _nodes[_pos[j]].invert_a = literal & 1;
        _nodes[_pos[j]].level = _nodes[_nodes[_pos[j]].input_a].level;
    }
    clean_dangling();
}


void aigGraph::print_stats() {
    int lev = max_lev();

    std::cout << "i/o = " << num_pis() << "/" << num_pos()
              << "  and = " << num_ands() << " lev = " << lev << '\n';
}

int aigGraph::max_lev() {
    int lev = 0;
    for(const auto& po : _pos) {
        if(_nodes[po].level > lev) lev = _nodes[po].level;
    }
    return lev;
}

void aigGraph::clean_dangling() {
    std::vector<char> seen(_nodes.size(), 0);
    std::queue<int> work;
    for(std::size_t i = 0; i < _pos.size(); ++i) {
        work.push(_pos[(int)i]);
    }

    while(!work.empty()) {
        int id = work.front();
        work.pop();

        if(seen[id] == 1) continue;

        seen[id] = 1;
        if(_nodes[id].input_a >= 0) work.push(_nodes[id].input_a);
        if(_nodes[id].input_b >= 0) work.push(_nodes[id].input_b);
    }
    //After the above loop, seen will have a 1 in all id indexs that have been reached and processed
    //anywhere with a 0 is a dangling item to be pruned (in this case tombstoned)

    for(int s = 0; (std::size_t)s < _nodes.size(); ++s) {
        if(seen[s] == 0 && !_nodes[s].isPo && !_nodes[s].isPi && !_nodes[s].isConst) {
            _nodes[s].tombstone = true;
            //Now recreate key for the tombstoned node so it can be dropped from table
            std::uint64_t lookup = make_lit(s);
            _hashedNodes.erase(lookup);
        }
    }
}

std::uint64_t aigGraph::make_lit(int index) {
    int na = _nodes[index].input_a, nb = _nodes[index].input_b;
    bool nainv = _nodes[index].invert_a, nbinv = _nodes[index].invert_b;

    std::uint32_t lit_a = (std::uint32_t)na << 1 | (nainv ? 1u : 0u);
    std::uint32_t lit_b = (std::uint32_t)nb << 1 | (nbinv ? 1u : 0u);

    if(lit_b <= lit_a) std::swap(lit_b, lit_a);

    uint64_t literal = (std::uint64_t)lit_a << 32 | lit_b;
    return literal;
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

void aigGraph::enumerate_cuts(std::vector<std::vector<Cut>>& cuts_by_node) {
    for(int i = 0; (std::size_t)i < _nodes.size(); ++i) {
        if(_nodes[i].isPo || _nodes[i].isConst || _nodes[i].tombstone) continue;
        
        Cut cut;
        cut.nLeaves = 1; 
        cut.leaf[0] = i;
        cuts_by_node[i].push_back(cut);
        if(_nodes[i].isPi) continue;
        else {
            int adex = _nodes[i].input_a;
            int bdex = _nodes[i].input_b;
            for(int j = 0; (std::size_t)j < cuts_by_node[adex].size(); ++j) {
                for(int k = 0; (std::size_t)k < cuts_by_node[bdex].size(); ++k) {
                    //work with cuts_by_node[adex][j] and cuts_by_node[bdex][k] to make a new union each loop
                    Cut ca = cuts_by_node[adex][j];
                    Cut cb = cuts_by_node[bdex][k];
                    Cut nc = upper_cut(ca, cb);
                    if(nc.nLeaves == 0) continue;
                    
                    //Now loop over cuts_by_node[i] and check against new cut nc
                    bool same = false;
                    for(int l = 0; l < (int)cuts_by_node[i].size(); ++l) {
                        same = same_cut(nc, cuts_by_node[i][l]);
                        if(same) break;
                    }
                    if(!same) cuts_by_node[i].push_back(nc);                    
                }
            }
        }
    }
}

std::uint16_t aigGraph::cut_tt(Cut c, int id) {
    std::vector<int> starters = {0xAAAA, 0xCCCC, 0xF0F0, 0xFF00}; //Initialize 4-bit truth-table worthy inputs to each leaf
    std::vector<int> tts((int)_nodes.size(), -1);
    for(int i = 0; i < c.nLeaves; ++i) tts[c.leaf[i]] = starters[i];

    std::uint16_t curr_tt = eval_tt(tts, id); //Recursive call, it travels 'up' the fanins until it hits the leaves, then back down to build this tt
    return curr_tt;
}


std::uint16_t aigGraph::eval_tt(std::vector<int> &tts, int id) {
    if(tts[id] != -1) return tts[id];

    int fanina = eval_tt(tts, _nodes[id].input_a);
    if(_nodes[id].invert_a) fanina = (~fanina) & 0xFFFF; //Invert and then mask the upper 16
    int faninb = eval_tt(tts, _nodes[id].input_b);
    if(_nodes[id].invert_b) faninb = (~faninb) & 0xFFFF;
    int result = fanina & faninb;
    tts[id] = result;
    return result;
}

NPN aigGraph::canon_tt(Cut c) {
    std::uint16_t orig_tt = c.tt; //Make copy so we don't overwrite 
    std::uint16_t best = 0xFFFF;
    int best_perm[4], best_mask[4], best_neg;

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

void aigGraph::rewrite() {
    std::vector<std::vector<Cut>> cuts_by_node;
    cuts_by_node.resize(_nodes.size());
    enumerate_cuts(cuts_by_node); 
    for(int nid = 0; (std::size_t)nid < cuts_by_node.size(); ++nid) {
        if(_nodes[nid].isPi || _nodes[nid].isPo || _nodes[nid].isConst || _nodes[nid].tombstone) continue;
        for(int cid = 0; (std::size_t)cid < cuts_by_node[nid].size(); ++cid) {
            Cut& curr_cut = cuts_by_node[nid][cid];
            if(curr_cut.nLeaves == 1 && curr_cut.leaf[0] == nid) continue;
            std::uint16_t new_tt = cut_tt(curr_cut, nid);
            curr_cut.tt = new_tt;
            NPN npn = canon_tt(curr_cut);
        }
    }
}