#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// Instances of this owned per netlist.
class NameMan {
public:
    void grow_to(int n); //Make sure internal vector is always big enough
    void shrink_to(int n); //Drop suffix [n, size); unbind those names. rollback_rwgraph.
    bool set_name(int id, const std::string& name); //Set caller-supplied name. replaces current
    bool set_name(int id); //will generate a generic name 'n123'

    const std::string& name(int id) const;
    int  id(const std::string& name) const;
    bool has_name(int id) const;
    bool has(const std::string& name) const;
    int  size() const;

private:
    std::vector<std::string> _names;
    std::unordered_map<std::string, int> _id_of;
};
