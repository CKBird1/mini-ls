#include "aig.hpp"
#include "rwlib.hpp"

void RwLib::load_hand() {
    if (!_graphs.empty()) return;

    // a XOR b  (tt 0x6666). Ids 0,1 = leaves a,b; 4,5,6 = ANDs.
    // n4 = a & b
    // n5 = ~a & ~b
    // n6 = ~n4 & ~n5   = XOR
    RwGraph xor_ab;
    xor_ab.nAnds = 3;
    xor_ab.fanin0[0] = make_lit(0, false);
    xor_ab.fanin1[0] = make_lit(1, false);
    xor_ab.fanin0[1] = make_lit(0, true);
    xor_ab.fanin1[1] = make_lit(1, true);
    xor_ab.fanin0[2] = make_lit(4, true);
    xor_ab.fanin1[2] = make_lit(5, true);
    xor_ab.root = make_lit(6, false);
    xor_ab.npn = npn_canon(0x6666);
    _graphs[xor_ab.npn.canon].push_back(xor_ab);

    /*
        This entire hand written section is a great learning experience, but boy has it caused a lot of extra work to actually run it through
        The way ABC does it's subgraph generation and storage involves storing in canon order, not in 'original' order
        So while I need to 'monkey around' to restore the original order and prevent the rewrite from creating nodes for dummies
        ABC would have easily known which are dummies and which aren't and used those.
        Eventually I will replace all that monkeying with what I think is the correct method, generally the more straight forward
        the better a solution is. 
    */



    // a & (b | c)  (tt 0xA8A8).
    // n4 = ~b & ~c     = NOR(b,c)
    // n5 = a & ~n4     = a & (b|c)
    RwGraph a_and_b_or_c;
    a_and_b_or_c.nAnds = 2;
    a_and_b_or_c.fanin0[0] = make_lit(1, true);
    a_and_b_or_c.fanin1[0] = make_lit(2, true);
    a_and_b_or_c.fanin0[1] = make_lit(0, false);
    a_and_b_or_c.fanin1[1] = make_lit(4, true);
    a_and_b_or_c.root = make_lit(5, false);
    a_and_b_or_c.npn = npn_canon(0xA8A8);
    _graphs[a_and_b_or_c.npn.canon].push_back(a_and_b_or_c);
}