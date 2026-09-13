# Stage 0 notes

ABC: `~/eda/abc/abc`

Larger netlist, balance cut the depth, rewrite cut the AND count and then mapped to LUT 6s
## i10.aig (`i/o = 257/224`, combinational)

| After     | Size     | lev |
|-----------|----------|-----|
| strash    | 2675 AND | 50  |
| balance   | 2396 AND | 37  |
| rewrite   | 2046 AND | 36  |
| if -K 6   | 575 LUT  | 9   |


3-bit ripple carry adder:
Goal was to create an extremely simple netlist, and observe how it changes as I call abc-specific commands. In this case a 3-bit ripple carry adder -> strash turn it into an AIG, both balance and rewrite didn't find any improvements, but turning it into LUT 4 using if -K 4 minimized it significantly. Will do more experimenting as I go.

Next: Stage 1 AIG + AIGER + `print_stats` vs the strash row.