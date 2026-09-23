#include "aig.hpp"
#include "rwlib.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static const char* kNpnPath = "data/npn4.txt";

static void usage(const char* argv0) {
    std::cerr << "usage: " << argv0 << " <in.aig> [out.aig] [-c <cmds>]\n"
              << "       " << argv0 << " --gen-npn <file>\n"
              << "  -c, --commands   space-separated commands, run in order (repeatable)\n"
              << "  commands:        balance, rewrite\n"
              << "  --gen-npn FILE   write NPN class table and exit\n"
              << "  --npn FILE       class table for rewrite (default data/npn4.txt)\n";
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
    return cmd == "balance" || cmd == "rewrite";
}

static void run_command(aigGraph& g, const std::string& cmd) {
    if (cmd == "balance")
        g.balance();
    else if (cmd == "rewrite")
        g.rewrite();
}

int main(int argc, char** argv) {
    const char* in_path = nullptr;
    const char* out_path = nullptr;
    const char* gen_npn_path = nullptr;
    const char* npn_path = kNpnPath;
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
        if (a == "--npn") {
            if (i + 1 >= argc) {
                std::cerr << "error: " << a << " requires an argument\n";
                usage(argv[0]);
                return 1;
            }
            npn_path = argv[++i];
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
    if (want_rewrite)
        RwLib::instance().load_npn(npn_path);

    aigGraph g;
    if (!g.read_aiger(in_path))
        return 1;

    g.clean_dangling();

    for (const auto& cmd : commands)
        run_command(g, cmd);

    g.print_stats();

    if (out_path && !g.write_aiger(out_path))
        return 1;
    return 0;
}
