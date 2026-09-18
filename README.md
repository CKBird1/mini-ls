# mini-ls

Toy **logic synthesis** engine: AIG in, optimized AIG and K-LUT netlist out.

This is the front half of a personal CAD stack. A separate repo already has a
bookshelf placer (quadratic + Abacus legalize) and a g-cell router. The long-term
glue is:

```text
AIGER / BLIF  →  mini-ls (AIG + rewrite + LUT map)  →  placer / router

Day 1-4 (9-11-2026 -> 9-14-2026): Setup environment, repo, and core layout of tool. Use abc to get more comfortable with it along with basic review.
Day 4 (9-14-2026): Created AIG database as a mix of aigNode and aigGraph, both hand-written. AIGER parser is next
Day 5 (9-15-2026): AIGER parser is in, wrote print_stats, calculation for level including level tracking, wrote a method to clean dangling (using tombstone)
                    Balance is in, using huffman/priority_queue along with fanin traversal to collect  sets of ANDs that can be combined. Ensures we use correct pre-huffman data to avoid incorrect consideration of new ands made during the loops. next is Rewrite.
Day 6 (9-17-2026): Updated command line to be more ABC-style. It cannot allow live decision-making without needing to reload/rerun netlist, but it's good for now. mini-ls path/to/design.aig outfile.aig -c "balance rewrite balance" will call the equivalent of open-netlist -> balance -> rewrite -> balance -> print_stats -> quit. I also started the skeleton for rewrite today, but spent a couple hours really diving into the algorithm and variants itself, determining how I want to structure it, and what data structures/methods are best.