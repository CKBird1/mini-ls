#include "aig.hpp"
#include <queue>
#include <vector>
#include <utility>

void aigGraph::balance() {
    //Step 1, iterate over all nodes, if not PO, add their personal literal to a new vector size of nodes
    std::vector<std::uint32_t> repl(_nodes.size());
    std::vector<int> old_fanout(_nodes.size());
    for(int i = 0; (std::size_t)i < _nodes.size(); ++i) {
        if(!_nodes[i].isPo) {
            repl[i] = make_lit(i, false);
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

        for(const auto& p : leaves) huffman.push(std::make_pair(_nodes[lit_id(p)].level, p));

        while(huffman.size() > 1) {
            std::uint32_t lita = huffman.top().second;
            huffman.pop();
            std::uint32_t litb = huffman.top().second;
            huffman.pop();

            std::uint32_t created = create_and(lit_id(lita), lit_inv(lita), lit_id(litb), lit_inv(litb));
            huffman.push(std::make_pair(_nodes[lit_id(created)].level, created));
        }

        //Now down to one item, simply submit
        repl[i] = huffman.top().second;
    }
    for(int j = 0; j < (int)_pos.size(); ++j) {
        int old_driver = _nodes[_pos[j]].input_a;
        bool old_invert = _nodes[_pos[j]].invert_a;
        std::uint32_t literal = repl[old_driver] ^ old_invert;
        _nodes[_pos[j]].input_a = lit_id(literal);
        _nodes[_pos[j]].invert_a = lit_inv(literal);
        _nodes[_pos[j]].level = _nodes[_nodes[_pos[j]].input_a].level;
    }
    clean_dangling();
}