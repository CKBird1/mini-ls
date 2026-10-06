#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

class aigGraph;

class Constraints {
public:
    void set_period(int n);
    void set_max_delay(int po, int n);
    void set_false_path(int po);
    void set_dont_touch(int nid);

    int get_period() const;
    bool has_timing() const;
    int  po_cap(int po) const;
    bool is_false_path(int po) const;
    bool is_dont_touch(int nid) const;

    bool validate(const aigGraph& g) const;

    bool bind_max_delay(const aigGraph& g, const std::string& name, int n);
    bool bind_false_path(const aigGraph& g, const std::string& name);
    bool bind_dont_touch(const aigGraph& g, const std::string& name);
    bool bind_max_delay_all_outputs(const aigGraph& g, int n);
    bool bind_false_path_all_outputs(const aigGraph& g);

private:
    int _period = -1;                         // -1 = no create_clock
    std::unordered_map<int,int> _max_delay;   // PO index -> cap
    std::unordered_set<int> _false_path;      // PO indices
    std::unordered_set<int> _dont_touch;      // AIG node ids
};

bool read_sdc(const char* path, const aigGraph& g, Constraints& sdc);
