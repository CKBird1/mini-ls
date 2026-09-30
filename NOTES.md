# mini-ls notes

Working log. Current engine: [README.md](README.md). ABC lab notes: [STAGE0.md](STAGE0.md).

---

## Day 1–4 (9-11-2026 -> 9-14-2026)

Prepared environments, created repository, did labs to work with my installation of ABC, visualize its outputs, and become more deeply understanding of concepts like rewrite, balance, etc. Despite knowing these in the past, reviewing and putting into ABC was good solid memory recall/patching gaps.

## Day 4 (9-14-2026)

Created AIG database as a mix of `aigNode` and `aigGraph`, both hand-written. AIGER parser is next.

Created the `aigNode` and `aigGraph` that sit in `aig.cpp/hpp`. This includes functions to create new AND, PI, PO, and constants. It also includes the data structures needed for expanding the AIG, such as preventing duplicate additions using `unordered_map` to store previously made-ANDs, and constant folding so that we do not create ANDs that are guaranteed to have a specific single output (`x&x`, `x&0`, `x&~x` etc). Next is to add the AIGER parser (AIGER good for abc/yosys, good no-license tools to work with instead of trying to truly mimic company-sized codebases).

## Day 5 (9-15-2026)

AIGER parser is in, wrote `print_stats`, calculation for level including level tracking, wrote a method to clean dangling (using tombstone)

Balance is in, using `huffman/priority_queue` along with fanin traversal to collect sets of ANDs that can be combined. Ensures we use correct pre-huffman data to avoid incorrect consideration of new ands made during the loops. next is Rewrite.

## Day 6 (9-17-2026)

Updated command line to be more ABC-style. It cannot allow live decision-making without needing to reload/rerun netlist, but it's good for now. `mini-ls path/to/design.aig outfile.aig -c "balance rewrite balance"` will call the equivalent of open-netlist -> balance -> rewrite -> balance -> `print_stats` -> quit. I also started the skeleton for rewrite today, but spent a couple hours really diving into the algorithm and variants themselves, determining how I want to structure it, and what data structures/methods are best.

## Day 7 (9-18-2026)

Implemented `cut_enumeration` to create the cuts for each relevant node. Ensure unit cut is included so topo-sort ordering works properly, but later in flow we always skip the unit cut.

## Day 8 (9-19-2026)

Calculate truth tables for each cut, this 16-bit truth table is a simple way of storing the output values for all given input combinations for the cut, allowing for NPN chart referencing and eventual replacement later.

## Day 9 (9-20-2026)

Canonicalize the truth tables found in the previous loops. The goal is to go through all possible permutations and negations of the function so that ordering, duplicates etc all end up the same. This is brute force for now, on small netlists the 768 combinations per cut doesn't take that long. For bigger netlists this will need to be updated I'm guessing.

## Day 10 (9-21-2026)

Refactored the code, added `balance.cpp` and `rewrite.cpp` so they don't create one frankenstein `aig.cpp` file. They still link to `aig.hpp` to nothing else needed. Also created 3 helper functions to reduce some bitwise mess throughout the code.

## Day 11 (9-22-2026)

Added RwLibrary 'rewrite library' to store subgraphs based on truth tables. Also added a generator to generate all 222 NPN class types (not the subgraphs). Wrote some hand-drawn examples to load the library, great for deeper understanding.

## Day 12 (9-23-2026)

Created MFFC size to calculate gain, `map_npn_leaves` to tell which AIG nodes the subgraph should connect to. Rewrite loop fully works now, but requires more subgraph generation to actually function the way I intend it to.

## Day 13 (9-24-2026)

AIG enumerator added with `max_ands` set to 5. Creating a small set of subgraphs that hit often is much faster to find and use than the max `max_ands` = 8 which takes hours+ to run and would take significantly longer to use. Cleaned up more ways previous rewrite would leave dead nodes 'active' during specific parts of the flow giving correct, but not optimal, results. Created a Kahn sort to make sure every time we run rewrite we truly have topological order, indegrees based on live ANDs only, no PI/PO/Const etc. Finally enumerate cuts runs once per node to make sure all generated cuts have current and real leaves. Doing it once total and snapshotting misses every change made during rewrite otherwise. Finally made MFFC more accurate by skipping leaves that will be staying in the AIG list. Previously I'd just assume all of them were 'deleted'. Finally moving onto K-LUT mapper (similar to ABC `if -K x`). Rewrite can always be improved though...

## Day 14 (9-25-2026)

Set up the `LutCut` struct along with how to fill it. Mostly just modified struct `Cut` from previous efforts with updated enumeration requirements, truth table filling, and updated leaf counts. Since the goal is to map to LUT-6 or smaller, all cuts needed to accept more leaves and a bigger truth table (64bit). Enumeration finished, and since there are many possible cuts, sorted the final list by best delay -> best area and then only kept the top 8. This means `cuts[1]` is always the 'best', and therefore is the assumed LUT to be used during actual covering.

## Day 15 (9-26-2026)

Covering now finished, simply iterate over the POs, add their drivers to a worklist, and then recurse through the netlist. Since a `LutCut` stops at its leaves, all we need to do is imagine we had replaced the entire netlist from `PO->input_a` up to but not including the leaves of the best cut, then we use that as the new root and take its best cut and repeat. Then after finishing, setup print for count of LUTs and lev (mapped depth). Here is where we can see the LUT counts and lev compared to ABC. Shows how some of my optimizations aren't as good as ABC (obviously) but also getting quite close regardless.

## Day 16 (9-27-2026)

Mostly a 'results' driven day. Understood but did not fully author the blif/bench output. Blif output for ABC checking and bench output so I can have a full flow feeding into my mini-pd repo. I can now take a starting AIG netlist and take it all the way through synthesis optimization -> placer -> legalize -> routing. Ran tests, full flows, and determined the main results of this stage of synthesis mini-ls.

## Day 17 (9-28-2026)

Cleaned up README, created NOTES, and had AI help with beautifying the display of the results along with making the repo look more professional. I did not change any of the code, and leaving my current comments within the code for now. I may remove them later, but for now they are good reference of what I did. Starting on adding some constraints and usage of them to this mini-ls. Constrained vs unconstrained results will be interesting and helpful.

Added the beginnings of constraint/requirement comprehension in mini-ls. Currently it's somewhat simple, we can declare a period on the command line, and then the K-LUT mapper takes it in, and will use it as part of the determination process for what the 'best cut' is. Instead of delay->area for best cut, if these two cuts meet timing, optimize for area, otherwise take the one that meets, if neither meet, default to delay. In each run where period is added, we can see the pre and post optimizations for timing requirement, and we save around 7% area based on period given.

## Day 18 (9-29-2026)

Added --max-delay i:N as an option into mini-ls. It will do the same as set_max_delay on the given PO to constrain that path more than the period itself will. The code works if you set one, the other, or both. Worst negative slack is now calculated with this in mind, not simply period - mapped-depth. When populating required_time vector the max delay is taken into account with the rest of the code staying the same. Happy to be adding more constraints/clock relevant as I move through.
