#include "aig.hpp"

#include <iostream>

static void usage(const char* argv0) {
    std::cerr << "usage: " << argv0 << " <circuit.aig>\n";
}

int main(int argc, char** argv) {
    if (argc != 2) {
        usage(argv[0]);
        return 1;
    }

    aigGraph g;
    if (!g.read_aiger(argv[1])) {
        return 1;
    }

    g.clean_dangling();
    g.print_stats();
    return 0;
}
