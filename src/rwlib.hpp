#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

struct NPN {
    int best_perm[4];
    int best_mask[4];
    int best_neg;
    std::uint16_t canon;

    NPN() : best_perm{0, 1, 2, 3}, best_mask{0, 0, 0, 0}, best_neg(0), canon(0) {}
    NPN(int* bp, int* bm, int bn, std::uint16_t c) :
        best_neg(bn), canon(c) {
            for(int i = 0; i < 4; ++i) {
                best_perm[i] = bp[i];
                best_mask[i] = bm[i];
            }
        }
};

NPN npn_canon(std::uint16_t tt);

// Recipe ids 0, 1, 2, 3 are this graph's leaves a,b,c,d in slot order.
// npn is npn_canon of the function as drawn (slots 0,1,2,3), used to
// compose with the cut's NPN when instantiating. Fanins/root are make_lit.
struct RwGraph {
    int nAnds = 0;
    std::uint32_t fanin0[8] = {};
    std::uint32_t fanin1[8] = {};
    std::uint32_t root = 0;
    NPN npn;
};

// Now that all 222 have been dumped, read-only after load
class RwLib {
public:
    static RwLib& instance() {
        static RwLib lib;
        return lib;
    }

    void load_hand();
    bool generate_npn(const char* path);
    bool load_npn(const char* path);
    // 4-input AIGs up to max_ands ANDs. Keeps several cones per
    // NPN class, defined in src/rwgen.cpp
    bool generate_graphs(const char* path, int max_ands = 5);
    bool load_graphs(const char* path);

    const std::vector<RwGraph>* find(std::uint16_t tt) const {
        auto it = _graphs.find(tt);
        if (it == _graphs.end()) return nullptr;
        return &it->second;
    }

    std::size_t size() const { return _graphs.size(); }
    std::size_t num_classes() const { return _classes.size(); }
    const std::vector<std::uint16_t>& classes() const { return _classes; }

private:
    std::unordered_map<std::uint16_t, std::vector<RwGraph>> _graphs;
    std::vector<std::uint16_t> _classes;
};