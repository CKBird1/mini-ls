# mini-ls

Toy **logic synthesis** engine: AIG in, optimized AIG and K-LUT netlist out.

This is the front half of a personal CAD stack. A separate repo already has a
bookshelf placer (quadratic + Abacus legalize) and a g-cell router. The long-term
glue is:

```text
AIGER / BLIF  →  mini-ls (AIG + rewrite + LUT map)  →  placer / router

Day 1-4 (9-11-2026 -> 9-14-2026): Setup environment, repo, and core layout of tool. Use abc to get more comfortable with it along with basic review.
Day 4 (9-14-2026): Created AIG database as a mix of aigNode and aigGraph, both hand-written. AIGER parser is next