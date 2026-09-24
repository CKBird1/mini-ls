#include "aig.hpp"
#include "rwlib.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// This is mostly generated code, I walked through to understand and know how it works
// but for the learning/experimenting/implementing I am wanting to do, hand-writing this
// takes away from time I'd rather spend somewhere else (Like LUT mapper)

// Grow a 4-input AIG by adding ANDs, then record each new AND as a rewrite
// root. Leaves are ids 0,1,2,3 (a,b,c,d). AND i lives at id 4+i.
//
//   grow     -> add ANDs in a canonical order, recurse
//   consider -> TT of the newest AND (and ~AND), NPN class, keep if cheap
//   extract  -> drop ANDs not in the root's cone, compact ids to 4,5,...
//   relabel  -> permute/negate leaves so the stored TT is the canon TT
//
// Independent ANDs are added in increasing pair_key order so {A,B} is not
// searched as both A-then-B and B-then-A. Only the newest AND is classified;
// older roots were recorded when they were added (later ANDs are not in
// their cones).

static const int kMaxPerClass = 5;
static const int kAndSlack = 1;
static const char* kNpnPath = "data/npn4.txt";

struct GenState {
    std::uint32_t fanin0[8];
    std::uint32_t fanin1[8];
    int nAnds = 0;
    std::unordered_set<std::uint64_t> hashed;
};

static std::unordered_map<std::uint16_t, NPN> g_npn_cache;

static std::uint64_t pair_key(std::uint32_t a, std::uint32_t b) {
    if (b < a) {
        std::uint32_t t = a;
        a = b;
        b = t;
    }
    return ((std::uint64_t)a << 32) | b;
}

// Skip the same folds create_and would: &0, &1, x&x, x&~x.
static bool usable_and(std::uint32_t a, std::uint32_t b) {
    if (b < a) {
        std::uint32_t t = a;
        a = b;
        b = t;
    }
    if (a == 0 || a == 1) return false;
    if (a == b) return false;
    if ((a ^ 1u) == b) return false;
    return true;
}

static void eval_tts(const GenState& st, std::uint16_t tt[12]) {
    tt[0] = 0xAAAA;
    tt[1] = 0xCCCC;
    tt[2] = 0xF0F0;
    tt[3] = 0xFF00;
    for (int i = 0; i < st.nAnds; ++i) {
        std::uint32_t a = st.fanin0[i], b = st.fanin1[i];
        std::uint16_t ta = tt[lit_id(a)];
        if (lit_inv(a)) ta = (std::uint16_t)(~ta);
        std::uint16_t tb = tt[lit_id(b)];
        if (lit_inv(b)) tb = (std::uint16_t)(~tb);
        tt[4 + i] = (std::uint16_t)(ta & tb);
    }
}

static void mark_cone(const GenState& st, int id, char used[12]) {
    if (id < 4 || used[id]) return;
    used[id] = 1;
    int ai = id - 4;
    mark_cone(st, lit_id(st.fanin0[ai]), used);
    mark_cone(st, lit_id(st.fanin1[ai]), used);
}

static RwGraph extract_cone(const GenState& st, std::uint32_t root) {
    char used[12] = {};
    mark_cone(st, lit_id(root), used);

    int old_to_new[12];
    for (int i = 0; i < 4; ++i)
        old_to_new[i] = i;

    RwGraph g;
    int n = 0;
    for (int i = 0; i < st.nAnds; ++i) {
        if (!used[4 + i]) continue;
        old_to_new[4 + i] = 4 + n;
        auto remap = [&](std::uint32_t lit) {
            return make_lit(old_to_new[lit_id(lit)], lit_inv(lit));
        };
        g.fanin0[n] = remap(st.fanin0[i]);
        g.fanin1[n] = remap(st.fanin1[i]);
        ++n;
    }
    g.nAnds = n;
    g.root = make_lit(old_to_new[lit_id(root)], lit_inv(root));
    return g;
}

// Drawn slot s is original variable s. Canon variable k came from original
// perm[k]. Stored slot k is that original leaf, with input mask folded in.
// Output polarity is XOR'd onto the root. g.npn is identity + the class key
// so today's compose mapping still works (inv_r is identity).
static RwGraph relabel_canon(const RwGraph& drawn, const NPN& npn) {
    int inv_p[4];
    for (int k = 0; k < 4; ++k)
        inv_p[npn.best_perm[k]] = k;

    auto map_lit = [&](std::uint32_t lit) {
        int id = lit_id(lit);
        bool inv = lit_inv(lit);
        if (id < 4) {
            int k = inv_p[id];
            return make_lit(k, inv ^ (bool)npn.best_mask[id]);
        }
        return lit;
    };

    RwGraph g = drawn;
    for (int i = 0; i < g.nAnds; ++i) {
        g.fanin0[i] = map_lit(g.fanin0[i]);
        g.fanin1[i] = map_lit(g.fanin1[i]);
    }
    g.root = map_lit(g.root);
    if (npn.best_neg)
        g.root ^= 1u;
    g.npn = NPN();
    g.npn.canon = npn.canon;
    return g;
}

static const NPN& cached_npn(std::uint16_t tt) {
    auto it = g_npn_cache.find(tt);
    if (it != g_npn_cache.end()) return it->second;
    return g_npn_cache.insert({tt, npn_canon(tt)}).first->second;
}

static bool same_graph(const RwGraph& a, const RwGraph& b) {
    if (a.nAnds != b.nAnds || a.root != b.root) return false;
    for (int i = 0; i < a.nAnds; ++i) {
        if (a.fanin0[i] != b.fanin0[i] || a.fanin1[i] != b.fanin1[i])
            return false;
    }
    return true;
}

static void keep_graph(std::unordered_map<std::uint16_t, std::vector<RwGraph>>& graphs,
                       const RwGraph& g, std::uint16_t canon) {
    std::vector<RwGraph>& vec = graphs[canon];
    for (const RwGraph& e : vec) {
        if (same_graph(e, g)) return;
    }

    int best = g.nAnds;
    for (const RwGraph& e : vec) {
        if (e.nAnds < best) best = e.nAnds;
    }
    if (!vec.empty() && g.nAnds > best + kAndSlack) return;

    vec.push_back(g);

    best = vec[0].nAnds;
    for (const RwGraph& e : vec) {
        if (e.nAnds < best) best = e.nAnds;
    }
    int w = 0;
    for (int r = 0; r < (int)vec.size(); ++r) {
        if (vec[r].nAnds <= best + kAndSlack)
            vec[w++] = vec[r];
    }
    vec.resize((std::size_t)w);

    while ((int)vec.size() > kMaxPerClass) {
        int worst = 0;
        for (int i = 1; i < (int)vec.size(); ++i) {
            if (vec[i].nAnds > vec[worst].nAnds) worst = i;
        }
        vec.erase(vec.begin() + worst);
    }
}

static void consider_root(std::unordered_map<std::uint16_t, std::vector<RwGraph>>& graphs,
                          const GenState& st, std::uint32_t root, std::uint16_t tt) {
    if (lit_id(root) < 4) return;
    const NPN& npn = cached_npn(tt);
    RwGraph g = relabel_canon(extract_cone(st, root), npn);
    if (g.nAnds <= 0 || g.nAnds > 8) return;
    keep_graph(graphs, g, npn.canon);
}

static void grow(std::unordered_map<std::uint16_t, std::vector<RwGraph>>& graphs,
                 GenState& st, int max_ands) {
    if (st.nAnds > 0) {
        std::uint16_t tt[12];
        eval_tts(st, tt);
        int i = st.nAnds - 1;
        std::uint32_t id_lit = make_lit(4 + i, false);
        consider_root(graphs, st, id_lit, tt[4 + i]);
        consider_root(graphs, st, id_lit ^ 1u, (std::uint16_t)(~tt[4 + i]));
    }
    if (st.nAnds >= max_ands) return;

    int n = 4 + st.nAnds;
    int last_id = 3 + st.nAnds;
    std::uint64_t prev_key = 0;
    if (st.nAnds > 0)
        prev_key = pair_key(st.fanin0[st.nAnds - 1], st.fanin1[st.nAnds - 1]);

    for (int a = 0; a < n; ++a) {
        for (int b = 0; b < n; ++b) {
            for (int ia = 0; ia <= 1; ++ia) {
                for (int ib = 0; ib <= 1; ++ib) {
                    std::uint32_t la = make_lit(a, ia);
                    std::uint32_t lb = make_lit(b, ib);
                    if (!usable_and(la, lb)) continue;
                    std::uint64_t key = pair_key(la, lb);
                    if (st.hashed.count(key)) continue;
                    if (st.nAnds > 0) {
                        bool uses_last = (lit_id(la) == last_id || lit_id(lb) == last_id);
                        if (!uses_last && key <= prev_key) continue;
                    }

                    st.fanin0[st.nAnds] = la;
                    st.fanin1[st.nAnds] = lb;
                    st.hashed.insert(key);
                    ++st.nAnds;
                    grow(graphs, st, max_ands);
                    --st.nAnds;
                    st.hashed.erase(key);
                }
            }
        }
    }
}

static int count_graphs(const std::unordered_map<std::uint16_t, std::vector<RwGraph>>& graphs) {
    int n = 0;
    for (const auto& kv : graphs)
        n += (int)kv.second.size();
    return n;
}

static bool write_graphs(const char* path,
                         const std::unordered_map<std::uint16_t, std::vector<RwGraph>>& graphs) {
    if (!path || !*path) return !graphs.empty();
    std::ofstream os(path);
    if (!os) return false;
    os << "# 4-input rewrite subgraphs (up to " << kMaxPerClass
       << " cones per NPN class, nAnds <= best+" << kAndSlack << ")\n";
    os << "# classes " << graphs.size() << " graphs " << count_graphs(graphs) << '\n';
    os << graphs.size() << '\n';
    for (const auto& kv : graphs) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%04X", (unsigned)kv.first);
        for (const RwGraph& g : kv.second) {
            os << buf << ' ' << g.nAnds << ' ' << g.root;
            for (int i = 0; i < g.nAnds; ++i)
                os << ' ' << g.fanin0[i] << ' ' << g.fanin1[i];
            os << '\n';
        }
    }
    return (bool)os;
}

static void report_coverage(const std::vector<std::uint16_t>& classes,
                            const std::unordered_map<std::uint16_t, std::vector<RwGraph>>& graphs) {
    if (classes.empty()) {
        std::cout << "NPN class table not loaded; skip 222 coverage\n";
        return;
    }
    int covered = 0;
    std::vector<std::uint16_t> missing;
    for (std::uint16_t c : classes) {
        if (graphs.count(c))
            ++covered;
        else
            missing.push_back(c);
    }
    std::cout << "covered " << covered << " / " << classes.size()
              << " NPN classes, " << count_graphs(graphs) << " graphs\n";
    if (missing.empty()) return;
    std::cout << "missing " << missing.size() << ':';
    int show = (int)missing.size() < 16 ? (int)missing.size() : 16;
    for (int i = 0; i < show; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), " 0x%04X", (unsigned)missing[(std::size_t)i]);
        std::cout << buf;
    }
    if ((int)missing.size() > show)
        std::cout << " ...";
    std::cout << '\n';
}

bool RwLib::generate_graphs(const char* path, int max_ands) {
    if (max_ands < 1) max_ands = 1;
    if (max_ands > 8) max_ands = 8;

    if (_classes.empty())
        load_npn(kNpnPath);

    g_npn_cache.clear();
    _graphs.clear();
    GenState st;
    grow(_graphs, st, max_ands);
    report_coverage(_classes, _graphs);
    std::cout << "max_ands=" << max_ands << '\n';
    return write_graphs(path, _graphs);
}
