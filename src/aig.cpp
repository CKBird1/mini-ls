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