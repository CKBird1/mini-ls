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
Day 7 (9-18-2026): Implemented cut_enumeration to create the cuts for each relevant node. Ensure unit cut is included so topo-sort ordering works properly, but later in flow we always skip the unit cut.
Day 8 (9-19-2026): Calculate truth tables for each cut, this 16-bit truth table is a simple way of storing the output values for all given input combinations for the cut, allowing for NPN chart referencing and eventual replacement later.
Day 9 (9-20-2026): Canonicalize the truth tables found in the previous loops. The goal is to go through all possible permutations and negations of the function so that ordering, duplicates etc all end up the same. This is brute force for now, on small netlists the 768 combinations per cut doesn't take that long. For bigger netlists this will need to be updated I'm guessing.
Day 10 (9-21-2026): Refactored the code, added balance.cpp and rewrite.cpp so they don't create one frankenstein aig.cpp file. They still link to aig.hpp to nothing else needed. Also created 3 helper functions to reduce some bitwise mess throughout the code.
Day 11 (9-22-2026): Added RwLibrary 'rewrite library' to store subgraphs based on truth tables. Also added a generator to generate all 222 NPN class types (not the subgraphs). Wrote some hand-drawn examples to load the library, great for deeper understanding.
Day 12 (9-23-2026): Created MFFC size to calculate gain, map_npn_leaves to tell which AIG nodes the subgraph should connect to. Rewrite loop fully works now, but requires more subgraph generation to actually function the way I intend it to.
Day 13 (9-24-2026): AIG enumerator added with max_ands set to 5. Creating a small set of subgraphs that hit often is much faster to find and use than the max max_ands = 8 which takes hours+ to run and would take significantly longer to use. Cleaned up more ways previous rewrite would leave dead nodes 'active' during specifc parts of the flow giving correct, but not optimal, results. Created a Kahn sort to make sure every time we run rewrite we truly have topological order, indegrees based on live ANDs only, no PI/PO/Const etc. Finally enumerate cuts runs once per node to make sure all generated cuts have current and real leaves. Doing it once total and snapshotting misses every change made during rewrite otherwise. Finally made MFFC more accurate by skipping leaves that will be staying in the AIG list. Previously I'd just assume all of them were 'deleted'. Finally moving onto K-LUT mapper (similar to ABC if -K x). Rewrite can always be improved though...