# mini-ls

Toy **logic synthesis** engine: AIG in, optimized AIG and K-LUT netlist out.

This is the front half of a personal CAD stack. A separate repo already has a
bookshelf placer (quadratic + Abacus legalize) and a g-cell router. The long-term
glue is:

```text
AIGER / BLIF  →  mini-ls (AIG + rewrite + LUT map)  →  placer / router