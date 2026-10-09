#pragma once
#include "name.hpp"
#include <vector>
#include <unordered_map>
#include <utility>
#include <cstdint>

enum class LutKind { Const, Pi, Lut, Po };

struct LutNode {
    int id = -1;
    LutKind kind = LutKind::Lut;
    int pin[8] = {};
    int nPins = 0;
    std::uint64_t tt = 0; //LUT only
    int driver = -1; //PO only
    bool invert = false; //PO only
    std::vector<int> fanouts;

    LutNode(const int* pinI, int np, std::uint64_t table) :
            nPins(np), tt(table) {
        if(nPins > 8) nPins = 8;
        for(int i = 0; i < nPins; ++i) {
            pin[i] = pinI[i];
        }
        //kind = LutKind::Lut;
    }; 
    
    LutNode(int d, bool inv) : driver(d), invert(inv) { 
        kind = LutKind::Po;
    };

    LutNode() { 
        kind = LutKind::Pi;
    };

    LutNode(bool /*is_const*/) {
        kind = LutKind::Const;
    };
};

class lutGraph {
    
    public:
        lutGraph();
        int create_lut(const int* pinI, int np, std::uint64_t table);
        int create_pi();
        int create_po(int d, bool invert);

        int num_nodes() const { return (int)_nodes.size(); };
        int num_pis() const { return (int)_pis.size(); };
        int num_pos() const { return (int)_pos.size(); };
        const std::string& name(int id) const { return _names.name(id); }
        int id(const std::string& name) const { return _names.id(name); }
        bool set_name(int id, const std::string& n) { return _names.set_name(id, n); };

        bool write_blif(const char* path) const;
        bool write_bench(const char* path) const;

    private:
        std::vector<LutNode> _nodes;
        std::vector<int> _pis;
        std::vector<int> _pos;
        NameMan _names;
};