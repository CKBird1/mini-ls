#include "sdc.hpp"
#include "aig.hpp"
#include <algorithm>
#include <iostream>

#include <climits>

//Leaving everything here instead of 'inline' for now easier to work with. will probably move later

void Constraints::set_period(int n) {
    _period = n;
}

void Constraints::set_max_delay(int po, int n) {
    _max_delay[po] = n;
}

void Constraints::set_false_path(int po) {
    _false_path.insert(po);
}

void Constraints::set_dont_touch(int nid) {
    _dont_touch.insert(nid);
}

int Constraints::get_period() const {
    return _period;
}

bool Constraints::has_timing() const {
    //Recognition given to how certain combinations of constraints should in theory
    //turn this off. That's a mapper-side issue that I won't add for now.
    if(_period >= 0 || (int)_max_delay.size() > 0) return true;
    return false;
}

int Constraints::po_cap(int po) const {
    if(is_false_path(po)) return INT_MAX;
    else {
        bool per_set = (_period != -1);
        auto it = _max_delay.find(po);
        bool max_set = it != _max_delay.end();
        if(per_set && max_set) {
            return std::min(_period, it->second);
        } else if(per_set)
            return _period;
        else if(max_set)
            return it->second;
    }
    return INT_MAX;
}

bool Constraints::is_false_path(int po) const {
    if(_false_path.find(po) != _false_path.end()) return true;
    return false;
}

bool Constraints::is_dont_touch(int nid) const {
    if(_dont_touch.find(nid) != _dont_touch.end()) return true;
    return false;
}

bool Constraints::validate(const aigGraph& g) const {
    for(const auto& md : _max_delay) {
        if(md.first >= g.num_pos() || md.first < 0) {
            std::cerr << "error: --max-delay PO index " << md.first
                      << " out of range (" << g.num_pos() << " POs)\n";
            return false;
        }
    }
    for(const auto& fp : _false_path) {
        if(fp >= g.num_pos() || fp < 0) {
            std::cerr << "error: --false-path PO index " << fp
                      << " out of range (" << g.num_pos() << " POs)\n";
            return false;
        }
    }
    for(const auto& dt : _dont_touch) {
        if(dt >= g.num_nodes() || dt < 0) {
            std::cerr << "error: --dont-touch node id " << dt
                      << " out of range (" << g.num_nodes() << " nodes)\n";
            return false;
        }
    }
    return true;
}

bool Constraints::bind_max_delay(const aigGraph& g, const std::string& name, int n) {
    int nid = g.id(name);
    if (nid < 0) {
        std::cerr << "error: sdc: unknown name '" << name << "'\n";
        return false;
    }
    int po = g.po_index(nid);
    if (po < 0) {
        std::cerr << "error: sdc: '" << name << "' is not a PO\n";
        return false;
    }
    set_max_delay(po, n);
    return true;
}

bool Constraints::bind_false_path(const aigGraph& g, const std::string& name) {
    int nid = g.id(name);
    if (nid < 0) {
        std::cerr << "error: sdc: unknown name '" << name << "'\n";
        return false;
    }
    int po = g.po_index(nid);
    if (po < 0) {
        std::cerr << "error: sdc: '" << name << "' is not a PO\n";
        return false;
    }
    set_false_path(po);
    return true;
}

bool Constraints::bind_dont_touch(const aigGraph& g, const std::string& name) {
    int nid = g.id(name);
    if (nid < 0) {
        std::cerr << "error: sdc: unknown name '" << name << "'\n";
        return false;
    }
    set_dont_touch(nid);
    return true;
}

bool Constraints::bind_max_delay_all_outputs(const aigGraph& g, int n) {
    for (int i = 0; i < g.num_pos(); ++i)
        set_max_delay(i, n);
    return true;
}

bool Constraints::bind_false_path_all_outputs(const aigGraph& g) {
    for (int i = 0; i < g.num_pos(); ++i)
        set_false_path(i);
    return true;
}
