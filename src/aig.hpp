#pragma once
#include <vector>
#include <unordered_map>
#include <cstdint>

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

        void print_stats();
        int max_lev();
        void clean_dangling();

    private:
        std::vector<aigNode> _nodes;
        std::vector<int> _pis;
        std::vector<int> _pos;
        std::unordered_map<std::uint64_t, int> _hashedNodes;

        std::uint64_t make_lit(int index);

};