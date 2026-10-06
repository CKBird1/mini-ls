#include "name.hpp"

void NameMan::grow_to(int n) {
    if((int)_names.size() >= n) return;
    _names.resize(n, "");
}

void NameMan::shrink_to(int n) {
    if(n < 0) n = 0;
    if(n >= (int)_names.size()) return;
    for(int i = (int)_names.size() - 1; i >= n; --i) {
        if(!_names[i].empty()) _id_of.erase(_names[i]);
    }
    _names.resize((std::size_t)n);
}

bool NameMan::set_name(int id, const std::string& name) {
    if(id < 0 || (int)_names.size() <= id || name.empty()) return false; //id out of range
    auto it = _id_of.find(name);
    if(it != _id_of.end() && it->second != id) return false; //name already bound
    
    if(!_names[id].empty() && _names[id] != name) _id_of.erase(_names[id]);
    _names[id] = name;
    _id_of[name] = id;
    return true;
}

bool NameMan::set_name(int id) {
    if(id < 0) return false;
    if((int)_names.size() <= id) grow_to(id+1);
    if(_names[id] != "") return true;

    std::string g_name = "n"+std::to_string(id);
    auto it = _id_of.find(g_name);
    if(it != _id_of.end()) return false;
    _id_of[g_name] = id;
    _names[id] = g_name;
    

    return true;
}

const std::string& NameMan::name(int id) const {
    if(id >= 0 && (id < (int)_names.size()) && (!_names[id].empty())) return _names[id];
    static const std::string empty;
    return empty;
}

int NameMan::id(const std::string& name) const {
    auto it = _id_of.find(name);
    if(it != _id_of.end()) return it->second;
    return -1;
}

bool NameMan::has_name(int id) const {
    return (id >= 0 && id < (int)_names.size() && (!_names[id].empty()));
}

bool NameMan::has(const std::string& name) const {
    auto it = _id_of.find(name);
    if(it == _id_of.end()) return false;
    return true;
}

int NameMan::size() const {
    return _names.size();
}
