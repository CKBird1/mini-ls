#include "aig.hpp"
#include <utility>
#include <iostream>
#include <queue>

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
        if(seen[s] == 0 && !_nodes[s].isPo && !_nodes[s].isPi && !_nodes[s].isConst) _nodes[s].tombstone = true;
    }
}