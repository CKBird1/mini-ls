#include "aig.hpp"
#include "rwlib.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

static const char* kNpnPath = "data/npn4.txt";
static const char* kRwlibPath = "data/rwlib4.txt";

static void usage(const char* argv0) {
    std::cerr << "usage: " << argv0 << " <in.aig> [out.aig|out.blif|out.bench] [-c <cmds>]\n"
              << "       " << argv0 << " --gen-npn <file>\n"
              << "  -c, --commands   space-separated commands, run in order (repeatable)\n"
              << "  commands:        balance, rewrite, map\n"
              << "  -K N             LUT size for map (default 6, 2..6)\n"
              << "  --period N       required LUT depth at POs (omit = unconstrained)\n"
              << "  --max-delay i:N  cap PO i (0-based _pos order) to N LUT delays (repeatable)\n"
              << "  --gen-npn FILE   write NPN class table and exit\n"
              << "  --gen-rwlib FILE write generated 4-input subgraphs and exit\n"
              << "  --max-ands N     AND cap for --gen-rwlib (default 5, max 8)\n"
              << "  --npn FILE       class table for rewrite (default data/npn4.txt)\n"
              << "  --rwlib FILE     subgraph library for rewrite (default data/rwlib4.txt)\n";
} //If not self-explanitory, wanted to represent an abc-style "open netlist -> perform operations -> print_stats" loop.
//mini-ls path/to/design.aig outfile.aig -c "balance rewrite" ((<-- that command will call print_stats automatically at the end and close))
//There is a limitation where I cannot easily/cleanly open netlist and then wait for commands one at a time, it feels
//like a ui upgrade in the future, not for now


static std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    for (std::string tok; iss >> tok; )
        out.push_back(tok);
    return out;
}

static bool is_known_command(const std::string& cmd) {
    return cmd == "balance" || cmd == "rewrite" || cmd == "map";
}

static void run_command(aigGraph& g, const std::string& cmd, int lut_k, int period,
                        const std::vector<std::pair<int,int>>& max_delays) {
    if (cmd == "balance")
        g.balance();
    else if (cmd == "rewrite")
        g.rewrite();
    else if (cmd == "map")
        g.map(lut_k, period, max_delays);
}

int main(int argc, char** argv) {
    const char* in_path = nullptr;
    const char* out_path = nullptr;
    const char* gen_npn_path = nullptr;
    const char* gen_rwlib_path = nullptr;
    const char* npn_path = kNpnPath;
    const char* rwlib_path = kRwlibPath;
    int gen_max_ands = 5;
    int lut_k = 6;
    int period = -1;
    std::vector<std::pair<int,int>> max_delays;
    std::vector<std::string> commands;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        }
        if (a == "--gen-npn") {
            if (i + 1 >= argc) {
                std::cerr << "error: " << a << " requires an argument\n";
                usage(argv[0]);
                return 1;
            }
            gen_npn_path = argv[++i];
            continue;
        }
        if (a == "--gen-rwlib") {
            if (i + 1 >= argc) {
                std::cerr << "error: " << a << " requires an argument\n";
                usage(argv[0]);
                return 1;
            }
            gen_rwlib_path = argv[++i];
            continue;
        }
        if (a == "--max-ands") {
            if (i + 1 >= argc) {
                std::cerr << "error: " << a << " requires an argument\n";
                usage(argv[0]);
                return 1;
            }
            char* end = nullptr;
            long v = std::strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end || v < 1 || v > 8) {
                std::cerr << "error: --max-ands must be 1..8\n";
                return 1;
            }
            gen_max_ands = (int)v;
            continue;
        }
        if (a == "--npn") {
            if (i + 1 >= argc) {
                std::cerr << "error: " << a << " requires an argument\n";
                usage(argv[0]);
                return 1;
            }
            npn_path = argv[++i];
            continue;
        }
        if (a == "--rwlib") {
            if (i + 1 >= argc) {
                std::cerr << "error: " << a << " requires an argument\n";
                usage(argv[0]);
                return 1;
            }
            rwlib_path = argv[++i];
            continue;
        }
        if (a == "-K") {
            if (i + 1 >= argc) {
                std::cerr << "error: " << a << " requires an argument\n";
                usage(argv[0]);
                return 1;
            }
            char* end = nullptr;
            long v = std::strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end || v < 2 || v > 6) {
                if (v == 7 || v == 8) {
                    std::cerr << "error: -K " << v
                              << " is not supported; truth tables are 64-bit (use 2..6)\n";
                    return 1;
                }
                std::cerr << "error: -K must be 2..6\n";
                return 1;
            }
            lut_k = (int)v;
            continue;
        }
        if (a == "--period") {
            if (i + 1 >= argc) {
                std::cerr << "error: " << a << " requires an argument\n";
                usage(argv[0]);
                return 1;
            }
            char* end = nullptr;
            long v = std::strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end || v < 0) {
                std::cerr << "error: --period must be >= 0\n";
                return 1;
            }
            period = (int)v;
            continue;
        }
        if (a == "--max-delay") {
            if (i + 1 >= argc) {
                std::cerr << "error: " << a << " requires an argument\n";
                usage(argv[0]);
                return 1;
            }
            const char* spec = argv[++i];
            const char* colon = std::strchr(spec, ':');
            if (!colon || colon == spec || colon[1] == '\0') {
                std::cerr << "error: --max-delay expects i:N (PO index : delay)\n";
                return 1;
            }
            char* end = nullptr;
            long po = std::strtol(spec, &end, 10);
            if (end != colon || po < 0) {
                std::cerr << "error: --max-delay PO index must be >= 0\n";
                return 1;
            }
            long d = std::strtol(colon + 1, &end, 10);
            if (end == colon + 1 || *end || d < 0) {
                std::cerr << "error: --max-delay delay must be >= 0\n";
                return 1;
            }
            max_delays.push_back({(int)po, (int)d});
            continue;
        }
        if (a == "-c" || a == "--commands") {
            if (i + 1 >= argc) {
                std::cerr << "error: " << a << " requires an argument\n";
                usage(argv[0]);
                return 1;
            }
            const auto more = split_ws(argv[++i]);
            commands.insert(commands.end(), more.begin(), more.end());
            continue;
        }
        if (!a.empty() && a[0] == '-') {
            std::cerr << "error: unknown option " << a << "\n";
            usage(argv[0]);
            return 1;
        }
        if (!in_path)
            in_path = argv[i];
        else if (!out_path)
            out_path = argv[i];
        else {
            std::cerr << "error: unexpected argument " << a << "\n";
            usage(argv[0]);
            return 1;
        }
    }

    if (gen_npn_path) {
        RwLib& lib = RwLib::instance();
        if (!lib.generate_npn(gen_npn_path)) {
            if (lib.num_classes() == 0)
                std::cerr << "error: generate_npn produced no classes (fill RwLib::generate_npn)\n";
            else
                std::cerr << "error: cannot write NPN classes to '" << gen_npn_path << "'\n";
            return 1;
        }
        std::cout << "wrote " << lib.num_classes() << " NPN classes to " << gen_npn_path << '\n';
        return 0;
    }

    if (gen_rwlib_path) {
        RwLib& lib = RwLib::instance();
        if (!lib.generate_graphs(gen_rwlib_path, gen_max_ands)) {
            std::cerr << "error: generate_graphs produced nothing or cannot write '"
                      << gen_rwlib_path << "'\n";
            return 1;
        }
        std::cout << "wrote " << lib.size() << " NPN-class subgraphs to " << gen_rwlib_path << '\n';
        return 0;
    }

    if (!in_path) {
        usage(argv[0]);
        return 1;
    }

    for (const auto& cmd : commands) {
        if (!is_known_command(cmd)) {
            std::cerr << "error: unknown command '" << cmd << "'\n";
            usage(argv[0]);
            return 1;
        }
    }

    bool want_rewrite = false;
    for (const auto& cmd : commands) {
        if (cmd == "rewrite") want_rewrite = true;
    }
    if (want_rewrite) {
        RwLib& lib = RwLib::instance();
        if (!lib.load_npn(npn_path)) {
            std::cerr << "error: cannot load NPN classes from '" << npn_path << "'\n";
            return 1;
        }
        if (!lib.load_graphs(rwlib_path)) {
            std::cerr << "error: cannot load rewrite subgraphs from '" << rwlib_path << "'\n";
            return 1;
        }
    }

    aigGraph g;
    if (!g.read_aiger(in_path))
        return 1;

    g.clean_dangling();

    for (const auto& md : max_delays) {
        if (md.first >= g.num_pos()) {
            std::cerr << "error: --max-delay PO index " << md.first
                      << " out of range (" << g.num_pos() << " POs)\n";
            return 1;
        }
    }

    for (const auto& cmd : commands)
        run_command(g, cmd, lut_k, period, max_delays);

    g.print_stats();

    if (out_path) {
        const std::string out = out_path;
        auto ends_with = [&](const char* suf) {
            const std::size_t n = std::char_traits<char>::length(suf);
            return out.size() >= n && out.compare(out.size() - n, n, suf) == 0;
        };
        if (ends_with(".blif")) {
            if (!g.write_blif(out_path))
                return 1;
        } else if (ends_with(".bench")) {
            if (!g.write_bench(out_path))
                return 1;
        } else if (!g.write_aiger(out_path)) {
            return 1;
        }
    }
    return 0;
}
