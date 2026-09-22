#include "aig.hpp"
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

    std::uint32_t lit_a = make_lit(na, nainv);
    std::uint32_t lit_b = make_lit(nb, nbinv);

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
        return make_lit((int)it->second, false);
    }

    aigNode tmp(na, nainv, nb, nbinv);
    tmp.id = _nodes.size();
    tmp.level = std::max(_nodes[a].level, _nodes[b].level) + 1;
    _nodes.push_back(tmp);

    _nodes[a].fanouts.push_back(tmp.id);
    _nodes[b].fanouts.push_back(tmp.id);
    _hashedNodes[lookup] = tmp.id;

    return make_lit((int)tmp.id, false);
}

uint32_t aigGraph::create_pi() {
    aigNode tmp{};
    tmp.id = _nodes.size();
    _nodes.push_back(tmp);
    _pis.push_back(tmp.id);
    return make_lit((int)tmp.id, false);
}

uint32_t aigGraph::create_po(int a, bool ainv) {
    aigNode tmp(a, ainv);
    tmp.id = _nodes.size();
    tmp.level = _nodes[a].level; //Don't increment, just absorb from above
    _nodes.push_back(tmp);
    _pos.push_back(tmp.id);

    _nodes[a].fanouts.push_back(tmp.id);

    return make_lit((int)tmp.id, false);
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
            std::uint64_t lookup = and_key(s);
            _hashedNodes.erase(lookup);
        }
    }
}

std::uint64_t aigGraph::and_key(int index) {
    int na = _nodes[index].input_a, nb = _nodes[index].input_b;
    bool nainv = _nodes[index].invert_a, nbinv = _nodes[index].invert_b;

    std::uint32_t lit_a = make_lit(na, nainv);
    std::uint32_t lit_b = make_lit(nb, nbinv);

    if(lit_b <= lit_a) std::swap(lit_b, lit_a);

    uint64_t literal = (std::uint64_t)lit_a << 32 | lit_b;
    return literal;
}

