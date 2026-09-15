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

    // Next: read argv[1] into an AIG and print_stats.
    std::cerr << "mini-ls: not implemented yet\n";
    (void)argv;
    return 1;
}
