#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

// Recipe ids 0,1,2,3 are the cut leaves a,b,c,d. Ids 4,5,... are ANDs in order.
// Fanins and root are make_lit(id, inv), same encoding as the rest of the AIG.
struct RwGraph {
    int nAnds = 0;
    std::uint32_t fanin0[8] = {};
    std::uint32_t fanin1[8] = {};
    std::uint32_t root = 0;
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
