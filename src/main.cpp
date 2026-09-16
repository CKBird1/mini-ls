#include "aig.hpp"

#include <iostream>

static void usage(const char* argv0) {
    std::cerr << "usage: " << argv0 << " <in.aig> [out.aig]\n";
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        usage(argv[0]);
        return 1;
    }

    aigGraph g;
    if (!g.read_aiger(argv[1])) {
        return 1;
    }

    g.clean_dangling();

    g.balance();

    g.print_stats();

    if (argc == 3 && !g.write_aiger(argv[2])) {
        return 1;
    }
    return 0;
}
