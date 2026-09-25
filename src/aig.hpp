#pragma once
#include <vector>
#include <unordered_map>
#include <cstdint>

//node id in the high bits, complement in the least significant bit.
inline std::uint32_t make_lit(int id, bool inv) {
    return ((std::uint32_t)id << 1) | (std::uint32_t)inv;
}
inline int lit_id(std::uint32_t lit) {
    return (int)(lit >> 1);
}
inline bool lit_inv(std::uint32_t lit) {
    return (lit & 1u) != 0;
}

struct aigNode {
    int id;
    
    int input_a;
    bool invert_a; //True = inverted
    int input_b;
    bool invert_b;

    int level = 0;
    bool tombstone = false;

    std::vector<int> fanouts;
    bool isPi = false;
    bool isPo = false;
    bool isConst = false;

    aigNode(int a, bool ia, int b, bool ib) : 
        input_a(a), invert_a(ia), input_b(b), invert_b(ib) { }; //For generic and
    
    aigNode() { //pi
        input_a = -1; invert_a = false;
        input_b = -1; invert_b = false;
        isPi = true;
    }

    aigNode(int a, bool ia) : input_a(a), invert_a(ia) { //po
        input_b = -1; invert_b = false;
        isPo = true;
    }

    aigNode(bool /*is_const*/) { //Const creation
        input_a = -1; invert_a = false;
        input_b = -1; invert_b = false;
        isConst = true;
    }
};

struct Cut {
    int leaf[4] = {};
    int nLeaves = 0;
    std::uint16_t tt;
};

struct RwGraph;
struct NPN;

class aigGraph {
    
    public:
        aigGraph();
        
        uint32_t create_and(int a, bool ainv, int b, bool binv);
        uint32_t create_pi();
        uint32_t create_po(int a, bool ainv);

        bool read_aiger(const char* path);
        bool write_aiger(const char* path) const;

        int num_pis() const { return (int)_pis.size(); }
        int num_pos() const { return (int)_pos.size(); }
        int num_ands() const {
            int n = 0;
            for (const auto& node : _nodes) {
                if (!node.isPi && !node.isPo && !node.isConst && !node.tombstone) ++n;
            }
            return n;
        }

        void balance();
        void rewrite();

        void print_stats();
        int max_lev();
        void clean_dangling();

    private:
        std::vector<aigNode> _nodes;
        std::vector<int> _pis;
        std::vector<int> _pos;
        std::unordered_map<std::uint64_t, int> _hashedNodes;

        std::uint64_t and_key(int index);
        void rebuild_fanouts();
        void rebuild_order();
        std::vector<int> _kahns;

        Cut upper_cut(Cut ca, Cut cb);
        bool same_cut(Cut ca, Cut cb);
        void enumerate_cuts(std::vector<std::vector<Cut>>& cuts_by_node);
        std::uint16_t eval_tt(std::vector<int>& tts, int id, bool& ok);
        std::uint16_t cut_tt(Cut c, int id, bool& ok);
        bool mffc_process_node(int nid, std::vector<int>& nof);
        int mffc_size(int nid);
        void map_npn_leaves(const Cut& cut, const NPN& cut_npn, std::uint32_t leaf_lit[4]);
        std::uint32_t build_rwgraph(const RwGraph& g, const std::uint32_t leaf_lit[4]);
        void rollback_rwgraph(int mark);
        void check_delete(int nid, std::vector<int>& node_is_dead);
        void clean_mffc(int nid);
        void swing(int nid, std::uint32_t new_root);
};